#include "process.h"
#include "bin/embedded_mapping.h"
#include "scheduler/parked.h"
#include "exec/load.h"
#include "bin/elf.h"
#include "arch/page_table.h"
#include "arch/device.h"
#include "arch/process.h"
#include "arch/process_context.h"
#include "types/bstring.h"
#include "types/result.h"
#include "types/optional.h"
#include "kernel_core.h"
#include "irq/irq_dispatcher.h"
#include "service/service_directory.h"
#include "../common_includes/syscalls/spawn.h"

#include "kalloc.h"
#include "stack.h"
#include "idle_debug.h"

FixedArrayBodyDefine(ProcessTable);
FixedArrayBodyDefine(ProcessArguments);
FixedArrayBodyDefine(SpawnArguments);
FixedArrayBodyDefine(ProcessChildEvents);
LinkedListBodyDefine(UserAllocations);
LinkedListBodyDefine(ParkedOperations);

static ProcessID pid_counter = 0;
static mutex_t pid_lock = MUTEX_INIT;

static BString no_entry_point_message = bstr("No entry point found in ELF");
static BString user_allocation_not_found = bstr("User allocation not found");
static BString static_user_allocation = bstr("Cannot free static user allocation");
static BString user_allocation_full = bstr("Too many user allocations");
static BString invalid_heap_break = bstr("Invalid heap break");
static BString argument_too_long = bstr("Argument too long");
static const ProcessCapabilities PROCESS__HUND_CAPABILITIES =
    PROCESS_CAP_BECOME_SERVICE | PROCESS_CAP_MAP_MMIO | PROCESS_CAP_SUBSCRIBE_IRQ | PROCESS_CAP_REGISTER_BLOCK_DEVICE |
    PROCESS_CAP_HANDLE_BLOCK_REQUESTS | PROCESS_CAP_MOUNT_FILESYSTEM | PROCESS_CAP_USE_BLOCK_DEVICES |
    PROCESS_CAP_MANAGE_SHARED_PAGES;

static ProcessSignalMask ProcessSignal__mask(ProcessSignal signal)
{
    return (ProcessSignalMask)1ULL << signal;
}

static size_t Process__page_aligned_size(size_t size)
{
    return (size + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
}

static Address Process__page_base(Address address)
{
    return address & ~(PAGE_SIZE - 1);
}

bool UserAllocation__equals(UserAllocationPtr self, UserAllocationPtr other)
{
    return self->address.virtual_addr == other->address.virtual_addr;
}

static const char * UserAllocationType__name(UserAllocationType type)
{
    switch (type)
    {
    case USER_ALLOCATION_ELF:
        return "ELF";
    case USER_ALLOCATION_STACK:
        return "STACK";
    case USER_ALLOCATION_HEAP:
        return "HEAP";
    case USER_ALLOCATION_DYNAMIC:
        return "DYNAMIC";
    case USER_ALLOCATION_MMIO:
        return "MMIO";
    case USER_ALLOCATION_SHARED:
        return "SHARED";
    }

    return "UNKNOWN";
}

bool ProcessChildEvent__equals(ProcessChildEventPtr self, ProcessChildEventPtr other)
{
    return self->pid == other->pid && self->status == other->status && self->type == other->type;
}

void Process__set_identity(ProcessPtr self, UserID user_id, GroupID group_id, ProcessCapabilities capabilities)
{
    self->user_id = user_id;
    self->group_id = group_id;
    self->capabilities = capabilities;
}

void Process__set_root_identity(ProcessPtr self)
{
    Process__set_identity(self, PROCESS_USER_ROOT, PROCESS_GROUP_ROOT, PROCESS_CAP_ALL);
}

void Process__set_hund_identity(ProcessPtr self)
{
    Process__set_identity(self, PROCESS_USER_HUND, PROCESS_GROUP_HUND, PROCESS__HUND_CAPABILITIES);
}

void Process__set_user_identity(ProcessPtr self)
{
    Process__set_identity(self, PROCESS_USER_USER, PROCESS_GROUP_USER, PROCESS_CAP_NONE);
}

bool Process__is_root(ProcessPtr self)
{
    return self->user_id == PROCESS_USER_ROOT;
}

bool Process__has_capability(ProcessPtr self, ProcessCapabilities capability)
{
    return Process__is_root(self) || (self->capabilities & capability) == capability;
}

static bool allocation_matches_virtual(UserAllocationPtr allocation, void * context)
{
    Address virtual_address = *(AddressPtr)context;
    return allocation->address.virtual_addr == virtual_address;
}

typedef struct UserAllocationRangeMatch
{
    Address virtual_address;
    size_t size;
} UserAllocationRangeMatch;

typedef UserAllocationRangeMatch * UserAllocationRangeMatchPtr;

static UserAllocationRangeMatch UserAllocationRangeMatch__new(Address virtual_address, size_t size)
{
    return (UserAllocationRangeMatch){
        .virtual_address = virtual_address,
        .size = size,
    };
}

static UserAllocation UserAllocation__new(UserAddress address, size_t size, UserAllocationType type)
{
    return (UserAllocation){
        .address = address,
        .size = size,
        .type = type,
    };
}

static bool allocation_contains_range(UserAllocationPtr allocation, void * context)
{
    UserAllocationRangeMatchPtr range = (UserAllocationRangeMatchPtr)context;
    Address allocation_start = allocation->address.virtual_addr;
    Address allocation_end = allocation_start + allocation->size;
    Address range_end = range->virtual_address + range->size;

    return range->virtual_address >= allocation_start && range_end <= allocation_end &&
           range_end >= range->virtual_address;
}

static size_t Process__copy_arg(DataPtr target, BStringPtr arg)
{
    StringPos length = BString__length(arg);
    for (StringPos i = 0; i < length; i++)
    {
        target[i] = (Data)BString__char_at_or(arg, i, '\0');
    }
    target[length] = '\0';
    return (size_t)length + 1;
}

static Result(Address) Process__setup_args(ProcessPtr self, ConstProcessArgumentsPtr arguments, uint64_t * argc)
{
    match_result(
        Process__alloc_user_pages(self, PAGE_SIZE),
        success(args_page)
        {
            Address user_base = args_page.virtual_addr;
            Address kernel_base = args_page.kernel_addr.virtual_addr;
            AddressPtr argv = ptr(kernel_base, Address);
            ProcessArguments arguments_copy = *arguments;

            *argc = ProcessArguments__len(&arguments_copy);

            size_t cursor = sizeof(Address) * (*argc + 1);
            for_each_fa(
                ProcessArguments,
                arguments_copy,
                i,
                arg,
                {
                    if (cursor + (size_t)BString__length(&arg) + 1 > PAGE_SIZE)
                    {
                        return Error(Address, argument_too_long, SPAWN_ARG_TOO_LONG);
                    }
                    argv[i] = user_base + cursor;
                    cursor += Process__copy_arg(ptr(kernel_base + cursor, Data), &arg);
                }
            );
            argv[*argc] = 0;
            return Success(Address, user_base);
        },
        error(code, message)
        {
            return Error(Address, message, code);
        }
    );

    return Error(Address, argument_too_long, SPAWN_ARG_TOO_LONG);
}

static Result(ProcessSetupStatus) Process__map_service_directory(ProcessPtr self)
{
    match_result(
        EmbeddedArchive__map_process(self),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ProcessSetupStatus, message, code);
        }
    );
    match_result(
        KernelProcessRegistry__map_process(&self->kernel->process_registry, self),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP);
        }
    );
    match_result(
        KernelDeviceRegistry__map_process(&self->kernel->device_registry, self),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP);
        }
    );
    if (!self->kernel->service_directory.directory.valid)
    {
        match_result(
            KernelServiceDirectory__init(&self->kernel->service_directory),
            success_do_nothing(),
            error(code, message)
            {
                return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP);
            }
        );
    }

    match_result(
        KernelServiceDirectory__map_process(&self->kernel->service_directory, self),
        success(_)
        {
            return Success(ProcessSetupStatus, PROCESS_SETUP);
        },
        error(code, message)
        {
            return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP);
        }
    );

    return Error(ProcessSetupStatus, user_allocation_not_found, PROCESS_FAILED_TO_SETUP);
}

