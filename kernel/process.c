#include "process.h"
#include "scheduler/parked.h"
#include "bin/elf.h"
#include "arch/page_table.h"
#include "arch/process.h"
#include "arch/process_context.h"
#include "types/bstring.h"
#include "types/result.h"
#include "types/optional.h"
#include "kernel_core.h"
#include "irq/irq_dispatcher.h"
#include "subsystems/io/block/block_device.h"
#include "../common_includes/syscalls/spawn.h"

#include "kalloc.h"
#include "stack.h"
#include "idle_debug.h"

FixedArrayBodyDefine(ProcessTable);
FixedArrayBodyDefine(ProcessArguments);
FixedArrayBodyDefine(ProcessChildEvents);
LinkedListBodyDefine(UserAllocations);
LinkedListBodyDefine(ProcessFileDescriptors);
LinkedListBodyDefine(ParkedOperations);

static ProcessID pid_counter = 0;
static mutex_t pid_lock = MUTEX_INIT;

static BString no_entry_point_message = bstr("No entry point found in ELF");
static BString user_allocation_not_found = bstr("User allocation not found");
static BString static_user_allocation = bstr("Cannot free static user allocation");
static BString user_allocation_full = bstr("Too many user allocations");
static BString invalid_heap_break = bstr("Invalid heap break");
static BString file_descriptor_full = bstr("Too many file descriptors");
static BString file_descriptor_not_found = bstr("File descriptor not found");
static BString argument_too_long = bstr("Argument too long");
static const ProcessCapabilities PROCESS__HUND_CAPABILITIES =
    PROCESS_CAP_BECOME_SERVICE
    | PROCESS_CAP_MAP_MMIO
    | PROCESS_CAP_SUBSCRIBE_IRQ
    | PROCESS_CAP_REGISTER_BLOCK_DEVICE
    | PROCESS_CAP_HANDLE_BLOCK_REQUESTS
    | PROCESS_CAP_MOUNT_FILESYSTEM
    | PROCESS_CAP_USE_BLOCK_DEVICES;

static ProcessSignalMask ProcessSignal__mask(ProcessSignal signal)
{
    return (ProcessSignalMask) 1ULL << signal;
}