Result(ProcessPtr)
    Process__new(KernelPtr kernel, ConstBStringPtr name, ConstProcessPageTablePtr page_table, ConstUserAddressPtr entry,
                 ConstStackPtr stack, OptionalPtr(ProcessPtr) parent, ConstProcessArgumentsPtr arguments)
{
    ProcessID pid;

    critical_section(
        pid_lock,
        pid = ++pid_counter
    );

    __d_printf(PROCESS, "Creating process with entry point %p\n", entry);
    ProcessPtr proc = ptr(Kernel__alloc_kernel(sizeof(Process)), Process);
    proc->lock = MUTEX_INIT;
    proc->kernel = kernel;
    proc->entry_point = *entry;
    proc->page_table = *page_table;
    proc->state = PROC_STATE_NEW;
    proc->pid = pid;
    proc->stack = *stack;
    proc->last_tick = 0;
    proc->dispatched_at = 0;
    proc->migration_safe = true;
    proc->preferred_active = false;
    proc->preferred_warm = false;
    proc->preferred_ticket = None(RequestTicket);
    proc->cpu_usage = CPUUsage__new();
    proc->queue_wait_usage = QueueWaitUsage__new();
    proc->queue_wait_control = None(UserAddress);
    proc->name = *name;
    proc->parked_operations = ParkedOperations__new();
    proc->parent = *parent;
    proc->heap_break = USER_HEAP_BASE;
    proc->heap_mapped_end = USER_HEAP_BASE;
    proc->allocation_next = USER_ALLOC_BASE;
    proc->user_allocations = UserAllocations__new();
    proc->virtio_device = None(UserAddress);
    proc->startup_stdio = SPAWN_STDIO_DEFAULT;
    match_optional(
        *parent,
        some(parent_process)
        {
            proc->startup_stdio = parent_process->startup_stdio;
        },
        none_do_nothing()
    );
    proc->abort = None(ProcessAbort);
    proc->process_class = PROCESS_CLASS_NORMAL;
    proc->exec_load = None(ExecLoadPtr);
    proc->service = ServiceState__new();
    proc->idle_debug_address = None(UserAddress);
    proc->user_id = PROCESS_USER_ROOT;
    proc->group_id = PROCESS_GROUP_ROOT;
    proc->capabilities = PROCESS_CAP_ALL;
    proc->pending_signals = 0;
    proc->child_events = ProcessChildEvents__new();

    match_optional(
        *parent,
        some(parent_process)
        {
            Process__set_identity(proc, parent_process->user_id, parent_process->group_id,
                                  parent_process->capabilities);
        },
        none()
        {
            Process__set_root_identity(proc);
        }
    );

    match_result(
        Process__map_service_directory(proc),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ProcessPtr, message, code);
        }
    );

    uint64_t argc;
    match_result(
        Process__setup_args(proc, arguments, &argc),
        success(argv)
        {
            __d_printf(
                PROCESS,
                "Process__new before context init pid=%i proc=%p stack_ptr=%p stack_base=%p stack_top=%p argc=%i argv=%p entry=%p page_table=%p\n",
                proc->pid, proc, &proc->stack, proc->stack.base.virtual_addr, proc->stack.top.virtual_addr, argc, argv,
                entry->virtual_addr, ProcessPageTable__root(&proc->page_table).phys_addr);
            KernelAddress root = ProcessPageTable__root(&proc->page_table);
            proc->context = ProcessContext__init(kernel, proc->pid, &proc->stack, *entry, root, argc, argv);
            __d_printf(PROCESS, "Process__new after context init pid=%i proc=%p context=%p\n", proc->pid, proc,
                       proc->context);
            return Success(ProcessPtr, proc);
        },
        error(code, message)
        {
            return Error(ProcessPtr, message, code);
        }
    );

    return Error(ProcessPtr, argument_too_long, SPAWN_ARG_TOO_LONG);
}

Result(CollectionCode) Process__push_parked_operation(ProcessPtr self, ParkedOperationPtr operation)
{
    critical_section(
        self->lock,
        {
            match_result(
                ParkedOperations__push(&self->parked_operations, operation),
                success(index)
                {
                    cs_return Success(CollectionCode, index);
                },
                error(code, message)
                {
                    cs_return Error(CollectionCode, message, code);
                }
            );
        }
    );

    return Error(CollectionCode, bstr("Unable to push parked operation"), LINKED_LIST_ALLOCATION_FAILED);
}

Optional(ParkedOperationPtr) Process__pop_parked_operation(ProcessPtr self)
{
    critical_section(
        self->lock,
        {
            match_optional(
                ParkedOperations__remove_at(&self->parked_operations, -1),
                some(parked_operation)
                {
                    cs_return Some(ParkedOperationPtr, parked_operation);
                },
                none_do_nothing()
            );
        }
    );

    return None(ParkedOperationPtr);
}

Optional(ParkedOperationPtr) Process__pop_ready_parked_operation(ProcessPtr self)
{
    critical_section(
        self->lock,
        {
            for (int64_t index = (int64_t)ParkedOperations__len(&self->parked_operations) - 1; index >= 0; index--)
            {
                match_optional(
                    ParkedOperations__at(&self->parked_operations, index),
                    some(parked_operation)
                    {
                        if (ParkedOperation__is_ready(parked_operation))
                        {
                            match_optional(
                                ParkedOperations__remove_at(&self->parked_operations, index),
                                some(removed)
                                {
                                    cs_return Some(ParkedOperationPtr, removed);
                                },
                                none_do_nothing()
                            );
                        }
                    },
                    none_do_nothing()
                );
            }
        }
    );

    return None(ParkedOperationPtr);
}

Optional(ParkReason) Process__top_park_reason(ProcessPtr self)
{
    critical_section(
        self->lock,
        {
            match_optional(
                ParkedOperations__at(&self->parked_operations, -1),
                some(parked_operation)
                {
                    cs_return Some(ParkReason, parked_operation->park_reason);
                },
                none_do_nothing()
            );
        }
    );

    return None(ParkReason);
}

Optional(SchedulerPtr) Process__top_parked_operation_scheduler(ProcessPtr self)
{
    critical_section(
        self->lock,
        {
            match_optional(
                ParkedOperations__at(&self->parked_operations, -1),
                some(parked_operation)
                {
                    match_optional(
                        parked_operation->kernel_context->scheduler,
                        some(scheduler)
                        {
                            cs_return Some(SchedulerPtr, scheduler);
                        },
                        none_do_nothing()
                    );
                },
                none_do_nothing()
            );
        }
    );

    return None(SchedulerPtr);
}

bool Process__has_parked_operation(ProcessPtr self, ParkReason reason, OptionalPtr(OperationKey) operation_key)
{
    critical_section(
        self->lock,
        {
            for (int64_t index = (int64_t)ParkedOperations__len(&self->parked_operations) - 1; index >= 0;
                 index--)
            {
                match_optional(
                    ParkedOperations__at(&self->parked_operations, index),
                    some(parked_operation)
                    {
                        if (parked_operation->park_reason != reason)
                        {
                            continue;
                        }

                        match_optional(
                            *operation_key,
                            some(key)
                            {
                                match_optional(
                                    parked_operation->operation_key,
                                    some(parked_key)
                                    {
                                        if (parked_key == key)
                                        {
                                            cs_return true;
                                        }
                                    },
                                    none_do_nothing()
                                );
                            },
                            none()
                            {
                                cs_return true;
                            }
                        );
                    },
                    none_do_nothing()
                );
            }
        }
    );

    return false;
}

bool Process__has_ready_parked_operation(ProcessPtr self, ParkReason reason, OptionalPtr(OperationKey) operation_key)
{
    critical_section(
        self->lock,
        {
            for (int64_t index = (int64_t)ParkedOperations__len(&self->parked_operations) - 1; index >= 0; index--)
            {
                match_optional(
                    ParkedOperations__at(&self->parked_operations, index),
                    some(parked_operation)
                    {
                        if (parked_operation->park_reason != reason || !ParkedOperation__is_ready(parked_operation))
                        {
                            continue;
                        }

                        match_optional(
                            *operation_key,
                            some(key)
                            {
                                match_optional(
                                    parked_operation->operation_key,
                                    some(parked_key)
                                    {
                                        if (parked_key == key)
                                        {
                                            cs_return true;
                                        }
                                    },
                                    none_do_nothing()
                                );
                            },
                            none()
                            {
                                cs_return true;
                            }
                        );
                    },
                    none_do_nothing()
                );
            }
        }
    );

    return false;
}

bool Process__mark_parked_operation_ready(ProcessPtr self, ParkReason reason, OptionalPtr(OperationKey) operation_key,
                                          OptionalPtr(ReturnCode) return_code)
{
    critical_section(
        self->lock,
        {
            for (int64_t index = (int64_t)ParkedOperations__len(&self->parked_operations) - 1; index >= 0;
                 index--)
            {
                match_optional(
                    ParkedOperations__at(&self->parked_operations, index),
                    some(parked_operation)
                    {
                        if (parked_operation->park_reason != reason)
                        {
                            continue;
                        }

                        match_optional(
                            *operation_key,
                            some(key)
                            {
                                match_optional(
                                    parked_operation->operation_key,
                                    some(parked_key)
                                    {
                                        if (parked_key == key)
                                        {
                                            parked_operation->return_code = *return_code;
                                            ParkedOperation__mark_ready(parked_operation);
                                            cs_return true;
                                        }
                                    },
                                    none_do_nothing()
                                );
                            },
                            none()
                            {
                                parked_operation->return_code = *return_code;
                                ParkedOperation__mark_ready(parked_operation);
                                cs_return true;
                            }
                        );
                    },
                    none_do_nothing()
                );
            }
        }
    );

    return false;
}