static size_t Process__page_aligned_size(size_t size)
{
    return (size + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
}

static Result(FileDescriptor) Process__set_file_descriptor_unlocked(
    ProcessPtr self, FileDescriptor fd, ProcessIOHandle handle
);
static Result(FileDescriptor) Process__allocate_file_descriptor_unlocked(ProcessPtr self, ProcessIOHandle handle);
static Result(ProcessIOHandle) Process__get_io_handle_unlocked(ProcessPtr self, FileDescriptor fd);
static Result(ProcessIOHandle) Process__remove_file_descriptor_unlocked(ProcessPtr self, FileDescriptor fd);

static Address Process__page_base(Address address)
{
    return address & ~(PAGE_SIZE - 1);
}

bool UserAllocation__equals(UserAllocationPtr self, UserAllocationPtr other)
{
    return self->address.virtual_addr == other->address.virtual_addr;
}

bool ProcessFileDescriptor__equals(ProcessFileDescriptorPtr self, ProcessFileDescriptorPtr other)
{
    return self->fd == other->fd;
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
    Address virtual_address = *(Address *) context;
    return allocation->address.virtual_addr == virtual_address;
}

typedef struct UserAllocationRangeMatch
{
    Address virtual_address;
    size_t size;
}
UserAllocationRangeMatch;

typedef UserAllocationRangeMatch * UserAllocationRangeMatchPtr;

static UserAllocationRangeMatch UserAllocationRangeMatch__new(Address virtual_address, size_t size)
{
    return (UserAllocationRangeMatch) {
        .virtual_address = virtual_address,
        .size = size,
    };
}

static UserAllocation UserAllocation__new(UserAddress address, size_t size, UserAllocationType type)
{
    return (UserAllocation) {
        .address = address,
        .size = size,
        .type = type,
    };
}

static ProcessFileDescriptor ProcessFileDescriptor__new(FileDescriptor fd, ProcessIOHandle handle)
{
    return (ProcessFileDescriptor) {
        .fd = fd,
        .handle = handle,
    };
}

static bool allocation_contains_range(UserAllocationPtr allocation, void * context)
{
    UserAllocationRangeMatchPtr range = (UserAllocationRangeMatchPtr) context;
    Address allocation_start = allocation->address.virtual_addr;
    Address allocation_end = allocation_start + allocation->size;
    Address range_end = range->virtual_address + range->size;

    return range->virtual_address >= allocation_start
        && range_end <= allocation_end
        && range_end >= range->virtual_address;
}

static bool file_descriptor_matches_fd(ProcessFileDescriptorPtr descriptor, void * context)
{
    FileDescriptor fd = *(FileDescriptor *) context;
    return descriptor->fd == fd;
}

static ProcessFileDescriptors Process__inherit_file_descriptors(Optional(ProcessPtr) parent)
{
    ProcessFileDescriptors descriptors = ProcessFileDescriptors__new();
    match_optional(ProcessPtr, parent,
        some(parent_process)
        {
            for_each_ll(ProcessFileDescriptors, parent_process->file_descriptors, descriptor,
            {
                ProcessFileDescriptors__push(&descriptors, descriptor);
            });
            return descriptors;
        },
        none_do_nothing()
    );

    return descriptors;
}

static FileDescriptor Process__inherit_next_file_descriptor(Optional(ProcessPtr) parent)
{
    match_optional(ProcessPtr, parent,
        some(parent_process) { return parent_process->next_file_descriptor; },
        none_do_nothing()
    );

    return 3;
}

static size_t Process__copy_arg(DataPtr target, BStringPtr arg)
{
    StringPos length = BString__length(arg);
    for (StringPos i = 0; i < length; i++)
    {
        target[i] = (Data) BString__char_at_or(arg, i, '\0');
    }
    target[length] = '\0';
    return (size_t) length + 1;
}

static Result(Address) Process__setup_args(
    ProcessPtr self,
    ProcessArguments arguments,
    uint64_t * argc
)
{
    match_result(UserAddress, Process__alloc_user_pages(self, PAGE_SIZE),
        success(args_page)
        {
            Address user_base = args_page.virtual_addr;
            Address kernel_base = args_page.kernel_addr.virtual_addr;
            Address * argv = ptr(kernel_base, Address);

            *argc = arguments.len;

            size_t cursor = sizeof(Address) * (*argc + 1);
            for_each_fa(ProcessArguments, arguments, i, arg,
            {
                if (cursor + (size_t) BString__length(&arg) + 1 > PAGE_SIZE)
                {
                    return Error(Address, argument_too_long, SPAWN_ARG_TOO_LONG);
                }
                argv[i] = user_base + cursor;
                cursor += Process__copy_arg(ptr(kernel_base + cursor, Data), &arg);

            });
            argv[*argc] = 0;
            return Success(Address, user_base);
        },
        error(code, message) { return Error(Address, message, code); }
    );

    return Error(Address, argument_too_long, SPAWN_ARG_TOO_LONG);
}

Result(ProcessPtr) Process__new(
    KernelPtr kernel,
    BString name,
    KernelAddress page_table,
    UserAddress entry,
    Stack stack,
    Optional(ProcessPtr) parent,
    ProcessArguments arguments
)
{
    ProcessID pid;

    critical_section(pid_lock, pid = ++pid_counter);

    __d_printf(PROCESS, "Creating process with entry point %p\n", entry);
    ProcessPtr proc = ptr(Kernel__alloc_kernel(sizeof(Process)), Process);
    proc->lock = MUTEX_INIT;
    proc->kernel = kernel;
    proc->entry_point = entry;
    proc->page_table = page_table;
    proc->state = PROC_STATE_NEW;
    proc->pid = pid;
    proc->stack = stack;
    proc->last_tick = 0;
    proc->name = name;
    proc->parked_operations = ParkedOperations__new();
    proc->parent = parent;
    proc->heap_break = USER_HEAP_BASE;
    proc->heap_mapped_end = USER_HEAP_BASE;
    proc->allocation_next = USER_ALLOC_BASE;
    proc->user_allocations = UserAllocations__new();
    proc->file_descriptors = Process__inherit_file_descriptors(parent);
    proc->next_file_descriptor = Process__inherit_next_file_descriptor(parent);
    proc->abort = None(ProcessAbort);
    proc->process_class = PROCESS_CLASS_NORMAL;
    proc->service = ServiceState__new();
    proc->idle_debug_address = None(UserAddress);
    proc->user_id = PROCESS_USER_ROOT;
    proc->group_id = PROCESS_GROUP_ROOT;
    proc->capabilities = PROCESS_CAP_ALL;
    proc->pending_signals = 0;
    proc->child_events = ProcessChildEvents__new();

    match_optional(ProcessPtr, parent,
        some(parent_process) { Process__set_identity(proc, parent_process->user_id, parent_process->group_id, parent_process->capabilities); },
        none() { Process__set_root_identity(proc); }
    );

    uint64_t argc;
    match_result(Address, Process__setup_args(proc, arguments, &argc),
        success(argv)
        {
            __d_printf(
                PROCESS,
                "Process__new before context init pid=%i proc=%p stack_ptr=%p stack_base=%p stack_top=%p argc=%i argv=%p entry=%p page_table=%p\n",
                proc->pid,
                proc,
                &proc->stack,
                proc->stack.base.virtual_addr,
                proc->stack.top.virtual_addr,
                argc,
                argv,
                entry.virtual_addr,
                page_table.phys_addr
            );
            proc->context = ProcessContext__init(kernel, proc->pid, &proc->stack, entry, page_table, argc, argv);
            __d_printf(
                PROCESS,
                "Process__new after context init pid=%i proc=%p context=%p\n",
                proc->pid,
                proc,
                proc->context
            );
            return Success(ProcessPtr, proc);
        },
        error(code, message) { return Error(ProcessPtr, message, code); }
    );

    return Error(ProcessPtr, argument_too_long, SPAWN_ARG_TOO_LONG);
}

Result(CollectionCode) Process__push_parked_operation(ProcessPtr self, ParkedOperationPtr operation)
{
    critical_section(self->lock,
    {
        match_result(CollectionCode, ParkedOperations__push(&self->parked_operations, operation),
            success(index) { cs_return Success(CollectionCode, index); },
            error(code, message) { cs_return Error(CollectionCode, message, code); }
        );
    });

    return Error(CollectionCode, bstr("Unable to push parked operation"), LINKED_LIST_ALLOCATION_FAILED);
}

Optional(ParkedOperationPtr) Process__pop_parked_operation(ProcessPtr self)
{
    critical_section(self->lock,
    {
        match_optional(ParkedOperationPtr, ParkedOperations__remove_at(&self->parked_operations, -1),
            some(parked_operation) { cs_return Some(ParkedOperationPtr, parked_operation); },
            none_do_nothing()
        );
    });

    return None(ParkedOperationPtr);
}

Optional(ParkedOperationPtr) Process__pop_ready_parked_operation(ProcessPtr self)
{
    critical_section(self->lock,
    {
        for (int64_t index = (int64_t) ParkedOperations__len(&self->parked_operations) - 1; index >= 0; index--)
        {
            match_optional(ParkedOperationPtr, ParkedOperations__at(&self->parked_operations, index),
                some(parked_operation)
                {
                    if (ParkedOperation__is_ready(parked_operation))
                    {
                        match_optional(ParkedOperationPtr, ParkedOperations__remove_at(&self->parked_operations, index),
                            some(removed) { cs_return Some(ParkedOperationPtr, removed); },
                            none_do_nothing()
                        );
                    }
                },
                none_do_nothing()
            );
        }
    });

    return None(ParkedOperationPtr);
}

Optional(ParkReason) Process__top_park_reason(ProcessPtr self)
{
    critical_section(self->lock,
    {
        match_optional(ParkedOperationPtr, ParkedOperations__at(&self->parked_operations, -1),
            some(parked_operation) { cs_return Some(ParkReason, parked_operation->park_reason); },
            none_do_nothing()
        );
    });

    return None(ParkReason);
}

Optional(SchedulerPtr) Process__top_parked_operation_scheduler(ProcessPtr self)
{
    critical_section(self->lock,
    {
        match_optional(ParkedOperationPtr, ParkedOperations__at(&self->parked_operations, -1),
            some(parked_operation)
            {
                match_optional(SchedulerPtr, parked_operation->kernel_context->scheduler,
                    some(scheduler) { cs_return Some(SchedulerPtr, scheduler); },
                    none_do_nothing()
                );
            },
            none_do_nothing()
        );
    });

    return None(SchedulerPtr);
}

bool Process__has_parked_operation(ProcessPtr self, ParkReason reason, Optional(OperationKey) operation_key)
{
    critical_section(self->lock,
    {
        for (int64_t index = (int64_t) ParkedOperations__len(&self->parked_operations) - 1; index >= 0; index--)
        {
            match_optional(ParkedOperationPtr, ParkedOperations__at(&self->parked_operations, index),
                some(parked_operation)
                {
                    if (parked_operation->park_reason != reason)
                    {
                        continue;
                    }

                    match_optional(OperationKey, operation_key,
                        some(key)
                        {
                            match_optional(OperationKey, parked_operation->operation_key,
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
                        none() { cs_return true; }
                    );
                },
                none_do_nothing()
            );
        }
    });

    return false;
}

bool Process__mark_parked_operation_ready(
    ProcessPtr self, ParkReason reason, Optional(OperationKey) operation_key, Optional(ReturnCode) return_code
)
{
    critical_section(self->lock,
    {
        for (int64_t index = (int64_t) ParkedOperations__len(&self->parked_operations) - 1; index >= 0; index--)
        {
            match_optional(ParkedOperationPtr, ParkedOperations__at(&self->parked_operations, index),
                some(parked_operation)
                {
                    if (parked_operation->park_reason != reason)
                    {
                        continue;
                    }

                    match_optional(OperationKey, operation_key,
                        some(key)
                        {
                            match_optional(OperationKey, parked_operation->operation_key,
                                some(parked_key)
                                {
                                    if (parked_key == key)
                                    {
                                        parked_operation->return_code = return_code;
                                        ParkedOperation__mark_ready(parked_operation);
                                        cs_return true;
                                    }
                                },
                                none_do_nothing()
                            );
                        },
                        none()
                        {
                            parked_operation->return_code = return_code;
                            ParkedOperation__mark_ready(parked_operation);
                            cs_return true;
                        }
                    );
                },
                none_do_nothing()
            );
        }
    });

    return false;
}

bool Process__wake_expired_sleep(ProcessPtr self, Tick current_tick)
{
    critical_section(self->lock,
    {
        if (self->state != PROC_STATE_SLEEPING)
        {
            cs_return false;
        }

        for (int64_t index = (int64_t) ParkedOperations__len(&self->parked_operations) - 1; index >= 0; index--)
        {
            match_optional(ParkedOperationPtr, ParkedOperations__at(&self->parked_operations, index),
                some(parked_operation)
                {
                    if (parked_operation->park_reason != PARK_SLEEP)
                    {
                        continue;
                    }

                    match_optional(OperationKey, parked_operation->operation_key,
                        some(deadline)
                        {
                            if (current_tick >= deadline)
                            {
                                parked_operation->return_code = Some(ReturnCode, PROCESS_SLEEP_OK);
                                ParkedOperation__mark_ready(parked_operation);
                                self->state = PROC_STATE_INTERRUPTED;
                                cs_return true;
                            }
                        },
                        none_do_nothing()
                    );
                },
                none_do_nothing()
            );
        }
    });

    return false;
}

bool Process__is_parked(ProcessPtr self)
{
    critical_section(self->lock,
    {
        cs_return ParkedOperations__len(&self->parked_operations) > 0;
    });

    return false;
}

bool Process__is_handling_service_irq(ProcessPtr self)
{
    match_optional(ServiceEvent, self->service.active_irq_event,
        some(event) { return true; },
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
    self->pending_signals |= ProcessSignal__mask(signal);
}

bool Process__has_pending_signal(ProcessPtr self)
{
    return self->pending_signals != 0;
}

Optional(ProcessSignal) Process__take_pending_signal(ProcessPtr self)
{
    ProcessSignal ordered[] = {
        PROCESS_SIGNAL_KILL,
        PROCESS_SIGNAL_TERMINATE,
        PROCESS_SIGNAL_INTERRUPT,
    };

    for (size_t i = 0; i < sizeof(ordered) / sizeof(ordered[0]); i++)
    {
        ProcessSignal signal = ordered[i];
        ProcessSignalMask mask = ProcessSignal__mask(signal);
        if ((self->pending_signals & mask) != 0)
        {
            self->pending_signals &= ~mask;
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

    match_result(BlockDeviceCode, BlockDeviceRegistry__dispose_process_resources(self),
        success_do_nothing(),
        error(code, message) { return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP); }
    );

    match_result(IRQRegistrationStatus, IRQDispatcher__dispose_process_resources(self->kernel->irq_dispatcher, self),
        success_do_nothing(),
        error(code, message) { return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP); }
    );

    return Success(ProcessSetupStatus, PROCESS_SETUP);
}

Result(ProcessChildEvent) Process__pop_next_child_event(ProcessPtr self)
{
    match_optional(ProcessChildEvent, ProcessChildEvents__pop_front(&self->child_events),
        some(event) { return Success(ProcessChildEvent, event); },
        none_do_nothing()
    );

    return Error(ProcessChildEvent, bstring("Child event queue empty"), PROCESS_CHILD_EVENT_QUEUE_EMPTY);
}

Result(CollectionCode) Process__push_child_event(ProcessPtr self, ProcessChildEvent event)
{
    return ProcessChildEvents__push(&self->child_events, event);
}

Result(ProcessPtr) Process__setup(KernelPtr kernel, Elf binary, Optional(ProcessPtr) parent)
{
    return Process__setup_with_args(kernel, binary, parent, ProcessArguments__new());
}

Result(ProcessPtr) Process__setup_with_args(KernelPtr kernel, Elf binary, Optional(ProcessPtr) parent, ProcessArguments arguments)
{
    __d_println(PROCESS, "Process__spawn_from_elf");
    ExpandedSegmentArray expanded_elf = Elf__map(binary);
    __d_printf(PROCESS, "ELF: %s %x %x\n", cstr(binary.name), binary.start, binary.end);
    __d_printf(PROCESS, "ELF mapped %i\n", ExpandedSegmentArray__len(&expanded_elf));

    __d_println(PROCESS, "Creating page table");
    KernelAddress page_table = PageTable__new(kernel);

    __d_println(PROCESS, "Page table created");

    for_each_fa(ExpandedSegmentArray, expanded_elf, i, segment,
    {
        UserAddress addr = segment.address;
        __d_printf(MMU, "Mapping segment %i phys=%p, virt=%p\n", i, addr.kernel_addr.phys_addr, addr.virtual_addr);
        PageTable__map(v_ptr(page_table, PageTable), kernel, segment.address, segment.size);
    });
    __d_println(PROCESS, "Code mapped");

    __d_printf(PROCESS, "Creating stack for %s\n", cstr(binary.name));
    Stack stack = Stack__new();
    __d_printf(
        PROCESS,
        "Stack created for %s base=%p top=%p kbase=%p phys=%p\n",
        cstr(binary.name),
        stack.base.virtual_addr,
        stack.top.virtual_addr,
        stack.base.kernel_addr.virtual_addr,
        stack.base.kernel_addr.phys_addr
    );
    __d_printf(PROCESS, "Mapping stack for %s page_table=%p\n", cstr(binary.name), page_table.phys_addr);
    Stack__map(kernel, &stack, v_ptr(page_table, PageTable));
    __d_println(PROCESS, "Stack mapped");

    match_optional(ExpandedSegment, Elf__entry_segment(binary, &expanded_elf),
        some(entry_point)
        {
            Offset entry_offset = Elf__read_header(binary)->entry - entry_point.address.virtual_addr;
            UserAddress entry_address = UserAddress__relative(&entry_point.address, entry_offset);
            ProcessArguments process_arguments = ProcessArguments__new();
            match_result(CollectionCode, ProcessArguments__push(&process_arguments, binary.name),
                success_do_nothing(),
                error(code, message) { return Error(ProcessPtr, message, SPAWN_ARG_TOO_LONG); }
            );
            match_result(CollectionCode, ProcessArguments__push_all(&process_arguments, &arguments),
                success_do_nothing(),
                error(code, message) { return Error(ProcessPtr, argument_too_long, SPAWN_ARG_TOO_LONG); }
            );

            match_result(ProcessPtr, Process__new(kernel, binary.name, page_table, entry_address, stack, parent, process_arguments),
                success(proc)
                {
                    for_each_fa(ExpandedSegmentArray, expanded_elf, segment_index, segment,
                    {
                        Process__record_user_allocation(proc, segment.address, segment.size, USER_ALLOCATION_ELF);
                    });
                    Process__record_user_allocation(proc, stack.base, STACK_SIZE, USER_ALLOCATION_STACK);
                    return Success(ProcessPtr, proc);
                },
                error(code, message) { return Error(ProcessPtr, message, code); }
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

Result(ProcessMemoryCode) Process__record_user_allocation(
    ProcessPtr self, UserAddress address, size_t size, UserAllocationType type
)
{
    UserAllocation allocation = UserAllocation__new(address, Process__page_aligned_size(size), type);

    match_result(CollectionCode, UserAllocations__push(&self->user_allocations, allocation),
        success(_)
        {
        return Success(ProcessMemoryCode, PROCESS_MEMORY_OK);
    },
        error(code, message)
        {
        return Error(ProcessMemoryCode, user_allocation_full, PROCESS_MEMORY_FULL);
    }
    );

    return Error(ProcessMemoryCode, user_allocation_full, PROCESS_MEMORY_FULL);
}

Result(Address) Process__device_address(ProcessPtr self, Address virtual_address)
{
    if (self == NULL || virtual_address == 0) // hundelint: ignore-no-null
    {
        return Error(Address, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
    }

    for_each_ll(UserAllocations, self->user_allocations, allocation,
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
                self->kernel, allocation.address.kernel_addr.virtual_addr + offset
            );
            if (!address.valid)
            {
                return Error(Address, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
            }

            return Success(Address, address.phys_addr);
        }
    });
    return Error(Address, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
}

Result(UserAddress) Process__alloc_user_pages(ProcessPtr self, size_t size)
{
    size_t allocation_size = Process__page_aligned_size(size);
    Address virtual_address = self->allocation_next;

    UserAddress allocation = Kernel__alloc_user(virtual_address, allocation_size, true, true, false);
    PageTable__map(v_ptr(self->page_table, PageTable), self->kernel, allocation, allocation_size);

    match_result(ProcessMemoryCode, Process__record_user_allocation(
        self, allocation, allocation_size, USER_ALLOCATION_DYNAMIC
    ),
        success(_)
        {
            self->allocation_next += allocation_size;
            return Success(UserAddress, allocation);
        },
        error(code, message)
        {
            return Error(UserAddress, message, code);
        });

    return Error(UserAddress, user_allocation_full, PROCESS_MEMORY_FULL);
}

Result(UserAddress) Process__map_shared_user_pages(
    ProcessPtr self, ProcessPtr source, Address source_virtual_address, size_t size
)
{
    if (self == NULL || source == NULL || source_virtual_address == 0 || size == 0) // hundelint: ignore-no-null
    {
        return Error(UserAddress, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
    }

    Address source_base = Process__page_base(source_virtual_address);
    Offset source_offset = source_virtual_address - source_base;
    size_t allocation_size = Process__page_aligned_size(source_offset + size);
    UserAllocationRangeMatch range = UserAllocationRangeMatch__new(source_base, allocation_size);

    match_optional(UserAllocations__FindResult, UserAllocations__find(
        &source->user_allocations, allocation_contains_range, &range
    ),
        some(found)
        {
            Offset backing_offset = source_base - found.value->address.virtual_addr;
            Address target_base = self->allocation_next;
            KernelAddress mapping_kernel_address = KERNEL_ADDR(
                found.value->address.kernel_addr.virtual_addr + backing_offset + source_offset,
                found.value->address.kernel_addr.phys_addr + backing_offset + source_offset
            );
            mapping_kernel_address.valid = found.value->address.kernel_addr.valid;
            UserAddress mapping = UserAddress__new(
                target_base + source_offset,
                mapping_kernel_address,
                true,
                true,
                false,
                true
            );

            KernelAddress allocation_kernel_address = KERNEL_ADDR(
                found.value->address.kernel_addr.virtual_addr + backing_offset,
                found.value->address.kernel_addr.phys_addr + backing_offset
            );
            allocation_kernel_address.valid = found.value->address.kernel_addr.valid;
            UserAddress allocation = UserAddress__new(
                target_base,
                allocation_kernel_address,
                true,
                true,
                false,
                true
            );

            PageTable__map(v_ptr(self->page_table, PageTable), self->kernel, allocation, allocation_size);
            match_result(ProcessMemoryCode, Process__record_user_allocation(
                self, allocation, allocation_size, USER_ALLOCATION_SHARED
            ),
                success(_)
                {
                    self->allocation_next += allocation_size;
                    return Success(UserAddress, mapping);
                },
                error(code, message) { return Error(UserAddress, message, code); }
            );
        },
        none_do_nothing()
    );

    return Error(UserAddress, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
}

Result(UserAddress) Process__map_idle_debug_page(ProcessPtr self)
{
    UserAddress allocation = Kernel__alloc_user(
        IDLE_DEBUG_PAGE_VADDR, PAGE_SIZE, true, true, false
    );
    IdleDebugStatePtr state = k_v_ptr(allocation, IdleDebugState);
    state->magic = IDLE_DEBUG_MAGIC;
    state->before_wfi_count = 0;
    state->after_wfi_count = 0;

    PageTable__map(v_ptr(self->page_table, PageTable), self->kernel, allocation, PAGE_SIZE);

    match_result(ProcessMemoryCode, Process__record_user_allocation(
        self, allocation, PAGE_SIZE, USER_ALLOCATION_DYNAMIC
    ),
        success_do_nothing(),
        error(code, message) { return Error(UserAddress, message, code); }
    );

    self->idle_debug_address = Some(UserAddress, allocation);
    return Success(UserAddress, allocation);
}

Result(UserAddress) Process__map_mmio(ProcessPtr self, Address physical, size_t size)
{
    Address physical_base = Process__page_base(physical);
    Address physical_offset = physical - physical_base;
    size_t allocation_size = Process__page_aligned_size(size + physical_offset);
    Address virtual_base = self->allocation_next;
    Address virtual_address = virtual_base + physical_offset;

    UserAddress mapping = UserAddress__new(
        virtual_address,
        KERNEL_ADDR(physical, physical),
        true,
        true,
        false,
        true
    );
    UserAddress allocation = UserAddress__new(
        virtual_base,
        KERNEL_ADDR(physical_base, physical_base),
        true,
        true,
        false,
        true
    );

    PageTable__map_mmio(v_ptr(self->page_table, PageTable), self->kernel, mapping, size);
    match_result(ProcessMemoryCode, Process__record_user_allocation(
        self, allocation, allocation_size, USER_ALLOCATION_MMIO
    ),
        success(_)
        {
            self->allocation_next += allocation_size;
            return Success(UserAddress, mapping);
        },
        error(code, message)
        {
            return Error(UserAddress, message, code);
        });

    return Error(UserAddress, user_allocation_full, PROCESS_MEMORY_FULL);
}

Result(Address) Process__sbrk(ProcessPtr self, int64_t increment)
{
    Address old_break = self->heap_break;
    Address new_break;

    if (increment < 0)
    {
        uint64_t decrement = (uint64_t) -increment;
        if (decrement > old_break - USER_HEAP_BASE)
        {
            return Error(Address, invalid_heap_break, PROCESS_MEMORY_INVALID_SIZE);
        }
        new_break = old_break - decrement;
    }
    else
    {
        uint64_t grow = (uint64_t) increment;
        new_break = old_break + grow;
        if (new_break < old_break)
        {
            return Error(Address, invalid_heap_break, PROCESS_MEMORY_INVALID_SIZE);
        }
    }

    if (new_break > self->heap_mapped_end)
    {
        Address mapped_end = Process__page_aligned_size(new_break);
        size_t allocation_size = mapped_end - self->heap_mapped_end;
        UserAddress allocation = Kernel__alloc_user(
            self->heap_mapped_end, allocation_size, true, true, false
        );

        PageTable__map(v_ptr(self->page_table, PageTable), self->kernel, allocation, allocation_size);
        match_result(ProcessMemoryCode, Process__record_user_allocation(
            self, allocation, allocation_size, USER_ALLOCATION_HEAP
        ),
            success(_)
            {
                self->heap_mapped_end = mapped_end;
            },
            error(code, message)
            {
                return Error(Address, message, code);
            });
    }

    self->heap_break = new_break;
    return Success(Address, old_break);
}

static Result(ProcessMemoryCode) Process__free_user_allocation_at(ProcessPtr self, ListIndex index)
{
    match_optional(UserAllocation, UserAllocations__at(&self->user_allocations, index),
        some(allocation)
        {
            PageTable__unmap(v_ptr(self->page_table, PageTable), self->kernel, allocation.address.virtual_addr, allocation.size);
            Address kernel_virtual = allocation.address.kernel_addr.virtual_addr;

            UserAllocations__remove_at(&self->user_allocations, index);

            if (allocation.type == USER_ALLOCATION_MMIO || allocation.type == USER_ALLOCATION_SHARED)
            {
                return Success(ProcessMemoryCode, PROCESS_MEMORY_OK);
            }

            match_result(KAllocCode, Kernel__free(kernel_virtual),
                success_do_nothing(),
                error(code, message) { return Error(ProcessMemoryCode, message, PROCESS_MEMORY_FREE_FAILED); }
            );

            return Success(ProcessMemoryCode, PROCESS_MEMORY_OK);
        },
        none_do_nothing()
    );

    return Error(ProcessMemoryCode, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
}

Result(ProcessMemoryCode) Process__free_user_pages(ProcessPtr self, Address virtual_address)
{
    match_optional(UserAllocations__FindResult, UserAllocations__find(
        &self->user_allocations, allocation_matches_virtual, &virtual_address
    ),
        some(found)
        {
            if (
                found.value->type == USER_ALLOCATION_ELF
                || found.value->type == USER_ALLOCATION_STACK
                || found.value->type == USER_ALLOCATION_HEAP
            )
            {
                return Error(ProcessMemoryCode, static_user_allocation, PROCESS_MEMORY_STATIC_ALLOCATION);
            }

            return Process__free_user_allocation_at(self, found.index);
        }, none() { return Error(ProcessMemoryCode, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND); });

    return Error(ProcessMemoryCode, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
}

Result(ProcessMemoryCode) Process__dispose_user_allocations(ProcessPtr self)
{
    while (self->user_allocations.len > 0)
    {
        match_result(ProcessMemoryCode, Process__free_user_allocation_at(self, 0),
            success_do_nothing(),
            error(code, message) { return Error(ProcessMemoryCode, message, code); }
        );
    }

    return Success(ProcessMemoryCode, PROCESS_MEMORY_OK);
}

Result(FileDescriptor) Process__set_file_descriptor(ProcessPtr self, FileDescriptor fd, ProcessIOHandle handle)
{
    critical_section(self->lock,
        cs_return Process__set_file_descriptor_unlocked(self, fd, handle);
    );

    return Error(FileDescriptor, file_descriptor_full, IO_REGISTER_FAILED);
}

static Result(FileDescriptor) Process__set_file_descriptor_unlocked(
    ProcessPtr self, FileDescriptor fd, ProcessIOHandle handle
)
{
    match_optional(ProcessFileDescriptors__FindResult, ProcessFileDescriptors__find(
        &self->file_descriptors, file_descriptor_matches_fd, &fd
    ),
        some(found)
        {
            found.value->handle = handle;
            if (fd >= self->next_file_descriptor)
            {
                self->next_file_descriptor = fd + 1;
            }
            return Success(FileDescriptor, fd);
        }, none_do_nothing());

    ProcessFileDescriptor descriptor = ProcessFileDescriptor__new(fd, handle);

    match_result(CollectionCode, ProcessFileDescriptors__push(&self->file_descriptors, descriptor),
        success(_)
        {
            if (fd >= self->next_file_descriptor)
            {
                self->next_file_descriptor = fd + 1;
            }
            return Success(FileDescriptor, fd);
        },
        error(code, message)
        {
            return Error(FileDescriptor, file_descriptor_full, IO_REGISTER_FAILED);
        }
    );

    return Error(FileDescriptor, file_descriptor_full, IO_REGISTER_FAILED);
}

Result(FileDescriptor) Process__allocate_file_descriptor(ProcessPtr self, ProcessIOHandle handle)
{
    critical_section(self->lock,
        cs_return Process__allocate_file_descriptor_unlocked(self, handle);
    );

    return Error(FileDescriptor, file_descriptor_full, IO_REGISTER_FAILED);
}

static Result(FileDescriptor) Process__allocate_file_descriptor_unlocked(ProcessPtr self, ProcessIOHandle handle)
{
    FileDescriptor fd = self->next_file_descriptor;
    while (fd < MAX_HANDLES_PER_PROCESS)
    {
        match_optional(ProcessFileDescriptors__FindResult, ProcessFileDescriptors__find(
            &self->file_descriptors, file_descriptor_matches_fd, &fd
        ),
            some_do_nothing(),
            none() { return Process__set_file_descriptor_unlocked(self, fd, handle); }
        );

        fd++;
    }

    return Error(FileDescriptor, file_descriptor_full, IO_REGISTER_FAILED);
}

Result(ProcessIOHandle) Process__get_io_handle(ProcessPtr self, FileDescriptor fd)
{
    critical_section(self->lock,
        cs_return Process__get_io_handle_unlocked(self, fd);
    );

    return Error(ProcessIOHandle, file_descriptor_not_found, IO_NOT_OPEN);
}

static Result(ProcessIOHandle) Process__get_io_handle_unlocked(ProcessPtr self, FileDescriptor fd)
{
    match_optional(ProcessFileDescriptors__FindResult, ProcessFileDescriptors__find(
        &self->file_descriptors, file_descriptor_matches_fd, &fd
    ),
        some(found) { return Success(ProcessIOHandle, found.value->handle); },
        none() { return Error(ProcessIOHandle, file_descriptor_not_found, IO_NOT_OPEN); }
    );

    return Error(ProcessIOHandle, file_descriptor_not_found, IO_NOT_OPEN);
}

Result(FileDescriptor) Process__copy_file_descriptor(
    ProcessPtr self, ProcessPtr source, FileDescriptor target_fd, FileDescriptor source_fd
)
{
    match_result(ProcessIOHandle, Process__get_io_handle(source, source_fd),
        success(handle) { return Process__set_file_descriptor(self, target_fd, handle); },
        error(code, message) { return Error(FileDescriptor, message, code); }
    );

    return Error(FileDescriptor, file_descriptor_not_found, IO_NOT_OPEN);
}

Result(ProcessIOHandle) Process__remove_file_descriptor(ProcessPtr self, FileDescriptor fd)
{
    critical_section(self->lock,
        cs_return Process__remove_file_descriptor_unlocked(self, fd);
    );

    return Error(ProcessIOHandle, file_descriptor_not_found, IO_NOT_OPEN);
}

static Result(ProcessIOHandle) Process__remove_file_descriptor_unlocked(ProcessPtr self, FileDescriptor fd)
{
    match_result(ProcessIOHandle, Process__get_io_handle_unlocked(self, fd),
        success(handle)
        {
            match_optional(ProcessFileDescriptors__FindResult, ProcessFileDescriptors__find(
                &self->file_descriptors, file_descriptor_matches_fd, &fd
            ),
                some(found) { ProcessFileDescriptors__remove_at(&self->file_descriptors, found.index); },
                none_do_nothing()
            );
            if (fd < self->next_file_descriptor)
            {
                self->next_file_descriptor = fd;
            }
            return Success(ProcessIOHandle, handle);
        },
        error(code, message)
        {
            return Error(ProcessIOHandle, message, code);
        }
    );

    return Error(ProcessIOHandle, file_descriptor_not_found, IO_NOT_OPEN);
}