bool Process__wake_expired_sleep(ProcessPtr self, Tick current_tick)
{
    critical_section(
        self->lock,
        {
            if (self->state != PROC_STATE_SLEEPING && self->state != PROC_STATE_WAITING)
            {
                cs_return false;
            }

            for (int64_t index = (int64_t)ParkedOperations__len(&self->parked_operations) - 1; index >= 0; index--)
            {
                match_optional(
                    ParkedOperations__at(&self->parked_operations, index),
                    some(parked_operation)
                    {
                        if (!((parked_operation->park_reason == PARK_SLEEP && self->state == PROC_STATE_SLEEPING) ||
                              (parked_operation->park_reason == PARK_WAIT_QUEUE && self->state == PROC_STATE_WAITING)))
                        {
                            continue;
                        }

                        match_optional(
                            parked_operation->operation_key,
                            some(deadline)
                            {
                                if (current_tick >= deadline)
                                {
                                parked_operation->return_code = Some(
                                    ReturnCode, parked_operation->park_reason == PARK_WAIT_QUEUE ? QUEUE_WAIT_TIMEOUT
                                                                                                 : PROCESS_SLEEP_OK);
                                ParkedOperation__mark_ready(parked_operation);
                                self->state = PROC_STATE_INTERRUPTED;
                                if (parked_operation->park_reason == PARK_WAIT_QUEUE)
                                {
                                    QueueWaitUsage__wake(&self->queue_wait_usage, QUEUE_WAIT_TIMEOUT);
                                }
                                cs_return true;
                                }
                            },
                            none_do_nothing()
                        );
                    },
                    none_do_nothing()
                );
            }
        }
    );

    return false;
}

bool Process__is_parked(ProcessPtr self)
{
    critical_section(
        self->lock,
        {
            cs_return ParkedOperations__len(&self->parked_operations) > 0;
        }
    );

    return false;
}

bool Process__is_handling_service_irq(ProcessPtr self)
{
    match_optional(self->service.active_irq_event,
        some(event)
        {
            return true;
        },
        none_do_nothing()
    );

    return false;
}

bool Process__supports_signal(ProcessPtr self, ProcessSignal signal)
{
    switch (signal)
    {
    case PROCESS_SIGNAL_INTERRUPT:
    case PROCESS_SIGNAL_KILL:
    case PROCESS_SIGNAL_TERMINATE:
        return true;
    default:
        return false;
    }
}

void Process__queue_signal(ProcessPtr self, ProcessSignal signal)
{
    __atomic_fetch_or(&self->pending_signals, ProcessSignal__mask(signal), __ATOMIC_RELEASE);
}

bool Process__has_pending_signal(ProcessPtr self)
{
    return __atomic_load_n(&self->pending_signals, __ATOMIC_ACQUIRE) != 0;
}

Optional(ProcessSignal) Process__take_pending_signal(ProcessPtr self)
{
    if (!Process__has_pending_signal(self))
    {
        return None(ProcessSignal);
    }

    ProcessSignal ordered[] = {
        PROCESS_SIGNAL_KILL,
        PROCESS_SIGNAL_TERMINATE,
        PROCESS_SIGNAL_INTERRUPT,
    };

    for (size_t i = 0; i < sizeof(ordered) / sizeof(ordered[0]); i++)
    {
        ProcessSignal signal = ordered[i];
        ProcessSignalMask mask = ProcessSignal__mask(signal);
        if ((__atomic_fetch_and(&self->pending_signals, ~mask, __ATOMIC_ACQ_REL) & mask) != 0)
        {
            return Some(ProcessSignal, signal);
        }
    }

    return None(ProcessSignal);
}

ReturnCode ProcessSignal__exit_code(ProcessSignal signal)
{
    return PROCESS_SIGNAL_EXIT_BASE - signal;
}

Result(ProcessSetupStatus) Process__dispose_owned_kernel_resources(ProcessPtr self)
{
    if (self == NULL || self->kernel == NULL) // hundelint: ignore-no-null
    {
        return Error(ProcessSetupStatus, argument_too_long, PROCESS_FAILED_TO_SETUP);
    }

    Process__quiesce_device(self);
    ExecLoad__cancel(self);
    KernelDeviceRegistry__remove_process(&self->kernel->device_registry, self->pid);

    match_result(
        IRQDispatcher__dispose_process_resources(self->kernel->irq_dispatcher, self),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP);
        }
    );

    return Success(ProcessSetupStatus, PROCESS_SETUP);
}

Result(KAllocCode) Process__dispose_page_table(ProcessPtr self)
{
    return ProcessPageTable__dispose(&self->page_table, self->kernel);
}

Result(ProcessChildEvent) Process__pop_next_child_event(ProcessPtr self)
{
    match_optional(
        ProcessChildEvents__pop_front(&self->child_events),
        some(event)
        {
            return Success(ProcessChildEvent, event);
        },
        none_do_nothing()
    );

    return Error(ProcessChildEvent, bstring("Child event queue empty"), PROCESS_CHILD_EVENT_QUEUE_EMPTY);
}

Result(CollectionCode) Process__push_child_event(ProcessPtr self, ConstProcessChildEventPtr event)
{
    return ProcessChildEvents__push(&self->child_events, *event);
}

Result(ProcessPtr) Process__setup(KernelPtr kernel, ConstElfPtr binary, OptionalPtr(ProcessPtr) parent)
{
    ProcessArguments arguments = ProcessArguments__new();
    return Process__setup_with_args(kernel, binary, parent, &arguments);
}

Result(ProcessPtr) Process__setup_with_args(KernelPtr kernel, ConstElfPtr binary, OptionalPtr(ProcessPtr) parent,
                                            ConstProcessArgumentsPtr arguments)
{
    __d_println(PROCESS, "Process__spawn_from_elf");
    ExpandedSegmentArray expanded_elf = Elf__map(binary);
    __d_printf(PROCESS, "ELF: %s %x %x\n", cstr(binary->name), binary->start, binary->end);
    __d_printf(PROCESS, "ELF mapped %i\n", ExpandedSegmentArray__len(&expanded_elf));

    __d_println(PROCESS, "Creating page table");
    ProcessPageTable page_table;
    match_result(
        ProcessPageTable__new(kernel),
        success(created_page_table)
        {
            page_table = created_page_table;
        },
        error(code, message)
        {
            return Error(ProcessPtr, message, code);
        }
    );

    __d_println(PROCESS, "Page table created");

    for_each_fa(
        ExpandedSegmentArray,
        expanded_elf,
        i,
        segment,
        {
            UserAddress addr = segment.address;
            __d_printf(MMU, "Mapping segment %i phys=%p, virt=%p\n", i, addr.kernel_addr.phys_addr,
                       addr.virtual_addr);
            PageTable__map(&page_table, kernel, segment.address, segment.size);
        }
    );
    __d_println(PROCESS, "Code mapped");

    __d_printf(PROCESS, "Creating stack for %s\n", cstr(binary->name));
    Stack stack = Stack__new();
    __d_printf(PROCESS, "Stack created for %s base=%p top=%p kbase=%p phys=%p\n", cstr(binary->name),
               stack.base.virtual_addr, stack.top.virtual_addr, stack.base.kernel_addr.virtual_addr,
               stack.base.kernel_addr.phys_addr);
    __d_printf(PROCESS, "Mapping stack for %s page_table=%p\n", cstr(binary->name),
               ProcessPageTable__root(&page_table).phys_addr);
    Stack__map(kernel, &stack, &page_table);
    __d_println(PROCESS, "Stack mapped");

    match_optional(
        Elf__entry_segment(binary, &expanded_elf),
        some(entry_point)
        {
            Offset entry_offset = Elf__read_header(binary)->entry - entry_point.address.virtual_addr;
            UserAddress entry_address = UserAddress__relative(&entry_point.address, entry_offset);
            ProcessArguments process_arguments = ProcessArguments__new();
            ProcessArguments requested_arguments = *arguments;
            match_result(
                ProcessArguments__push(&process_arguments, binary->name),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(ProcessPtr, message, SPAWN_ARG_TOO_LONG);
                }
            );
            match_result(
                ProcessArguments__push_all(&process_arguments, &requested_arguments),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(ProcessPtr, argument_too_long, SPAWN_ARG_TOO_LONG);
                }
            );

            match_result(
                Process__new(kernel, &binary->name, &page_table, &entry_address, &stack, parent, &process_arguments),
                success(proc)
                {
                    for_each_fa(
                        ExpandedSegmentArray,
                        expanded_elf,
                        segment_index,
                        segment,
                        {
                            Process__record_user_allocation(proc, segment.address, segment.size,
                                                            USER_ALLOCATION_ELF);
                        }
                    );
                    Process__record_user_allocation(proc, stack.base, STACK_SIZE, USER_ALLOCATION_STACK);
                    return Success(ProcessPtr, proc);
                },
                error(code, message)
                {
                    return Error(ProcessPtr, message, code);
                }
            );
        },
        none()
        {
            __d_println(PANIC, cstr(no_entry_point_message));
            return Error(ProcessPtr, no_entry_point_message, PROCESS_FAILED_TO_SETUP);
        }
    );

    return Error(ProcessPtr, no_entry_point_message, PROCESS_FAILED_TO_SETUP);
}

Result(ProcessSetupStatus) Process__dispose(ProcessPtr self)
{
    while (ParkedOperations__len(&self->parked_operations) > 0)
    {
        match_optional(
            ParkedOperations__remove_at(&self->parked_operations, 0),
            some(operation)
            {
                ParkedOperation__dispose(operation);
            },
            none_do_nothing()
        );
    }
    match_result(
        ParkedOperations__dispose(&self->parked_operations),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP);
        }
    );

    match_result(
        UserAllocations__dispose(&self->user_allocations),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP);
        }
    );

    ProcessContext__dispose(self->context);
    match_result(
        Kernel__free(addr(self)),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP);
        }
    );

    return Success(ProcessSetupStatus, PROCESS_SETUP);
}

Result(ProcessMemoryCode)
    Process__record_user_allocation(ProcessPtr self, UserAddress address, size_t size, UserAllocationType type)
{
    UserAllocation allocation = UserAllocation__new(address, Process__page_aligned_size(size), type);

    match_result(
        UserAllocations__push(&self->user_allocations, allocation),
        success(_)
        {
            __d_printf(MEMORY, "process alloc pid=%i name=%s type=%s user=%p kernel=%p phys=%p size=%i tracked=%i\n",
                       self->pid, cstr(self->name), UserAllocationType__name(type), address.virtual_addr,
                       address.kernel_addr.virtual_addr, address.kernel_addr.phys_addr, allocation.size,
                       UserAllocations__len(&self->user_allocations));
            return Success(ProcessMemoryCode, PROCESS_MEMORY_OK);
        },
        error(code, message)
        {
            __d_printf(MEMORY,
                       "process alloc tracking failed pid=%i name=%s type=%s user=%p kernel=%p size=%i code=%i\n",
                       self->pid, cstr(self->name), UserAllocationType__name(type), address.virtual_addr,
                       address.kernel_addr.virtual_addr, allocation.size, code);
            return Error(ProcessMemoryCode, user_allocation_full, PROCESS_MEMORY_FULL);
        }
    );

    return Error(ProcessMemoryCode, user_allocation_full, PROCESS_MEMORY_FULL);
}

Result(ProcessMemoryCode) Process__claim_virtio_device(ProcessPtr self, Address mapping)
{
    match_optional(
        self->virtio_device,
        some(device)
        {
            return Error(ProcessMemoryCode, user_allocation_full, PROCESS_MEMORY_FULL);
        },
        none_do_nothing()
    );
    for_each_ll(
        UserAllocations,
        self->user_allocations,
        allocation,
        {
            Address start = allocation.address.virtual_addr;
            if (allocation.type == USER_ALLOCATION_MMIO && mapping >= start && allocation.size >= 0x100 &&
                mapping - start <= allocation.size - 0x100 && (mapping & 3) == 0 &&
                Arch__is_virtio_block_device(mapping))
            {
                UserAddress device = allocation.address;
                device.virtual_addr = mapping;
                self->virtio_device = Some(UserAddress, device);
                return Success(ProcessMemoryCode, PROCESS_MEMORY_OK);
            }
        }
    );
    return Error(ProcessMemoryCode, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
}

void Process__quiesce_device(ProcessPtr self)
{
    match_optional(
        self->virtio_device,
        some(device)
        {
            Arch__reset_virtio_device(device.virtual_addr, self->page_table.root.phys_addr);
            self->virtio_device = None(UserAddress);
        },
        none_do_nothing()
    );
}

Result(Address) Process__device_address(ProcessPtr self, Address virtual_address)
{
    if (self == NULL || virtual_address == 0) // hundelint: ignore-no-null
    {
        return Error(Address, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
    }

    for_each_ll(
        UserAllocations,
        self->user_allocations,
        allocation,
        {
            Address start = allocation.address.virtual_addr;
            Address end = start + allocation.size;
            if (virtual_address >= start && virtual_address < end)
            {
                Address offset = virtual_address - start;
                if (allocation.type == USER_ALLOCATION_MMIO)
                {
                    return Success(Address, allocation.address.kernel_addr.phys_addr + offset);
                }

                KernelAddress address = Kernel__from_virtual_address(
                    self->kernel, allocation.address.kernel_addr.virtual_addr + offset);
                if (!address.valid)
                {
                    return Error(Address, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
                }

                return Success(Address, address.phys_addr);
            }
        }
    );
    return Error(Address, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
}

Result(UserAddress) Process__alloc_user_pages(ProcessPtr self, size_t size)
{
    if (size == 0 || self->allocation_next >= HUND_SERVICE_DIRECTORY_VADDR ||
        size > HUND_SERVICE_DIRECTORY_VADDR - self->allocation_next)
    {
        return Error(UserAddress, user_allocation_full, PROCESS_MEMORY_INVALID_SIZE);
    }
    size_t allocation_size = Process__page_aligned_size(size);
    Address virtual_address = self->allocation_next;

    match_result(
        Kernel__try_alloc_user(virtual_address, allocation_size, true, true, false),
        success(allocation)
        {
            PageTable__map(&self->page_table, self->kernel, allocation, allocation_size);

            match_result(
                Process__record_user_allocation(self, allocation, allocation_size, USER_ALLOCATION_DYNAMIC),
                success(_)
                {
                    self->allocation_next += allocation_size;
                    return Success(UserAddress, allocation);
                },
                error(code, message)
                {
                    PageTable__unmap(&self->page_table, self->kernel, allocation.virtual_addr, allocation_size);
                    Kernel__free(allocation.kernel_addr.virtual_addr);
                    return Error(UserAddress, message, code);
                }
            );
        },
        error(code, message)
        {
            return Error(UserAddress, message, PROCESS_MEMORY_FULL);
        }
    );

    return Error(UserAddress, user_allocation_full, PROCESS_MEMORY_FULL);
}

Result(SharedUserPages) Process__map_shared_user_pages_with_allocation(ProcessPtr self, ProcessPtr source,
                                                                       Address source_virtual_address, size_t size)
{
    if (self == NULL || source == NULL || source_virtual_address == 0 || size == 0) // hundelint: ignore-no-null
    {
        return Error(SharedUserPages, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
    }

    Address source_base = Process__page_base(source_virtual_address);
    Offset source_offset = source_virtual_address - source_base;
    size_t allocation_size = Process__page_aligned_size(source_offset + size);
    if (source_offset + size < size || allocation_size < size ||
        self->allocation_next >= HUND_SERVICE_DIRECTORY_VADDR ||
        allocation_size > HUND_SERVICE_DIRECTORY_VADDR - self->allocation_next)
    {
        return Error(SharedUserPages, user_allocation_full, PROCESS_MEMORY_INVALID_SIZE);
    }
    UserAllocationRangeMatch range = UserAllocationRangeMatch__new(source_base, allocation_size);

    match_find_result_ptr(
        UserAllocations__find(&source->user_allocations, allocation_contains_range, &range),
        some(allocation_match)
        {
            match_optional(
                source->queue_wait_control,
                some(control)
                {
                    if (control.virtual_addr == allocation_match->address.virtual_addr)
                    {
                        return Error(SharedUserPages, static_user_allocation, PROCESS_MEMORY_STATIC_ALLOCATION);
                    }
                },
                none_do_nothing()
            );
            Offset backing_offset = source_base - allocation_match->address.virtual_addr;
            Address target_base = self->allocation_next;
            KernelAddress mapping_kernel_address =
                KERNEL_ADDR(allocation_match->address.kernel_addr.virtual_addr + backing_offset + source_offset,
                            allocation_match->address.kernel_addr.phys_addr + backing_offset + source_offset);
            mapping_kernel_address.valid = allocation_match->address.kernel_addr.valid;
            UserAddress mapping =
                UserAddress__new(target_base + source_offset, mapping_kernel_address, true, true, false, true);

            KernelAddress allocation_kernel_address =
                KERNEL_ADDR(allocation_match->address.kernel_addr.virtual_addr + backing_offset,
                            allocation_match->address.kernel_addr.phys_addr + backing_offset);
            allocation_kernel_address.valid = allocation_match->address.kernel_addr.valid;
            UserAddress allocation = UserAddress__new(target_base, allocation_kernel_address, true, true, false, true);

            PageTable__map_shared(&self->page_table, self->kernel, allocation, allocation_size);
            match_result(
                Process__record_user_allocation(self, allocation, allocation_size, USER_ALLOCATION_SHARED),
                success(_)
                {
                    self->allocation_next += allocation_size;
                    return Success(SharedUserPages, SharedUserPages__new(allocation, mapping));
                },
                error(code, message)
                {
                    return Error(SharedUserPages, message, code);
                }
            );
        },
        none_do_nothing()
    );

    return Error(SharedUserPages, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
}

Result(UserAddress)
    Process__map_shared_user_pages(ProcessPtr self, ProcessPtr source, Address source_virtual_address, size_t size)
{
    match_result(
        Process__map_shared_user_pages_with_allocation(self, source, source_virtual_address, size),
        success(shared)
        {
            return Success(UserAddress, shared.mapping);
        },
        error(code, message)
        {
            return Error(UserAddress, message, code);
        }
    );

    return Error(UserAddress, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
}

Result(UserAddress) Process__map_idle_debug_page(ProcessPtr self)
{
    size_t allocation_size = self->kernel->heap.coarse_block_size;
    UserAddress allocation = Kernel__alloc_user(IDLE_DEBUG_PAGE_VADDR, allocation_size, true, true, false);
    IdleDebugStatePtr state = k_v_ptr(allocation, IdleDebugState);
    state->magic = IDLE_DEBUG_MAGIC;
    state->before_wfi_count = 0;
    state->after_wfi_count = 0;

    PageTable__map(&self->page_table, self->kernel, allocation, allocation_size);

    match_result(
        Process__record_user_allocation(self, allocation, allocation_size, USER_ALLOCATION_DYNAMIC),
        success_do_nothing(),
        error(code, message)
        {
            return Error(UserAddress, message, code);
        }
    );

    self->idle_debug_address = Some(UserAddress, allocation);
    return Success(UserAddress, allocation);
}

Result(UserAddress) Process__map_mmio(ProcessPtr self, Address physical, size_t size)
{
    Address physical_base = Process__page_base(physical);
    Address physical_offset = physical - physical_base;
    size_t allocation_size = Process__page_aligned_size(size + physical_offset);
    if (size == 0 || size + physical_offset < size || allocation_size < size ||
        self->allocation_next >= HUND_SERVICE_DIRECTORY_VADDR ||
        allocation_size > HUND_SERVICE_DIRECTORY_VADDR - self->allocation_next)
    {
        return Error(UserAddress, user_allocation_full, PROCESS_MEMORY_INVALID_SIZE);
    }
    Address virtual_base = self->allocation_next;
    Address virtual_address = virtual_base + physical_offset;

    UserAddress mapping = UserAddress__new(virtual_address, KERNEL_ADDR(physical, physical), true, true, false, true);
    UserAddress allocation =
        UserAddress__new(virtual_base, KERNEL_ADDR(physical_base, physical_base), true, true, false, true);

    PageTable__map_mmio(&self->page_table, self->kernel, mapping, size);
    match_result(
        Process__record_user_allocation(self, allocation, allocation_size, USER_ALLOCATION_MMIO),
        success(_)
        {
            self->allocation_next += allocation_size;
            return Success(UserAddress, mapping);
        },
        error(code, message)
        {
            return Error(UserAddress, message, code);
        }
    );

    return Error(UserAddress, user_allocation_full, PROCESS_MEMORY_FULL);
}

Result(Address) Process__sbrk(ProcessPtr self, int64_t increment)
{
    Address old_break = self->heap_break;
    Address new_break;

    if (increment < 0)
    {
        uint64_t decrement = (uint64_t)-increment;
        if (decrement > old_break - USER_HEAP_BASE)
        {
            return Error(Address, invalid_heap_break, PROCESS_MEMORY_INVALID_SIZE);
        }
        new_break = old_break - decrement;
    }
    else
    {
        uint64_t grow = (uint64_t)increment;
        new_break = old_break + grow;
        if (new_break < old_break)
        {
            return Error(Address, invalid_heap_break, PROCESS_MEMORY_INVALID_SIZE);
        }
    }

    if (new_break > USER_ALLOC_BASE)
    {
        return Error(Address, invalid_heap_break, PROCESS_MEMORY_INVALID_SIZE);
    }

    if (new_break > self->heap_mapped_end)
    {
        Address mapped_end = Process__page_aligned_size(new_break);
        size_t allocation_size = mapped_end - self->heap_mapped_end;
        match_result(
            Kernel__try_alloc_user(self->heap_mapped_end, allocation_size, true, true, false),
            success(allocation)
            {
                PageTable__map(&self->page_table, self->kernel, allocation, allocation_size);
                match_result(
                    Process__record_user_allocation(self, allocation, allocation_size, USER_ALLOCATION_HEAP),
                    success(_)
                    {
                        self->heap_mapped_end = mapped_end;
                    },
                    error(code, message)
                    {
                        PageTable__unmap(&self->page_table, self->kernel, allocation.virtual_addr, allocation_size);
                        Kernel__free(allocation.kernel_addr.virtual_addr);
                        return Error(Address, message, code);
                    }
                );
            },
            error(code, message)
            {
                return Error(Address, message, PROCESS_MEMORY_FULL);
            }
        );
    }

    self->heap_break = new_break;
    return Success(Address, old_break);
}

static Result(ProcessMemoryCode) Process__free_user_allocation_at(ProcessPtr self, ListIndex index)
{
    match_optional(
        UserAllocations__at(&self->user_allocations, index),
        some(allocation)
        {
            match_optional(
                self->virtio_device,
                some(device)
                {
                    if (allocation.type == USER_ALLOCATION_MMIO &&
                        device.virtual_addr >= allocation.address.virtual_addr &&
                        device.virtual_addr - allocation.address.virtual_addr < allocation.size)
                    {
                        return Error(ProcessMemoryCode, user_allocation_full, PROCESS_MEMORY_FREE_FAILED);
                    }
                },
                none_do_nothing()
            );
            __d_printf(MEMORY, "process free pid=%i name=%s type=%s user=%p kernel=%p phys=%p size=%i tracked=%i\n",
                       self->pid, cstr(self->name), UserAllocationType__name(allocation.type),
                       allocation.address.virtual_addr, allocation.address.kernel_addr.virtual_addr,
                       allocation.address.kernel_addr.phys_addr, allocation.size,
                       UserAllocations__len(&self->user_allocations));
            PageTable__unmap(&self->page_table, self->kernel, allocation.address.virtual_addr, allocation.size);
            Address kernel_virtual = allocation.address.kernel_addr.virtual_addr;

            UserAllocations__remove_at(&self->user_allocations, index);

            if (allocation.type == USER_ALLOCATION_MMIO || allocation.type == USER_ALLOCATION_SHARED)
            {
                return Success(ProcessMemoryCode, PROCESS_MEMORY_OK);
            }

            match_result(
                Kernel__free(kernel_virtual),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(ProcessMemoryCode, message, PROCESS_MEMORY_FREE_FAILED);
                }
            );

            return Success(ProcessMemoryCode, PROCESS_MEMORY_OK);
        },
        none_do_nothing()
    );

    return Error(ProcessMemoryCode, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
}

Result(ProcessMemoryCode) Process__free_user_pages(ProcessPtr self, Address virtual_address)
{
    match_optional(
        self->queue_wait_control,
        some(mapping)
        {
            if (mapping.virtual_addr == virtual_address)
            {
                return Error(ProcessMemoryCode, static_user_allocation, PROCESS_MEMORY_STATIC_ALLOCATION);
            }
        },
        none_do_nothing()
    );
    match_optional(
        UserAllocations__find(&self->user_allocations, allocation_matches_virtual, &virtual_address),
        some(found)
        {
            if (found.value->type == USER_ALLOCATION_ELF || found.value->type == USER_ALLOCATION_STACK ||
                found.value->type == USER_ALLOCATION_HEAP)
            {
                return Error(ProcessMemoryCode, static_user_allocation, PROCESS_MEMORY_STATIC_ALLOCATION);
            }

            return Process__free_user_allocation_at(self, found.index);
        },
        none()
        {
            return Error(ProcessMemoryCode, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
        }
    );

    return Error(ProcessMemoryCode, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
}

Result(ProcessMemoryCode) Process__dispose_user_allocations(ProcessPtr self)
{
    self->queue_wait_control = None(UserAddress);
    Process__quiesce_device(self);
    while (self->user_allocations.len > 0)
    {
        match_result(
            Process__free_user_allocation_at(self, 0),
            success_do_nothing(),
            error(code, message)
            {
                return Error(ProcessMemoryCode, message, code);
            }
        );
    }

    return Success(ProcessMemoryCode, PROCESS_MEMORY_OK);
}

static Result(SpawnFileDescriptor) Process__startup_endpoint(ProcessPtr self, SpawnFileDescriptor descriptor)
{
    if (IOEndpoint__is_terminal(descriptor))
    {
        return Success(SpawnFileDescriptor, descriptor);
    }
    switch (descriptor)
    {
    case 0:
        return Success(SpawnFileDescriptor, SpawnStdio__stdin_fd(self->startup_stdio));
    case 1:
        return Success(SpawnFileDescriptor, SpawnStdio__stdout_fd(self->startup_stdio));
    case 2:
        return Success(SpawnFileDescriptor, SpawnStdio__stderr_fd(self->startup_stdio));
    default:
        return Error(SpawnFileDescriptor, bstr("Userspace handle transfer is not supported"), SPAWN_FAILED);
    }
}

Result(SpawnStdio) Process__stdio_for_spawn(ProcessPtr self, ConstSpawnStdioPtr requested)
{
    if (!SpawnStdio__is_present(*requested))
    {
        return Success(SpawnStdio, self->startup_stdio);
    }
    SpawnFileDescriptor input;
    SpawnFileDescriptor output;
    SpawnFileDescriptor error_output;
    match_result(
        Process__startup_endpoint(self, SpawnStdio__stdin_fd(*requested)),
        success(value)
        {
            input = value;
        },
        error(code, message)
        {
            return Error(SpawnStdio, message, code);
        }
    );
    match_result(
        Process__startup_endpoint(self, SpawnStdio__stdout_fd(*requested)),
        success(value)
        {
            output = value;
        },
        error(code, message)
        {
            return Error(SpawnStdio, message, code);
        }
    );
    match_result(
        Process__startup_endpoint(self, SpawnStdio__stderr_fd(*requested)),
        success(value)
        {
            error_output = value;
        },
        error(code, message)
        {
            return Error(SpawnStdio, message, code);
        }
    );
    return Success(SpawnStdio, SpawnStdio__new(input, output, error_output));
}

void Process__prepare_startup_stdio(ProcessPtr self)
{
    Process__set_startup_stdio(self, SpawnStdio__stdin_fd(self->startup_stdio),
                               SpawnStdio__stdout_fd(self->startup_stdio), SpawnStdio__stderr_fd(self->startup_stdio));
}
