#include "process.h"
#include "bin/embedded_mapping.h"
#include "scheduler/parked.h"
#include "exec/load.h"
#include "bin/elf.h"
#include "arch/page_table.h"
#include "arch/device.h"
#include "arch/cpu.h"
#include "arch/process.h"
#include "arch/process_context.h"
#include "arch/cache.h"
#include "types/bstring.h"
#include "types/result.h"
#include "types/optional.h"
#include "kernel_core.h"
#include "panic.h"
#include "irq/irq_dispatcher.h"
#include "service/service_directory.h"
#include "service/clock.h"
#include "scheduler/shared_wait.h"
#include "../common_includes/syscalls/spawn.h"

#include "kalloc.h"
#include "stack.h"
#include "idle_debug.h"

FixedArrayBodyDefine(ProcessTable);
FixedArrayBodyDefine(ProcessArguments);
FixedArrayBodyDefine(SpawnArguments);
FixedArrayBodyDefine(ProcessChildEvents);
LinkedListBodyDefine(UserAllocations);
LinkedListBodyDefine(UserMappings);
LinkedListBodyDefine(ParkedOperations);
MapBodyDefine(ProcessSignalActions);
LinkedListBodyDefine(ProcessSignalFrames);

static ProcessID pid_counter = 0;
static mutex_t pid_lock = MUTEX_INIT;

static BString no_entry_point_message = bstr("No entry point found in ELF");
static BString user_allocation_not_found = bstr("User allocation not found");
static BString static_user_allocation = bstr("Cannot free static user allocation");
static BString user_allocation_full = bstr("Too many user allocations");
static BString invalid_user_allocation = bstr("Invalid user allocation");
static BString invalid_user_permissions = bstr("Invalid user page permissions");
static BString invalid_heap_break = bstr("Invalid heap break");
static BString argument_too_long = bstr("Argument too long");
static BString fork_no_memory = bstr("Unable to allocate fork child");
static BString fork_unsupported_state = bstr("Process state cannot be forked");
static BString signal_frame_allocation_failed = bstr("Unable to save signal context");
static BString signal_frame_missing = bstr("No signal context to restore");
static ProcessSignalAction ProcessSignalAction__default(void);
static bool ProcessSignal__default_is_ignored(ProcessSignal signal);
static const ProcessCapabilities PROCESS__HUND_CAPABILITIES =
    PROCESS_CAP_BECOME_SERVICE | PROCESS_CAP_MAP_MMIO | PROCESS_CAP_SUBSCRIBE_IRQ | PROCESS_CAP_REGISTER_BLOCK_DEVICE |
    PROCESS_CAP_HANDLE_BLOCK_REQUESTS | PROCESS_CAP_MOUNT_FILESYSTEM | PROCESS_CAP_USE_BLOCK_DEVICES |
    PROCESS_CAP_MANAGE_SHARED_PAGES;

FixedArrayBodyDefine(ProcessVirtioDevices);

void ProcessContext__dispose(ProcessContextPtr self)
{
    Kernel__free(addr(self));
}

Result(ProcessContextSaveCode) Process__store_context(ProcessPtr self, ProcessContextPtr context)
{
    match_optional(
        self->context,
        some(destination)
        {
            if (self->state != PROC_STATE_RUNNING)
            {
                BString format = bstr("Process not running state=%s pid=%i name=%s\n");
                BString message =
                    BString__sprintf(&format, ProcessState__to_string(self->state), self->pid, cstr(self->name));
                return Error(ProcessContextSaveCode, message, PROCESS_CONTEXT_SAVE_INVALID_STATE);
            }

            __d_printf(PROCESS, "Process context save self=%p context=%p %i\n", destination, context, self->pid);
            ProcessContext__copy(destination, context);
            return Success(ProcessContextSaveCode, PROCESS_CONTEXT_SAVE_OK);
        },
        none()
        {
            return Error(ProcessContextSaveCode, bstr("Process context is missing"),
                         PROCESS_CONTEXT_SAVE_DESTINATION_MISSING);
        }
    );
}

Result(ProcessContextSaveCode) Process__save_context(ProcessPtr self, const OptionalPtr(ProcessContextPtr) context)
{
    match_optional(
        *context,
        some(source)
        {
            return Process__store_context(self, source);
        },
        none()
        {
            return Success(ProcessContextSaveCode, PROCESS_CONTEXT_SAVE_OK);
        }
    );
}

ProcessContextPtr Process__required_context(ProcessPtr self)
{
    match_optional(
        self->context,
        some(context)
        {
            return context;
        },
        none()
        {
            __d_printf(PANIC, "Process context is missing pid=%i name=%s\n", self->pid, cstr(self->name));
            CPU__halt_all();
            __builtin_unreachable();
        }
    );
}

void Process__activate_address_space(ProcessPtr self)
{
    ProcessContextPtr context = Process__required_context(self);
    KernelAddress page_table = ProcessPageTable__root(&self->page_table);
    Process__switch_address_space(self, context, page_table);
}

void Process__context_switch(ProcessPtr self)
{
    ProcessContextPtr context = Process__required_context(self);
    KernelAddress page_table = ProcessPageTable__root(&self->page_table);
    Process__switch_context(self, context, page_table);
}

static ProcessSignalMask ProcessSignal__mask(ProcessSignal signal)
{
    return (ProcessSignalMask)1ULL << (signal - 1);
}

static ProcessSignalMask ProcessSignal__mask_without_unmaskable(ProcessSignalMask mask)
{
    return mask & ~(ProcessSignal__mask(PROCESS_SIGNAL_KILL) | ProcessSignal__mask(PROCESS_SIGNAL_STOP));
}

static ProcessSignalMask ProcessSignal__stop_mask(void)
{
    return ProcessSignal__mask(PROCESS_SIGNAL_STOP) | ProcessSignal__mask(PROCESS_SIGNAL_TERMINAL_STOP) |
           ProcessSignal__mask(PROCESS_SIGNAL_TERMINAL_INPUT) | ProcessSignal__mask(PROCESS_SIGNAL_TERMINAL_OUTPUT);
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

bool ProcessSignal__numbers_equal(void * left, void * right)
{
    return *(ProcessSignalPtr)left == *(ProcessSignalPtr)right;
}

bool ProcessSignalFrame__equals(ProcessSignalFramePtr self, ProcessSignalFramePtr other)
{
    return self->context == other->context;
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
    case USER_ALLOCATION_DOORBELL:
        return "DOORBELL";
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

static UserMapping UserMapping__new(UserAddress address, size_t size)
{
    return (UserMapping){
        .address = address,
        .size = size,
    };
}

bool UserMapping__equals(UserMappingPtr self, UserMappingPtr other)
{
    return self->address.virtual_addr == other->address.virtual_addr && self->size == other->size;
}

static bool mapping_contains_range(UserMappingPtr mapping, void * context)
{
    UserAllocationRangeMatchPtr range = (UserAllocationRangeMatchPtr)context;
    Address mapping_start = mapping->address.virtual_addr;
    Address mapping_end = mapping_start + mapping->size;
    Address range_end = range->virtual_address + range->size;

    return range->virtual_address >= mapping_start && range_end <= mapping_end && range_end >= range->virtual_address;
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

static bool Process__alignment_is_valid(size_t alignment)
{
    return alignment >= PAGE_SIZE && (alignment & (alignment - 1)) == 0;
}

static Optional(Address) Process__align_user_address(Address address, size_t alignment)
{
    Address mask = alignment - 1;
    if (address > UINT64_MAX - mask)
    {
        return None(Address);
    }
    return Some(Address, (address + mask) & ~mask);
}

static bool Process__user_range_is_free(ProcessPtr self, Address virtual_address, size_t size)
{
    if (virtual_address < USER_ALLOC_BASE || virtual_address % PAGE_SIZE != 0 || size == 0 ||
        virtual_address >= HUND_SERVICE_DIRECTORY_VADDR || size > HUND_SERVICE_DIRECTORY_VADDR - virtual_address)
    {
        return false;
    }

    Address range_end = virtual_address + size;
    for_each_ll(
        UserAllocations,
        self->user_allocations,
        allocation,
        {
            Address allocation_start = allocation.address.virtual_addr;
            Address allocation_end = allocation_start + allocation.size;
            if (allocation_start < range_end && virtual_address < allocation_end)
            {
                return false;
            }
        }
    );
    return true;
}

static Optional(Address) Process__find_free_user_range(ProcessPtr self, size_t size, size_t alignment)
{
    Address candidate;
    match_optional(
        Process__align_user_address(USER_ALLOC_BASE, alignment),
        some(aligned)
        {
            candidate = aligned;
        },
        none()
        {
            return None(Address);
        }
    );
    while (candidate < HUND_SERVICE_DIRECTORY_VADDR && size <= HUND_SERVICE_DIRECTORY_VADDR - candidate)
    {
        Address candidate_end = candidate + size;
        Address next_candidate = candidate;
        for_each_ll(
            UserAllocations,
            self->user_allocations,
            allocation,
            {
                Address allocation_start = allocation.address.virtual_addr;
                Address allocation_end = allocation_start + allocation.size;
                if (allocation_start < candidate_end && candidate < allocation_end &&
                    allocation_end > next_candidate)
                {
                    next_candidate = allocation_end;
                }
            }
        );
        if (next_candidate == candidate)
        {
            return Some(Address, candidate);
        }
        match_optional(
            Process__align_user_address(next_candidate, alignment),
            some(aligned)
            {
                candidate = aligned;
            },
            none()
            {
                return None(Address);
            }
        );
    }
    return None(Address);
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

ResultDef(ProcessStartup);

typedef enum ProcessStartupKind
{
    PROCESS_STARTUP_ARGUMENTS,
    PROCESS_STARTUP_EXEC_PAYLOAD,
} ProcessStartupKind;

typedef struct ProcessStartupInput
{
    ProcessStartupKind kind;

    union
    {
        ConstProcessArgumentsPtr arguments;
        ConstExecPayloadPtr payload;
    } value;
} ProcessStartupInput;

typedef const ProcessStartupInput * ConstProcessStartupInputPtr;

static ProcessStartupInput ProcessStartupInput__new_arguments(ConstProcessArgumentsPtr arguments)
{
    return (ProcessStartupInput){.kind = PROCESS_STARTUP_ARGUMENTS, .value.arguments = arguments};
}

static ProcessStartupInput ProcessStartupInput__new_payload(ConstExecPayloadPtr payload)
{
    return (ProcessStartupInput){.kind = PROCESS_STARTUP_EXEC_PAYLOAD, .value.payload = payload};
}

static Result(ProcessStartup) Process__setup_args(ProcessPtr self, ConstProcessArgumentsPtr arguments)
{
    match_result(
        Process__alloc_user_pages(self, PAGE_SIZE),
        success(args_page)
        {
            Address user_base = args_page.virtual_addr;
            Address kernel_base = args_page.kernel_addr.virtual_addr;
            AddressPtr argv = ptr(kernel_base, Address);
            ProcessArguments arguments_copy = *arguments;
            uint64_t argc = ProcessArguments__len(&arguments_copy);
            AddressPtr envp = ptr(kernel_base + sizeof(Address) * (argc + 1), Address);

            size_t cursor = sizeof(Address) * (argc + 2);
            for_each_fa(
                ProcessArguments,
                arguments_copy,
                i,
                arg,
                {
                    if (cursor + (size_t)BString__length(&arg) + 1 > PAGE_SIZE)
                    {
                        return Error(ProcessStartup, argument_too_long, SPAWN_ARG_TOO_LONG);
                    }
                    argv[i] = user_base + cursor;
                    cursor += Process__copy_arg(ptr(kernel_base + cursor, Data), &arg);
                }
            );
            argv[argc] = 0;
            envp[0] = 0;
            Address current_directory = user_base + cursor;
            Process__copy_arg(ptr(kernel_base + cursor, Data), &bstr("/"));
            return Success(
                ProcessStartup,
                ProcessStartup__new(argc, user_base, user_base + sizeof(Address) * (argc + 1), current_directory, 0));
        },
        error(code, message)
        {
            return Error(ProcessStartup, message, code);
        }
    );

    return Error(ProcessStartup, argument_too_long, SPAWN_ARG_TOO_LONG);
}

static Result(ProcessStartup) Process__setup_exec_payload(ProcessPtr self, ConstExecPayloadPtr payload)
{
    size_t argument_count = ExecPayload__argument_count(payload);
    size_t environment_count = ExecPayload__environment_count(payload);
    size_t size = sizeof(Address) * (argument_count + environment_count + 2);
    const char * payload_current_directory = ExecPayload__current_directory(payload);
    size_t current_directory_length = ExecPayload__bounded_string_length(
        payload, (const uint8_t *)payload_current_directory - (const uint8_t *)payload);
    size += current_directory_length + 1;
    size = (size + (_Alignof(ExecPayload) - 1)) & ~((size_t)_Alignof(ExecPayload) - 1);
    size += ExecPayload__size(payload);
    for (size_t index = 0; index < argument_count; index++)
    {
        const char * argument = ExecPayload__argument_at(payload, index);
        size += ExecPayload__bounded_string_length(payload, (const uint8_t *)argument - (const uint8_t *)payload) + 1;
    }

    for (size_t index = 0; index < environment_count; index++)
    {
        const char * environment = ExecPayload__environment_at(payload, index);
        size +=
            ExecPayload__bounded_string_length(payload, (const uint8_t *)environment - (const uint8_t *)payload) + 1;
    }

    match_result(
        Process__alloc_user_pages(self, size),
        success(startup_page)
        {
            Address user_base = startup_page.virtual_addr;
            Address kernel_base = startup_page.kernel_addr.virtual_addr;
            AddressPtr argv = ptr(kernel_base, Address);
            AddressPtr envp = ptr(kernel_base + sizeof(Address) * (argument_count + 1), Address);
            size_t cursor = sizeof(Address) * (argument_count + environment_count + 2);

            for (size_t index = 0; index < argument_count; index++)
            {
                const char * argument = ExecPayload__argument_at(payload, index);
                size_t length =
                    ExecPayload__bounded_string_length(payload, (const uint8_t *)argument - (const uint8_t *)payload) +
                    1;
                argv[index] = user_base + cursor;
                Kernel__memcopy((Address)argument, kernel_base + cursor, length);
                cursor += length;
            }
            argv[argument_count] = 0;
            for (size_t index = 0; index < environment_count; index++)
            {
                const char * environment = ExecPayload__environment_at(payload, index);
                size_t length = ExecPayload__bounded_string_length(payload, (const uint8_t *)environment -
                                                                                (const uint8_t *)payload) +
                                1;
                envp[index] = user_base + cursor;
                Kernel__memcopy((Address)environment, kernel_base + cursor, length);
                cursor += length;
            }
            envp[environment_count] = 0;
            Address current_directory = user_base + cursor;
            Kernel__memcopy((Address)payload_current_directory, kernel_base + cursor, current_directory_length + 1);
            cursor += current_directory_length + 1;
            cursor = (cursor + (_Alignof(ExecPayload) - 1)) & ~((size_t)_Alignof(ExecPayload) - 1);
            Address exec_payload = user_base + cursor;
            Kernel__memcopy((Address)payload, kernel_base + cursor, ExecPayload__size(payload));
            return Success(ProcessStartup, ProcessStartup__new(argument_count, user_base,
                                                               user_base + sizeof(Address) * (argument_count + 1),
                                                               current_directory, exec_payload));
        },
        error(code, message)
        {
            return Error(ProcessStartup, message, code);
        }
    );

    return Error(ProcessStartup, argument_too_long, SPAWN_ARG_TOO_LONG);
}

static Result(ProcessStartup) Process__setup_startup(ProcessPtr self, ConstProcessStartupInputPtr startup)
{
    switch (startup->kind)
    {
    case PROCESS_STARTUP_ARGUMENTS:
        return Process__setup_args(self, startup->value.arguments);
    case PROCESS_STARTUP_EXEC_PAYLOAD:
        return Process__setup_exec_payload(self, startup->value.payload);
    }
    return Error(ProcessStartup, argument_too_long, SPAWN_FAILED);
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
        KernelClock__map_process(&self->kernel->clock, self),
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

static Result(ProcessPtr)
    Process__new_base(KernelPtr kernel, ProcessID pid, ConstBStringPtr name, ConstProcessPageTablePtr page_table,
                      ConstUserAddressPtr entry, ConstStackPtr stack, OptionalPtr(ProcessPtr) parent)
{
    __d_printf(PROCESS, "Creating process with entry point %p\n", entry);
    ProcessPtr proc = ptr(Kernel__alloc_kernel(sizeof(Process)), Process);
    proc->lock = MUTEX_INIT;
    proc->kernel = kernel;
    proc->entry_point = *entry;
    proc->page_table = *page_table;
    proc->context = None(ProcessContextPtr);
    proc->state = PROC_STATE_NEW;
    proc->pid = pid;
    proc->process_group_id = pid;
    proc->session_id = pid;
    proc->execed_since_fork = true;
    proc->process_registry_mapped = false;
    proc->stack = *stack;
    proc->last_tick = 0;
    proc->dispatched_at = 0;
    proc->migration_safe = true;
    proc->preferred_active = false;
    proc->preferred_warm = false;
    proc->preferred_dependency_weight = 0;
    proc->preferred_ticket = None(RequestTicket);
    proc->preferred_eligible_at_usec = 0;
    proc->preferred_selected_at_usec = 0;
    proc->preferred_dispatched_at_usec = 0;
    proc->preferred_trace_pending = false;
    proc->cpu_usage = CPUUsage__new();
    proc->queue_wait_usage = QueueWaitUsage__new();
    proc->queue_wait_control = None(UserAddress);
    proc->queue_wait_doorbell = None(IpcEventGeneration);
    proc->ipc_doorbell = None(UserAddress);
    proc->name = *name;
    proc->startup_argument_count = 0;
    proc->parked_operations = ParkedOperations__new();
    proc->parent = *parent;
    proc->pending_fork_child = None(ProcessPtr);
    proc->parent_pid = None(ProcessID);
    match_optional(
        *parent,
        some(parent_process)
        {
            proc->parent_pid = Some(ProcessID, parent_process->pid);
            proc->process_group_id = parent_process->process_group_id;
            proc->session_id = parent_process->session_id;
        },
        none_do_nothing()
    );
    proc->heap_break = USER_HEAP_BASE;
    proc->heap_mapped_end = USER_HEAP_BASE;
    proc->user_allocations = UserAllocations__new();
    proc->user_mappings = UserMappings__new();
    proc->virtio_devices = ProcessVirtioDevices__new();
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
    proc->memory_reclaimer = false;
    proc->idle_debug_address = None(UserAddress);
    proc->user_id = PROCESS_USER_ROOT;
    proc->group_id = PROCESS_GROUP_ROOT;
    proc->capabilities = PROCESS_CAP_ALL;
    proc->signal_actions = ProcessSignalActions__new();
    proc->signal_mask = 0;
    proc->pending_signals = 0;
    proc->forced_signal = None(ProcessSignal);
    proc->signal_frames = ProcessSignalFrames__new();
    proc->alarm_deadline_usec = None(ProcessAlarmDeadline);
    proc->child_events = ProcessChildEvents__new();
    proc->children_user_usec = 0;
    proc->children_system_usec = 0;

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
        success(_)
        {
            return Success(ProcessPtr, proc);
        },
        error(code, message)
        {
            Process__dispose_page_table(proc);
            Process__dispose(proc);
            return Error(ProcessPtr, message, code);
        }
    );

    return Error(ProcessPtr, user_allocation_not_found, PROCESS_FAILED_TO_SETUP);
}

static Result(ProcessPtr)
    Process__new_with_startup(KernelPtr kernel, ProcessID pid, ConstBStringPtr name,
                              ConstProcessPageTablePtr page_table, ConstUserAddressPtr entry, ConstStackPtr stack,
                              OptionalPtr(ProcessPtr) parent, ConstProcessStartupInputPtr startup_input)
{
    match_result(
        Process__new_base(kernel, pid, name, page_table, entry, stack, parent),
        success(proc)
        {
            match_result(
                Process__setup_startup(proc, startup_input),
                success(startup)
                {
                    proc->startup_argument_count = startup.argument_count;
                    __d_printf(
                        PROCESS,
                        "Process__new before context init pid=%i proc=%p stack_ptr=%p stack_base=%p stack_top=%p argc=%i argv=%p entry=%p page_table=%p\n",
                        proc->pid, proc, &proc->stack, proc->stack.base.virtual_addr, proc->stack.top.virtual_addr,
                        startup.argument_count, startup.arguments, entry->virtual_addr,
                        ProcessPageTable__root(&proc->page_table).phys_addr);
                    KernelAddress root = ProcessPageTable__root(&proc->page_table);
                    ProcessContextPtr context =
                        ProcessContext__init(kernel, proc->pid, &proc->stack, *entry, root, &startup);
                    proc->context = Some(ProcessContextPtr, context);
                    __d_printf(PROCESS, "Process__new after context init pid=%i proc=%p context=%p\n", proc->pid, proc,
                               context);
                    return Success(ProcessPtr, proc);
                },
                error(code, message)
                {
                    return Error(ProcessPtr, message, code);
                }
            );

            return Error(ProcessPtr, argument_too_long, SPAWN_ARG_TOO_LONG);
        },
        error(code, message)
        {
            return Error(ProcessPtr, message, code);
        }
    );
    return Error(ProcessPtr, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
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
    ProcessStartupInput startup = ProcessStartupInput__new_arguments(arguments);
    return Process__new_with_startup(kernel, pid, name, page_table, entry, stack, parent, &startup);
}

static bool Process__has_forkable_allocations(ProcessPtr self)
{
    for_each_ll(
        UserAllocations,
        self->user_allocations,
        allocation,
        {
            if (allocation.type == USER_ALLOCATION_MMIO)
            {
                return false;
            }
        }
    );

    return true;
}

static bool Process__forkable(ProcessPtr self)
{
    return self->state == PROC_STATE_RUNNING && self->process_class == PROCESS_CLASS_NORMAL &&
           ParkedOperations__len(&self->parked_operations) == 0 &&
           ProcessVirtioDevices__len(&self->virtio_devices) == 0 && is_nothing(self->exec_load) &&
           is_nothing(self->idle_debug_address) && is_nothing(self->pending_fork_child) &&
           Process__has_forkable_allocations(self);
}

static void Process__dispose_unpublished_fork_child(ProcessPtr self)
{
    Process__dispose_user_allocations(self);
    Process__dispose_page_table(self);
    Process__dispose(self);
}

static Result(ProcessMemoryCode)
    Process__copy_fork_permissions(ProcessPtr self, ProcessPtr source_process, ConstUserAllocationPtr source)
{
    Address allocation_start = source->address.virtual_addr;
    Address allocation_end = allocation_start + source->size;
    for_each_ll(
        UserMappings,
        source_process->user_mappings,
        mapping,
        {
            Address mapping_start = mapping.address.virtual_addr;
            Address mapping_end = mapping_start + mapping.size;
            if (mapping_start < allocation_start || mapping_end > allocation_end)
            {
                continue;
            }
            MemPagePermissions permissions = MEM_PAGE_NONE;
            if (mapping.address.execute)
            {
                permissions = MEM_PAGE_READ_EXECUTE;
            }
            else if (mapping.address.write)
            {
                permissions = MEM_PAGE_READ_WRITE;
            }
            else if (mapping.address.read)
            {
                permissions = MEM_PAGE_READ_ONLY;
            }
            match_result(
                Process__protect_user_pages(self, mapping_start, mapping.size, permissions),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(ProcessMemoryCode, message, code);
                }
            );
        }
    );

    return Success(ProcessMemoryCode, PROCESS_MEMORY_OK);
}

static Result(ProcessMemoryCode)
    Process__copy_fork_allocation(ProcessPtr self, ProcessPtr source_process, ConstUserAllocationPtr source)
{
    match_result(
        Kernel__try_alloc_user(source->address.virtual_addr, source->size, source->address.read, source->address.write,
                               source->address.execute),
        success(copy)
        {
            Kernel__memcopy(source->address.kernel_addr.virtual_addr, copy.kernel_addr.virtual_addr, source->size);
            if (copy.read || copy.write || copy.execute)
            {
                PageTable__map_unpublished(&self->page_table, self->kernel, copy, source->size);
            }
            match_result(
                Process__record_user_allocation(self, copy, source->size, source->type),
                success(_)
                {
                    if (source->type == USER_ALLOCATION_DYNAMIC)
                    {
                        match_result(
                            Process__copy_fork_permissions(self, source_process, source),
                            success_do_nothing(),
                            error(code, message)
                            {
                                return Error(ProcessMemoryCode, message, code);
                            }
                        );
                    }
                    if (source->type == USER_ALLOCATION_STACK)
                    {
                        self->stack.base = copy;
                        self->stack.top = UserAddress__relative(&copy, source->size);
                    }
                    Address entry = self->entry_point.virtual_addr;
                    if (entry >= source->address.virtual_addr && entry - source->address.virtual_addr < source->size)
                    {
                        self->entry_point = UserAddress__relative(&copy, entry - source->address.virtual_addr);
                    }
                    return Success(ProcessMemoryCode, PROCESS_MEMORY_OK);
                },
                error(code, message)
                {
                    PageTable__unmap(&self->page_table, self->kernel, copy.virtual_addr, source->size);
                    Kernel__free(copy.kernel_addr.virtual_addr);
                    return Error(ProcessMemoryCode, message, code);
                }
            );
        },
        error(code, message)
        {
            return Error(ProcessMemoryCode, message, PROCESS_MEMORY_FULL);
        }
    );

    return Error(ProcessMemoryCode, fork_no_memory, PROCESS_MEMORY_FULL);
}

static bool Process__inherits_fork_allocation(ProcessPtr self, ConstUserAllocationPtr allocation)
{
    if (allocation->type == USER_ALLOCATION_SHARED || allocation->type == USER_ALLOCATION_DOORBELL)
    {
        return false;
    }
    match_optional(
        self->queue_wait_control,
        some(mapping)
        {
            if (allocation->address.virtual_addr == mapping.virtual_addr)
            {
                return false;
            }
        },
        none_do_nothing()
    );
    return true;
}

Result(ProcessPtr) Process__fork(ProcessPtr self)
{
    if (!Process__forkable(self))
    {
        return Error(ProcessPtr, fork_unsupported_state, PROCESS_FORK_UNSUPPORTED_STATE);
    }

    match_result(
        ProcessPageTable__new(self->kernel),
        success(page_table)
        {
            ProcessID pid;
            critical_section(
                pid_lock,
                pid = ++pid_counter
            );
            Stack stack = NULL_STACK;
            Optional(ProcessPtr) parent = Some(ProcessPtr, self);
            match_result(
                Process__new_base(self->kernel, pid, &self->name, &page_table, &self->entry_point, &stack, &parent),
                success(child)
                {
                    child->startup_argument_count = self->startup_argument_count;
                    child->execed_since_fork = false;
                    child->heap_break = self->heap_break;
                    child->heap_mapped_end = self->heap_mapped_end;
                    child->signal_actions = self->signal_actions;
                    child->signal_mask = self->signal_mask;

                    for_each_ll(
                        UserAllocations,
                        self->user_allocations,
                        allocation,
                        {
                            if (!Process__inherits_fork_allocation(self, &allocation))
                            {
                                continue;
                            }
                            match_result(
                                Process__copy_fork_allocation(child, self, &allocation),
                                success_do_nothing(),
                                error(code, message)
                                {
                                    Process__dispose_unpublished_fork_child(child);
                                    return Error(ProcessPtr, message, PROCESS_FORK_NO_MEMORY);
                                }
                            );
                        }
                    );

                    ProcessContextPtr context = ProcessContext__clone(self->kernel, Process__required_context(self));
                    child->context = Some(ProcessContextPtr, context);
                    Process__set_syscall_result(child, 0);
                    ArchPageTable__publish(&child->page_table);
                    return Success(ProcessPtr, child);
                },
                error(code, message)
                {
                    return Error(ProcessPtr, message, PROCESS_FORK_NO_MEMORY);
                }
            );
            return Error(ProcessPtr, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
        },
        error(code, message)
        {
            return Error(ProcessPtr, message, PROCESS_FORK_NO_MEMORY);
        }
    );
    return Error(ProcessPtr, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

void Process__abort_pending_fork(ProcessPtr self)
{
    match_optional(
        self->pending_fork_child,
        some(child)
        {
            self->pending_fork_child = None(ProcessPtr);
            Process__dispose_unpublished_fork_child(child);
        },
        none_do_nothing()
    );
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
                    cs_return(Success(CollectionCode, index));
                },
                error(code, message)
                {
                    cs_return(Error(CollectionCode, message, code));
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
                    cs_return(Some(ParkedOperationPtr, parked_operation));
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
                                    cs_return(Some(ParkedOperationPtr, removed));
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
                    cs_return(Some(ParkReason, parked_operation->park_reason));
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
                            cs_return(Some(SchedulerPtr, scheduler));
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
                                            cs_return(true);
                                        }
                                    },
                                    none_do_nothing()
                                );
                            },
                            none()
                            {
                                cs_return(true);
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

bool Process__write_child_abort_report(ProcessPtr self, SpawnAbortReport report)
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
                        if (parked_operation->park_reason == PARK_WAIT_CHILD &&
                            parked_operation->operation_context != 0)
                        {
                            *(SpawnAbortReportPtr)parked_operation->operation_context = report;
                            cs_return(true);
                        }
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
                                            cs_return(true);
                                        }
                                    },
                                    none_do_nothing()
                                );
                            },
                            none()
                            {
                                cs_return(true);
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
                                            cs_return(true);
                                        }
                                    },
                                    none_do_nothing()
                                );
                            },
                            none()
                            {
                                parked_operation->return_code = *return_code;
                                ParkedOperation__mark_ready(parked_operation);
                                cs_return(true);
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

bool Process__mark_child_wait_ready(ProcessPtr self, ProcessID child_pid, ProcessGroupID child_process_group_id,
                                    uint64_t event_type)
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
                        if (parked_operation->park_reason != PARK_WAIT_CHILD_EVENT)
                        {
                            continue;
                        }
                        int64_t selection = (int64_t)unwrap_optional(parked_operation->operation_key, 0);
                        uint64_t options = parked_operation->operation_context;
                        bool selected = event_type == PROCESS_CHILD_EVENT_EXITED ||
                                        (event_type == PROCESS_CHILD_EVENT_STOPPED &&
                                         (options & PROCESS_CHILD_WAIT_UNTRACED) != 0) ||
                                        (event_type == PROCESS_CHILD_EVENT_CONTINUED &&
                                         (options & PROCESS_CHILD_WAIT_CONTINUED) != 0);
                        bool child_selected = (selection > 0 && (ProcessID)selection == child_pid) || selection == -1 ||
                                              (selection == 0 && self->process_group_id == child_process_group_id) ||
                                              (selection < -1 && (ProcessGroupID)-selection == child_process_group_id);
                        if (selected && child_selected)
                        {
                            ParkedOperation__mark_ready(parked_operation);
                            cs_return(true);
                        }
                    },
                    none_do_nothing()
                );
            }
        }
    );

    return false;
}

bool Process__mark_signal_suspend_ready(ProcessPtr self)
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
                        if (parked_operation->park_reason != PARK_WAIT_SIGNAL)
                        {
                            continue;
                        }
                        while (true)
                        {
                            ProcessSignal signal = 0;
                            match_optional(
                                Process__take_pending_signal(self),
                                some(pending)
                                {
                                    signal = pending;
                                },
                                none()
                                {
                                    cs_return(false);
                                }
                            );
                            ProcessSignalAction action =
                                unwrap_optional(ProcessSignalActions__get(&self->signal_actions, signal),
                                                ProcessSignalAction__default());
                            if (action.handler == PROCESS_SIGNAL_IGNORE_HANDLER ||
                                (action.handler == PROCESS_SIGNAL_DEFAULT_HANDLER &&
                                 ProcessSignal__default_is_ignored(signal)))
                            {
                                continue;
                            }
                            self->signal_mask = parked_operation->operation_context;
                            self->forced_signal = Some(ProcessSignal, signal);
                            parked_operation->return_code = Some(ReturnCode, PROCESS_SIGNAL_INTERRUPTED);
                            ParkedOperation__mark_ready(parked_operation);
                            cs_return(true);
                        }
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
                cs_return(false);
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
                                cs_return(true);
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
            cs_return(ParkedOperations__len(&self->parked_operations) > 0);
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
    return signal > 0 && signal <= PROCESS_SIGNAL_COUNT;
}

void Process__queue_signal(ProcessPtr self, ProcessSignal signal)
{
    if (signal == PROCESS_SIGNAL_CONTINUE)
    {
        __atomic_fetch_and(&self->pending_signals, ~ProcessSignal__stop_mask(), __ATOMIC_ACQ_REL);
    }
    else if ((ProcessSignal__stop_mask() & ProcessSignal__mask(signal)) != 0)
    {
        __atomic_fetch_and(&self->pending_signals, ~ProcessSignal__mask(PROCESS_SIGNAL_CONTINUE), __ATOMIC_ACQ_REL);
    }

    __atomic_fetch_or(&self->pending_signals, ProcessSignal__mask(signal), __ATOMIC_RELEASE);
}

bool Process__has_pending_signal(ProcessPtr self)
{
    return __atomic_load_n(&self->pending_signals, __ATOMIC_ACQUIRE) != 0;
}

bool Process__has_pending_kill(ProcessPtr self)
{
    return (__atomic_load_n(&self->pending_signals, __ATOMIC_ACQUIRE) & ProcessSignal__mask(PROCESS_SIGNAL_KILL)) != 0;
}

bool Process__has_pending_resume_signal(ProcessPtr self)
{
    ProcessSignalMask resume = ProcessSignal__mask(PROCESS_SIGNAL_KILL) | ProcessSignal__mask(PROCESS_SIGNAL_CONTINUE);
    return (__atomic_load_n(&self->pending_signals, __ATOMIC_ACQUIRE) & resume) != 0;
}

bool Process__can_resume_for_signal(ProcessPtr self)
{
    critical_section(
        self->lock,
        {
            match_optional(
                ParkedOperations__at(&self->parked_operations, -1),
                some(operation)
                {
                    if (operation->park_reason == PARK_WAIT_EXEC_LOAD)
                    {
                        cs_return(Process__has_pending_kill(self));
                    }
                },
                none_do_nothing()
            );
            ProcessSignalMask pending = __atomic_load_n(&self->pending_signals, __ATOMIC_ACQUIRE);
            ProcessSignalMask unmaskable =
                ProcessSignal__mask(PROCESS_SIGNAL_KILL) | ProcessSignal__mask(PROCESS_SIGNAL_STOP);
            cs_return((pending & (~self->signal_mask | unmaskable)) != 0);
        }
    );

    return false;
}

Optional(ProcessSignal) Process__take_pending_signal(ProcessPtr self)
{
    ProcessSignalMask pending = __atomic_load_n(&self->pending_signals, __ATOMIC_ACQUIRE);
    ProcessSignalMask deliverable = pending & ~self->signal_mask;
    deliverable |= pending & (ProcessSignal__mask(PROCESS_SIGNAL_KILL) | ProcessSignal__mask(PROCESS_SIGNAL_STOP));
    if (deliverable == 0)
    {
        return None(ProcessSignal);
    }

    if ((deliverable & ProcessSignal__mask(PROCESS_SIGNAL_KILL)) != 0)
    {
        __atomic_fetch_and(&self->pending_signals, ~ProcessSignal__mask(PROCESS_SIGNAL_KILL), __ATOMIC_ACQ_REL);
        return Some(ProcessSignal, PROCESS_SIGNAL_KILL);
    }

    for (ProcessSignal signal = 1; signal <= PROCESS_SIGNAL_COUNT; signal++)
    {
        ProcessSignalMask mask = ProcessSignal__mask(signal);
        if ((deliverable & mask) != 0 &&
            (__atomic_fetch_and(&self->pending_signals, ~mask, __ATOMIC_ACQ_REL) & mask) != 0)
        {
            return Some(ProcessSignal, signal);
        }
    }

    return None(ProcessSignal);
}

static ProcessSignalAction ProcessSignalAction__default(void)
{
    return ProcessSignalAction__new(PROCESS_SIGNAL_DEFAULT_HANDLER, 0, 0, 0);
}

ProcessSignalAction Process__signal_action(ProcessPtr self, ProcessSignal signal)
{
    ProcessSignalAction action;
    critical_section(
        self->lock,
        action = unwrap_optional(ProcessSignalActions__get(&self->signal_actions, signal),
                                                          ProcessSignalAction__default());
    )
    ;
    return action;
}

Result(ProcessSignalResult)
    Process__set_signal_action(ProcessPtr self, ProcessSignal signal, ConstProcessSignalActionPtr action)
{
    if (!Process__supports_signal(self, signal) || signal == PROCESS_SIGNAL_KILL || signal == PROCESS_SIGNAL_STOP)
    {
        return Error(ProcessSignalResult, bstr("Signal action cannot be changed"), PROCESS_SIGNAL_INVALID_ARGUMENT);
    }

    ProcessSignalAction replacement = *action;
    replacement.mask = ProcessSignal__mask_without_unmaskable(replacement.mask);
    critical_section(
        self->lock,
        {
            match_result(
                ProcessSignalActions__set(&self->signal_actions, signal, replacement),
                success(_)
                {
                    cs_return(Success(ProcessSignalResult, PROCESS_SIGNAL_OK));
                },
                error(code, message)
                {
                    cs_return(Error(ProcessSignalResult, message, PROCESS_SIGNAL_NO_MEMORY));
                }
            );
        }
    );

    return Error(ProcessSignalResult, signal_frame_allocation_failed, PROCESS_SIGNAL_NO_MEMORY);
}

ProcessSignalMask Process__signal_mask(ProcessPtr self)
{
    ProcessSignalMask mask;
    critical_section(
        self->lock,
        mask = self->signal_mask;
    )
    ;
    return mask;
}

Result(ProcessSignalResult) Process__set_signal_mask(ProcessPtr self, uint64_t operation, ProcessSignalMask mask)
{
    ProcessSignalMask requested = ProcessSignal__mask_without_unmaskable(mask);
    critical_section(
        self->lock,
        {
            switch (operation)
            {
            case PROCESS_SIGNAL_MASK_BLOCK:
                self->signal_mask |= requested;
                break;
            case PROCESS_SIGNAL_MASK_UNBLOCK:
                self->signal_mask &= ~requested;
                break;
            case PROCESS_SIGNAL_MASK_SET:
                self->signal_mask = requested;
                break;
            default:
                cs_return(Error(ProcessSignalResult, bstr("Invalid signal mask operation"),
                                PROCESS_SIGNAL_INVALID_ARGUMENT));
            }
            cs_return(Success(ProcessSignalResult, PROCESS_SIGNAL_OK));
        }
    );

    return Error(ProcessSignalResult, bstr("Invalid signal mask operation"), PROCESS_SIGNAL_INVALID_ARGUMENT);
}

ProcessSignalMask Process__pending_signal_mask(ProcessPtr self)
{
    return __atomic_load_n(&self->pending_signals, __ATOMIC_ACQUIRE) & self->signal_mask;
}

static bool ProcessSignal__default_is_ignored(ProcessSignal signal)
{
    return signal == PROCESS_SIGNAL_CHILD || signal == PROCESS_SIGNAL_URGENT ||
           signal == PROCESS_SIGNAL_WINDOW_CHANGE || signal == PROCESS_SIGNAL_INFO;
}

static bool ProcessSignal__default_stops(ProcessSignal signal)
{
    return (ProcessSignal__stop_mask() & ProcessSignal__mask(signal)) != 0;
}

static bool ProcessSignal__restarts_wait(ParkReason reason)
{
    return reason == PARK_WAIT_CHILD || reason == PARK_WAIT_IO || reason == PARK_WAIT_CHILD_EVENT ||
           reason == PARK_WAIT_QUEUE;
}

static Optional(ParkedOperationPtr)
    Process__interrupt_parked_operation(ProcessPtr self, ProcessContextPtr saved_context, uint64_t action_flags)
{
    critical_section(
        self->lock,
        {
            match_optional(
                ParkedOperations__at(&self->parked_operations, -1),
                some(operation)
                {
                    if (operation->park_reason == PARK_WAIT_EXEC_LOAD)
                    {
                        cs_return(None(ParkedOperationPtr));
                    }
                    ParkReason reason = operation->park_reason;
                    match_optional(
                        ParkedOperations__remove_at(&self->parked_operations, -1),
                        some(interrupted)
                        {
                            if ((action_flags & PROCESS_SIGNAL_ACTION_RESTART) != 0 &&
                                ProcessSignal__restarts_wait(reason))
                            {
                                ProcessContext__restart_syscall(saved_context);
                            }
                            else
                            {
                                ProcessContext__set_syscall_result(saved_context, SYSCALL_INTERRUPTED);
                            }
                            cs_return(Some(ParkedOperationPtr, interrupted));
                        },
                        none_do_nothing()
                    );
                },
                none_do_nothing()
            );
        }
    );

    return None(ParkedOperationPtr);
}

static Result(ProcessSignalDelivery) Process__enter_signal_handler(ProcessPtr self, ProcessContextPtr context,
                                                                   ProcessSignal signal, ProcessSignalAction action)
{
    ProcessContextPtr saved = ProcessContext__clone(self->kernel, context);
    ProcessSignalFrame frame = ProcessSignalFrame__new(saved, self->signal_mask);

    match_result(
        ProcessSignalFrames__push(&self->signal_frames, frame),
        success_do_nothing(),
        error(code, message)
        {
            ProcessContext__dispose(saved);
            return Error(ProcessSignalDelivery, message, PROCESS_SIGNAL_NO_MEMORY);
        }
    );

    ProcessSignalMask handler_mask = action.mask;
    if ((action.flags & PROCESS_SIGNAL_ACTION_NODEFER) == 0)
    {
        handler_mask |= ProcessSignal__mask(signal);
    }

    self->signal_mask = ProcessSignal__mask_without_unmaskable(self->signal_mask | handler_mask);
    if ((action.flags & PROCESS_SIGNAL_ACTION_RESETHAND) != 0)
    {
        ProcessSignalAction default_action = ProcessSignalAction__default();
        match_result(
            ProcessSignalActions__set(&self->signal_actions, signal, default_action),
            success_do_nothing(),
            error(code, message)
            {
                self->signal_mask = frame.mask;
                match_optional(
                    ProcessSignalFrames__remove_at(&self->signal_frames, -1),
                    some(removed)
                    {
                        ProcessContext__dispose(removed.context);
                    },
                    none_do_nothing()
                );
                return Error(ProcessSignalDelivery, message, PROCESS_SIGNAL_NO_MEMORY);
            }
        );
    }

    ProcessContext__enter_signal(context, signal, action.handler, action.restorer);
    ProcessSignalDelivery delivery = ProcessSignalDelivery__new(PROCESS_SIGNAL_DELIVERY_HANDLER, signal);
    delivery.interrupted_operation = Process__interrupt_parked_operation(self, saved, action.flags);
    return Success(ProcessSignalDelivery, delivery);
}

Result(ProcessSignalDelivery) Process__deliver_pending_signal(ProcessPtr self, ProcessContextPtr context)
{
    while (true)
    {
        ProcessSignal signal = 0;
        Optional(ProcessSignal) selected = None(ProcessSignal);
        critical_section(
            self->lock,
            {
                match_optional(
                    self->forced_signal,
                    some(forced)
                    {
                        selected = Some(ProcessSignal, forced);
                        self->forced_signal = None(ProcessSignal);
                    },
                    none_do_nothing()
                );
            }
        );

        if (is_nothing(selected))
        {
            selected = Process__take_pending_signal(self);
        }
        match_optional(
            selected,
            some(pending)
            {
                signal = pending;
            },
            none()
            {
                return Success(ProcessSignalDelivery, ProcessSignalDelivery__new(PROCESS_SIGNAL_DELIVERY_NONE, 0));
            }
        );

        ProcessSignalAction action = Process__signal_action(self, signal);
        if (action.handler == PROCESS_SIGNAL_IGNORE_HANDLER ||
            (action.handler == PROCESS_SIGNAL_DEFAULT_HANDLER && ProcessSignal__default_is_ignored(signal)))
        {
            continue;
        }

        if (action.handler == PROCESS_SIGNAL_DEFAULT_HANDLER && ProcessSignal__default_stops(signal))
        {
            return Success(ProcessSignalDelivery, ProcessSignalDelivery__new(PROCESS_SIGNAL_DELIVERY_STOP, signal));
        }

        if (action.handler == PROCESS_SIGNAL_DEFAULT_HANDLER && signal == PROCESS_SIGNAL_CONTINUE)
        {
            return Success(ProcessSignalDelivery, ProcessSignalDelivery__new(PROCESS_SIGNAL_DELIVERY_CONTINUE, signal));
        }

        if (action.handler == PROCESS_SIGNAL_DEFAULT_HANDLER || action.restorer == 0)
        {
            return Success(ProcessSignalDelivery,
                           ProcessSignalDelivery__new(PROCESS_SIGNAL_DELIVERY_TERMINATE, signal));
        }

        return Process__enter_signal_handler(self, context, signal, action);
    }
}

Result(ProcessSignalResult) Process__restore_signal_context(ProcessPtr self)
{
    match_optional(
        ProcessSignalFrames__remove_at(&self->signal_frames, -1),
        some(frame)
        {
            ProcessContext__copy(Process__required_context(self), frame.context);
            self->signal_mask = frame.mask;
            ProcessContext__dispose(frame.context);
            return Success(ProcessSignalResult, PROCESS_SIGNAL_OK);
        },
        none_do_nothing()
    );

    return Error(ProcessSignalResult, signal_frame_missing, PROCESS_SIGNAL_NO_FRAME);
}

uint64_t Process__set_alarm(ProcessPtr self, uint64_t now_usec, uint64_t seconds)
{
    uint64_t previous_seconds = 0;
    critical_section(
        self->lock,
        {
            match_optional(
                self->alarm_deadline_usec,
                some(deadline)
                {
                    if (deadline > now_usec)
                    {
                        previous_seconds = (deadline - now_usec + 999999ULL) / 1000000ULL;
                    }
                },
                none_do_nothing()
            );
            self->alarm_deadline_usec = seconds == 0
                                            ? None(ProcessAlarmDeadline)
                                            : Some(ProcessAlarmDeadline, now_usec + seconds * 1000000ULL);
        }
    );

    return previous_seconds;
}

bool Process__take_expired_alarm(ProcessPtr self, uint64_t now_usec)
{
    critical_section(
        self->lock,
        {
            match_optional(
                self->alarm_deadline_usec,
                some(deadline)
                {
                    if (deadline <= now_usec)
                    {
                        self->alarm_deadline_usec = None(ProcessAlarmDeadline);
                        cs_return(true);
                    }
                },
                none_do_nothing()
            );
        }
    );

    return false;
}

static Result(ProcessSignalResult) Process__reset_signal_state_for_exec(ProcessPtr self)
{
    ProcessSignalActions retained_actions = ProcessSignalActions__new();
    for_each_map(
        ProcessSignalActions,
        self->signal_actions,
        index,
        signal,
        action,
        {
            if (action.handler == PROCESS_SIGNAL_IGNORE_HANDLER)
            {
                match_result(
                    ProcessSignalActions__set(&retained_actions, signal, action),
                    success_do_nothing(),
                    error(code, message)
                    {
                        return Error(ProcessSignalResult, message, PROCESS_SIGNAL_NO_MEMORY);
                    }
                );
            }
        }
    );

    while (ProcessSignalFrames__len(&self->signal_frames) > 0)
    {
        match_optional(
            ProcessSignalFrames__remove_at(&self->signal_frames, -1),
            some(frame)
            {
                ProcessContext__dispose(frame.context);
            },
            none_do_nothing()
        );
    }

    self->signal_actions = retained_actions;
    return Success(ProcessSignalResult, PROCESS_SIGNAL_OK);
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

typedef struct ProcessChildEventSelection
{
    int64_t child_selection;
    ProcessGroupID caller_process_group_id;
    uint64_t options;
} ProcessChildEventSelection;

typedef ProcessChildEventSelection * ProcessChildEventSelectionPtr;

static ProcessChildEventSelection
ProcessChildEventSelection__new(int64_t child_selection, ProcessGroupID caller_process_group_id, uint64_t options)
{
    return (ProcessChildEventSelection){
        .child_selection = child_selection,
        .caller_process_group_id = caller_process_group_id,
        .options = options,
    };
}

static bool ProcessChildEvent__matches_selection(ProcessChildEventPtr self, void * context)
{
    ProcessChildEventSelectionPtr selection = context;
    int64_t child_selection = selection->child_selection;
    bool child_selected = (child_selection > 0 && self->pid == (ProcessID)child_selection) || child_selection == -1 ||
                          (child_selection == 0 && self->process_group_id == selection->caller_process_group_id) ||
                          (child_selection < -1 && self->process_group_id == (ProcessGroupID)-child_selection);
    if (!child_selected)
    {
        return false;
    }

    if (self->type == PROCESS_CHILD_EVENT_STOPPED)
    {
        return (selection->options & PROCESS_CHILD_WAIT_UNTRACED) != 0;
    }

    if (self->type == PROCESS_CHILD_EVENT_CONTINUED)
    {
        return (selection->options & PROCESS_CHILD_WAIT_CONTINUED) != 0;
    }
    return self->type == PROCESS_CHILD_EVENT_EXITED;
}

Result(ProcessChildEvent) Process__pop_child_event(ProcessPtr self, int64_t child_selection, uint64_t options)
{
    ProcessChildEventSelection selection =
        ProcessChildEventSelection__new(child_selection, self->process_group_id, options);
    match_find_result(
        ProcessChildEvents__find(&self->child_events, ProcessChildEvent__matches_selection, &selection),
        some(event)
        {
            match_result(
                ProcessChildEvents__remove(&self->child_events, event),
                success(code)
                {
                    self->children_user_usec += event.user_usec;
                    self->children_system_usec += event.system_usec;
                    return Success(ProcessChildEvent, event);
                },
                error(code, message)
                {
                    return Error(ProcessChildEvent, message, PROCESS_CHILD_EVENT_QUEUE_EMPTY);
                }
            );
        },
        none_do_nothing()
    );

    return Error(ProcessChildEvent, bstring("Child event queue empty"), PROCESS_CHILD_EVENT_QUEUE_EMPTY);
}

Result(CollectionCode) Process__push_child_event(ProcessPtr self, ConstProcessChildEventPtr event)
{
    return ProcessChildEvents__push(&self->child_events, *event);
}

void Process__account_cpu_mode(ProcessPtr self, CPUUsageMode mode)
{
    CPUTime now = CPU__time_usec();
    CPUUsage__transition(&self->cpu_usage, now, Process__user_wait_usec(self, now), mode);
}

Result(ProcessPtr) Process__setup(KernelPtr kernel, ConstElfPtr binary, OptionalPtr(ProcessPtr) parent)
{
    ProcessArguments arguments = ProcessArguments__new();
    return Process__setup_with_args(kernel, binary, parent, &arguments);
}

static Result(ProcessPtr)
    Process__setup_with_startup(KernelPtr kernel, ConstElfPtr binary, OptionalPtr(ProcessPtr) parent,
                                OptionalPtr(ProcessID) pid, ConstProcessStartupInputPtr startup)
{
    __d_println(PROCESS, "Process__spawn_from_elf");
    ExpandedSegmentArray expanded_elf = Elf__map(binary);
    __d_printf(PROCESS, "ELF: %s %x %x\n", cstr(binary->name), binary->start, binary->end);
    __d_printf(PROCESS, "ELF mapped %i\n", ExpandedSegmentArray__len(&expanded_elf));

    __d_println(PROCESS, "Creating page table");
    match_result(
        ProcessPageTable__new(kernel),
        success(page_table)
        {
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

            size_t stack_size = SystemInfo__default_user_stack_size();
            match_optional(
                kernel->system_info,
                some(system_info)
                {
                    stack_size = SystemInfo__user_stack_size(&system_info);
                },
                none_do_nothing()
            );
            __d_printf(PROCESS, "Creating stack for %s\n", cstr(binary->name));
            Stack stack = Stack__new(stack_size);
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
                    ProcessID process_id;
                    match_optional(
                        *pid,
                        some(requested_pid)
                        {
                            process_id = requested_pid;
                        },
                        none()
                        {
                            critical_section(
                                pid_lock,
                                process_id = ++pid_counter
                            );
                        }
                    );
                    match_result(
                        Process__new_with_startup(kernel, process_id, &binary->name, &page_table, &entry_address,
                                                  &stack, parent, startup),
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
                            Process__record_user_allocation(proc, stack.base, stack.size, USER_ALLOCATION_STACK);
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
        },
        error(code, message)
        {
            return Error(ProcessPtr, message, code);
        }
    );
    return Error(ProcessPtr, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

Result(ProcessPtr) Process__setup_with_args(KernelPtr kernel, ConstElfPtr binary, OptionalPtr(ProcessPtr) parent,
                                            ConstProcessArgumentsPtr arguments)
{
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
    ProcessStartupInput startup = ProcessStartupInput__new_arguments(&process_arguments);
    return Process__setup_with_startup(kernel, binary, parent, &None(ProcessID), &startup);
}

Result(ProcessPtr) Process__setup_overlay_candidate(ProcessPtr self, ConstElfPtr binary, ConstExecPayloadPtr payload)
{
    ProcessStartupInput startup = ProcessStartupInput__new_payload(payload);
    Optional(ProcessPtr) parent = self->parent;
    return Process__setup_with_startup(self->kernel, binary, &parent, &Some(ProcessID, self->pid), &startup);
}

typedef struct ProcessImage
{
    ProcessPageTable page_table;
    Optional(ProcessContextPtr) context;
    Stack stack;
    UserAddress entry_point;
    BString name;
    uint64_t startup_argument_count;
    Address heap_break;
    Address heap_mapped_end;
    UserAllocations user_allocations;
    UserMappings user_mappings;
    ProcessVirtioDevices virtio_devices;
    SpawnStdio startup_stdio;
    Optional(ProcessAbort) abort;
    Optional(UserAddress) queue_wait_control;
    Optional(UserAddress) ipc_doorbell;
    Optional(UserAddress) idle_debug_address;
} ProcessImage;

typedef const ProcessImage * ConstProcessImagePtr;

static ProcessImage ProcessImage__new(ProcessPtr self)
{
    return (ProcessImage){
        .page_table = self->page_table,
        .context = self->context,
        .stack = self->stack,
        .entry_point = self->entry_point,
        .name = self->name,
        .startup_argument_count = self->startup_argument_count,
        .heap_break = self->heap_break,
        .heap_mapped_end = self->heap_mapped_end,
        .user_allocations = self->user_allocations,
        .user_mappings = self->user_mappings,
        .virtio_devices = self->virtio_devices,
        .startup_stdio = self->startup_stdio,
        .abort = self->abort,
        .queue_wait_control = self->queue_wait_control,
        .ipc_doorbell = self->ipc_doorbell,
        .idle_debug_address = self->idle_debug_address,
    };
}

static void ProcessImage__install(ProcessPtr self, ConstProcessImagePtr image)
{
    self->page_table = image->page_table;
    self->context = image->context;
    self->stack = image->stack;
    self->entry_point = image->entry_point;
    self->name = image->name;
    self->startup_argument_count = image->startup_argument_count;
    self->heap_break = image->heap_break;
    self->heap_mapped_end = image->heap_mapped_end;
    self->user_allocations = image->user_allocations;
    self->user_mappings = image->user_mappings;
    self->virtio_devices = image->virtio_devices;
    self->startup_stdio = image->startup_stdio;
    self->abort = image->abort;
    self->queue_wait_control = image->queue_wait_control;
    self->ipc_doorbell = image->ipc_doorbell;
    self->idle_debug_address = image->idle_debug_address;
}

Result(ProcessSetupStatus) Process__commit_overlay(ProcessPtr self, ProcessPtr candidate)
{
    candidate->startup_stdio = self->startup_stdio;
    match_result(
        Process__reset_signal_state_for_exec(self),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP);
        }
    );
    ProcessImage old_image = ProcessImage__new(self);
    ProcessImage new_image = ProcessImage__new(candidate);
    ProcessImage__install(self, &new_image);
    ProcessImage__install(candidate, &old_image);
    self->execed_since_fork = true;
    Process__prepare_startup_stdio(self);
    Process__activate_address_space(self);

    match_result(
        Process__dispose_user_allocations(candidate),
        success_do_nothing(),
        error(code, message)
        {
            Kernel__panic(cstr(message), Process__required_context(self));
        }
    );
    match_result(
        Process__dispose_page_table(candidate),
        success_do_nothing(),
        error(code, message)
        {
            Kernel__panic(cstr(message), Process__required_context(self));
        }
    );
    match_result(
        Process__dispose(candidate),
        success_do_nothing(),
        error(code, message)
        {
            Kernel__panic(cstr(message), Process__required_context(self));
        }
    );
    return Success(ProcessSetupStatus, PROCESS_SETUP);
}

Result(ProcessSetupStatus) Process__dispose(ProcessPtr self)
{
    Process__abort_pending_fork(self);
    while (ProcessSignalFrames__len(&self->signal_frames) > 0)
    {
        match_optional(
            ProcessSignalFrames__remove_at(&self->signal_frames, -1),
            some(frame)
            {
                ProcessContext__dispose(frame.context);
            },
            none_do_nothing()
        );
    }

    match_result(
        ProcessSignalFrames__dispose(&self->signal_frames),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP);
        }
    );

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

    match_result(
        UserMappings__dispose(&self->user_mappings),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP);
        }
    );

    match_optional(self->context,
        some(context)
        {
            ProcessContext__dispose(context);
        },
        none_do_nothing()
    );
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
    UserMapping mapping = UserMapping__new(address, allocation.size);

    match_result(
        UserMappings__push(&self->user_mappings, mapping),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ProcessMemoryCode, user_allocation_full, PROCESS_MEMORY_FULL);
        }
    );

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
            UserMappings__remove_at(&self->user_mappings, -1);
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
                match_optional(
                    ProcessVirtioDevices__find_item(&self->virtio_devices, mapping),
                    some(index)
                    {
                        return Success(ProcessMemoryCode, PROCESS_MEMORY_OK);
                    },
                    none_do_nothing()
                );
                match_result(
                    ProcessVirtioDevices__push(&self->virtio_devices, mapping),
                    success(_)
                    {
                        return Success(ProcessMemoryCode, PROCESS_MEMORY_OK);
                    },
                    error(code, message)
                    {
                        return Error(ProcessMemoryCode, user_allocation_full, PROCESS_MEMORY_FULL);
                    }
                );
            }
        }
    );

    return Error(ProcessMemoryCode, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
}

void Process__quiesce_device(ProcessPtr self)
{
    for_each_fa_ptr(
        ProcessVirtioDevices,
        &self->virtio_devices,
        index,
        device,
        {
            Arch__reset_virtio_device(device, self->page_table.root.phys_addr);
        }
    );

    self->virtio_devices = ProcessVirtioDevices__new();
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

static Optional(UserAddress)
    Process__mmio_mapping(ProcessPtr self, Address virtual_address, size_t size, bool require_write)
{
    if (virtual_address == 0 || size == 0)
    {
        return None(UserAddress);
    }

    UserAllocationRangeMatch range = UserAllocationRangeMatch__new(virtual_address, size);
    match_find_result_ptr(
        UserAllocations__find(&self->user_allocations, allocation_contains_range, &range),
        some(allocation)
        {
            bool permitted = require_write ? allocation->address.write : allocation->address.read;
            if (allocation->type == USER_ALLOCATION_MMIO && permitted)
            {
                Offset offset = virtual_address - allocation->address.virtual_addr;
                return Some(UserAddress, UserAddress__relative(&allocation->address, offset));
            }
        },
        none_do_nothing()
    );

    return None(UserAddress);
}

Optional(UserAddress) Process__readable_mmio_mapping(ProcessPtr self, Address virtual_address, size_t size)
{
    return Process__mmio_mapping(self, virtual_address, size, false);
}

Optional(UserAddress) Process__writable_mmio_mapping(ProcessPtr self, Address virtual_address, size_t size)
{
    return Process__mmio_mapping(self, virtual_address, size, true);
}

Optional(KernelAddress) Process__writable_user_kernel_mapping(ProcessPtr self, Address virtual_address, size_t size)
{
    if (virtual_address == 0 || size == 0)
    {
        return None(KernelAddress);
    }

    UserAllocationRangeMatch range = UserAllocationRangeMatch__new(virtual_address, size);
    match_find_result_ptr(
        UserMappings__find(&self->user_mappings, mapping_contains_range, &range),
        some(mapping)
        {
            if (!mapping->address.write)
            {
                return None(KernelAddress);
            }
            match_find_result_ptr(
                UserAllocations__find(&self->user_allocations, allocation_contains_range, &range),
                some(allocation)
                {
                    if (allocation->address.kernel_addr.valid)
                    {
                        Offset offset = virtual_address - allocation->address.virtual_addr;
                        return Some(KernelAddress, KERNEL_ADDR(allocation->address.kernel_addr.virtual_addr + offset,
                                                               allocation->address.kernel_addr.phys_addr + offset));
                    }
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );

    return None(KernelAddress);
}

typedef Result(UserAddress) (*ProcessUserAllocator)(Address virtual_address, size_t size, bool read, bool write,
                                                    bool execute);

static Result(UserAddress)
    Process__install_dynamic_allocation(ProcessPtr self, UserAddress allocation, size_t allocation_size)
{
    PageTable__map(&self->page_table, self->kernel, allocation, allocation_size);

    match_result(
        Process__record_user_allocation(self, allocation, allocation_size, USER_ALLOCATION_DYNAMIC),
        success(_)
        {
            return Success(UserAddress, allocation);
        },
        error(code, message)
        {
            PageTable__unmap(&self->page_table, self->kernel, allocation.virtual_addr, allocation_size);
            Kernel__free(allocation.kernel_addr.virtual_addr);
            return Error(UserAddress, message, code);
        }
    );

    return Error(UserAddress, user_allocation_full, PROCESS_MEMORY_FULL);
}

static Result(UserAddress) Process__alloc_user_pages_with(ProcessPtr self, size_t size, ProcessUserAllocator allocator)
{
    if (size == 0 || size > SIZE_MAX - (PAGE_SIZE - 1))
    {
        return Error(UserAddress, user_allocation_full, PROCESS_MEMORY_INVALID_SIZE);
    }
    size_t allocation_size = Process__page_aligned_size(size);
    Address virtual_address;
    match_optional(
        Process__find_free_user_range(self, allocation_size, PAGE_SIZE),
        some(found)
        {
            virtual_address = found;
        },
        none()
        {
            return Error(UserAddress, user_allocation_full, PROCESS_MEMORY_FULL);
        }
    );

    match_result(
        allocator(virtual_address, allocation_size, true, true, false),
        success(allocation)
        {
            return Process__install_dynamic_allocation(self, allocation, allocation_size);
        },
        error(code, message)
        {
            return Error(UserAddress, message, PROCESS_MEMORY_FULL);
        }
    );

    return Error(UserAddress, user_allocation_full, PROCESS_MEMORY_FULL);
}

Result(UserAddress) Process__alloc_user_pages(ProcessPtr self, size_t size)
{
    return Process__alloc_user_pages_with(self, size, Kernel__try_alloc_user);
}

Result(UserAddress) Process__alloc_aligned_user_pages(ProcessPtr self, size_t size, size_t alignment)
{
    if (size == 0 || size > SIZE_MAX - (PAGE_SIZE - 1) || !Process__alignment_is_valid(alignment))
    {
        return Error(UserAddress, invalid_user_allocation, PROCESS_MEMORY_INVALID_SIZE);
    }

    size_t allocation_size = Process__page_aligned_size(size);
    Address virtual_address;
    match_optional(
        Process__find_free_user_range(self, allocation_size, alignment),
        some(found)
        {
            virtual_address = found;
        },
        none()
        {
            return Error(UserAddress, user_allocation_full, PROCESS_MEMORY_FULL);
        }
    );

    match_result(
        Kernel__try_alloc_user_aligned(virtual_address, allocation_size, alignment, true, true, false),
        success(allocation)
        {
            return Process__install_dynamic_allocation(self, allocation, allocation_size);
        },
        error(code, message)
        {
            return Error(UserAddress, message, PROCESS_MEMORY_FULL);
        }
    );

    return Error(UserAddress, user_allocation_full, PROCESS_MEMORY_FULL);
}

Result(UserAddress) Process__alloc_user_pages_at(ProcessPtr self, Address virtual_address, size_t size)
{
    if (size == 0 || size > SIZE_MAX - (PAGE_SIZE - 1))
    {
        return Error(UserAddress, invalid_user_allocation, PROCESS_MEMORY_INVALID_SIZE);
    }

    size_t allocation_size = Process__page_aligned_size(size);
    if (!Process__user_range_is_free(self, virtual_address, allocation_size))
    {
        return Error(UserAddress, invalid_user_allocation, PROCESS_MEMORY_NOT_FOUND);
    }

    match_result(
        Kernel__try_alloc_user(virtual_address, allocation_size, true, true, false),
        success(allocation)
        {
            return Process__install_dynamic_allocation(self, allocation, allocation_size);
        },
        error(code, message)
        {
            return Error(UserAddress, message, PROCESS_MEMORY_FULL);
        }
    );

    return Error(UserAddress, user_allocation_full, PROCESS_MEMORY_FULL);
}

static Optional(UserAddress) Process__protected_address(UserAddress address, MemPagePermissions permissions)
{
    switch (permissions)
    {
    case MEM_PAGE_NONE:
        return Some(UserAddress,
                    UserAddress__new(address.virtual_addr, address.kernel_addr, false, false, false, true));
    case MEM_PAGE_READ_ONLY:
        return Some(UserAddress, UserAddress__new(address.virtual_addr, address.kernel_addr, true, false, false, true));
    case MEM_PAGE_READ_WRITE:
        return Some(UserAddress, UserAddress__new(address.virtual_addr, address.kernel_addr, true, true, false, true));
    case MEM_PAGE_READ_EXECUTE:
        return Some(UserAddress, UserAddress__new(address.virtual_addr, address.kernel_addr, true, false, true, true));
    }
    return None(UserAddress);
}

Result(ProcessMemoryCode)
    Process__protect_user_pages(ProcessPtr self, Address virtual_address, size_t size, MemPagePermissions permissions)
{
    if (virtual_address % PAGE_SIZE != 0 || size == 0 || size % PAGE_SIZE != 0)
    {
        return Error(ProcessMemoryCode, invalid_user_allocation, PROCESS_MEMORY_NOT_FOUND);
    }
    UserAllocationRangeMatch range = UserAllocationRangeMatch__new(virtual_address, size);
    match_find_result_ptr(
        UserAllocations__find(&self->user_allocations, allocation_contains_range, &range),
        some(allocation)
        {
            if (allocation->type != USER_ALLOCATION_DYNAMIC)
            {
                return Error(ProcessMemoryCode, invalid_user_allocation, PROCESS_MEMORY_NOT_FOUND);
            }
            UserAddress range_address =
                UserAddress__relative(&allocation->address, virtual_address - allocation->address.virtual_addr);
            match_optional(
                Process__protected_address(range_address, permissions),
                some(address)
                {
                    UserMappings replacement = UserMappings__new();
                    Address protected_end = virtual_address + size;
                    size_t protected_size = 0;
                    for_each_ll(
                        UserMappings,
                        self->user_mappings,
                        mapping,
                        {
                            Address mapping_start = mapping.address.virtual_addr;
                            Address mapping_end = mapping_start + mapping.size;
                            if (mapping_end <= virtual_address || mapping_start >= protected_end)
                            {
                                match_result(
                                    UserMappings__push(&replacement, mapping),
                                    success_do_nothing(),
                                    error(code, message)
                                    {
                                        UserMappings__dispose(&replacement);
                                        return Error(ProcessMemoryCode, user_allocation_full, PROCESS_MEMORY_FULL);
                                    }
                                );
                                continue;
                            }
                            Address overlap_start = mapping_start > virtual_address ? mapping_start : virtual_address;
                            Address overlap_end = mapping_end < protected_end ? mapping_end : protected_end;
                            if (mapping_start < overlap_start)
                            {
                                UserMapping before = UserMapping__new(mapping.address, overlap_start - mapping_start);
                                match_result(
                                    UserMappings__push(&replacement, before),
                                    success_do_nothing(),
                                    error(code, message)
                                    {
                                        UserMappings__dispose(&replacement);
                                        return Error(ProcessMemoryCode, user_allocation_full, PROCESS_MEMORY_FULL);
                                    }
                                );
                            }
                            UserAddress overlap_address =
                                UserAddress__relative(&address, overlap_start - virtual_address);
                            UserMapping overlap = UserMapping__new(overlap_address, overlap_end - overlap_start);
                            match_result(
                                UserMappings__push(&replacement, overlap),
                                success_do_nothing(),
                                error(code, message)
                                {
                                    UserMappings__dispose(&replacement);
                                    return Error(ProcessMemoryCode, user_allocation_full, PROCESS_MEMORY_FULL);
                                }
                            );
                            protected_size += overlap.size;
                            if (overlap_end < mapping_end)
                            {
                                UserAddress after_address =
                                    UserAddress__relative(&mapping.address, overlap_end - mapping_start);
                                UserMapping after = UserMapping__new(after_address, mapping_end - overlap_end);
                                match_result(
                                    UserMappings__push(&replacement, after),
                                    success_do_nothing(),
                                    error(code, message)
                                    {
                                        UserMappings__dispose(&replacement);
                                        return Error(ProcessMemoryCode, user_allocation_full, PROCESS_MEMORY_FULL);
                                    }
                                );
                            }
                        }
                    );
                    if (protected_size != size)
                    {
                        UserMappings__dispose(&replacement);
                        return Error(ProcessMemoryCode, invalid_user_allocation, PROCESS_MEMORY_NOT_FOUND);
                    }
                    if (address.execute)
                    {
                        Arch__sync_instruction_memory(address.kernel_addr.virtual_addr, size);
                    }
                    PageTable__protect(&self->page_table, self->kernel, address, size);
                    UserMappings previous = self->user_mappings;
                    self->user_mappings = replacement;
                    UserMappings__dispose(&previous);
                    if (virtual_address == allocation->address.virtual_addr && size == allocation->size)
                    {
                        allocation->address = address;
                    }
                    return Success(ProcessMemoryCode, PROCESS_MEMORY_OK);
                },
                none()
                {
                    return Error(ProcessMemoryCode, invalid_user_permissions, PROCESS_MEMORY_INVALID_PERMISSIONS);
                }
            );
        },
        none()
        {
            return Error(ProcessMemoryCode, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
        }
    );

    return Error(ProcessMemoryCode, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
}

Result(UserAddress) Process__alloc_reclaimable_user_pages(ProcessPtr self, size_t size)
{
    return Process__alloc_user_pages_with(self, size, Kernel__try_alloc_reclaimable_user);
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
    if (source_offset + size < size || source_offset + size > SIZE_MAX - (PAGE_SIZE - 1))
    {
        return Error(SharedUserPages, user_allocation_full, PROCESS_MEMORY_INVALID_SIZE);
    }

    size_t allocation_size = Process__page_aligned_size(source_offset + size);
    Address target_base;
    match_optional(
        Process__find_free_user_range(self, allocation_size, PAGE_SIZE),
        some(found)
        {
            target_base = found;
        },
        none()
        {
            return Error(SharedUserPages, user_allocation_full, PROCESS_MEMORY_FULL);
        }
    );
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
            match_optional(
                source->ipc_doorbell,
                some(doorbell)
                {
                    if (doorbell.virtual_addr == allocation_match->address.virtual_addr)
                    {
                        return Error(SharedUserPages, static_user_allocation, PROCESS_MEMORY_STATIC_ALLOCATION);
                    }
                },
                none_do_nothing()
            );
            Offset backing_offset = source_base - allocation_match->address.virtual_addr;
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

static Result(UserAddress)
    Process__map_mmio_internal(ProcessPtr self, Address physical, size_t size, bool install_page_mapping)
{
    Address physical_base = Process__page_base(physical);
    Address physical_offset = physical - physical_base;
    if (size == 0 || size + physical_offset < size || size + physical_offset > SIZE_MAX - (PAGE_SIZE - 1))
    {
        return Error(UserAddress, user_allocation_full, PROCESS_MEMORY_INVALID_SIZE);
    }

    size_t allocation_size = Process__page_aligned_size(size + physical_offset);

    Address virtual_base;
    match_optional(
        Process__find_free_user_range(self, allocation_size, PAGE_SIZE),
        some(found)
        {
            virtual_base = found;
        },
        none()
        {
            return Error(UserAddress, user_allocation_full, PROCESS_MEMORY_FULL);
        }
    );
    Address virtual_address = virtual_base + physical_offset;

    UserAddress mapping = UserAddress__new(virtual_address, KERNEL_ADDR(physical, physical), true, true, false, true);
    UserAddress allocation =
        UserAddress__new(virtual_base, KERNEL_ADDR(physical_base, physical_base), true, true, false, true);

    if (install_page_mapping)
    {
        PageTable__map_mmio(&self->page_table, self->kernel, mapping, size);
    }

    match_result(
        Process__record_user_allocation(self, allocation, allocation_size, USER_ALLOCATION_MMIO),
        success(_)
        {
            return Success(UserAddress, mapping);
        },
        error(code, message)
        {
            return Error(UserAddress, message, code);
        }
    );

    return Error(UserAddress, user_allocation_full, PROCESS_MEMORY_FULL);
}

Result(UserAddress) Process__map_mmio(ProcessPtr self, Address physical, size_t size)
{
    return Process__map_mmio_internal(self, physical, size, true);
}

Result(UserAddress) Process__map_trapped_mmio(ProcessPtr self, Address physical, size_t size)
{
    return Process__map_mmio_internal(self, physical, size, false);
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
            for_each_fa_ptr(
                ProcessVirtioDevices,
                &self->virtio_devices,
                device_index,
                device,
                {
                    if (allocation.type == USER_ALLOCATION_MMIO &&
                        device >= allocation.address.virtual_addr &&
                        device - allocation.address.virtual_addr < allocation.size)
                    {
                        return Error(ProcessMemoryCode, user_allocation_full, PROCESS_MEMORY_FREE_FAILED);
                    }
                }
            );
            __d_printf(MEMORY, "process free pid=%i name=%s type=%s user=%p kernel=%p phys=%p size=%i tracked=%i\n",
                       self->pid, cstr(self->name), UserAllocationType__name(allocation.type),
                       allocation.address.virtual_addr, allocation.address.kernel_addr.virtual_addr,
                       allocation.address.kernel_addr.phys_addr, allocation.size,
                       UserAllocations__len(&self->user_allocations));
            PageTable__unmap(&self->page_table, self->kernel, allocation.address.virtual_addr, allocation.size);
            Address kernel_virtual = allocation.address.kernel_addr.virtual_addr;

            ListIndex mapping_index = 0;
            Address allocation_end = allocation.address.virtual_addr + allocation.size;
            while ((size_t)mapping_index < UserMappings__len(&self->user_mappings))
            {
                UserMapping mapping = unwrap_optional(UserMappings__at(&self->user_mappings, mapping_index),
                                                      UserMapping__new(NULL_USER_ADDRESS, 0));
                if (mapping.address.virtual_addr >= allocation.address.virtual_addr &&
                    mapping.address.virtual_addr < allocation_end)
                {
                    UserMappings__remove_at(&self->user_mappings, mapping_index);
                }
                else
                {
                    mapping_index++;
                }
            }

            UserAllocations__remove_at(&self->user_allocations, index);

            if (allocation.type == USER_ALLOCATION_MMIO || allocation.type == USER_ALLOCATION_SHARED)
            {
                return Success(ProcessMemoryCode, PROCESS_MEMORY_OK);
            }

            match_result(
                allocation.type == USER_ALLOCATION_DOORBELL ? DoorbellBacking__release(allocation.address)
                                                            : Kernel__free(kernel_virtual),
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
        self->ipc_doorbell,
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
    self->ipc_doorbell = None(UserAddress);
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

    match_result(
        Process__startup_endpoint(self, SpawnStdio__stdin_fd(*requested)),
        success(input)
        {
            match_result(
                Process__startup_endpoint(self, SpawnStdio__stdout_fd(*requested)),
                success(output)
                {
                    match_result(
                        Process__startup_endpoint(self, SpawnStdio__stderr_fd(*requested)),
                        success(error_output)
                        {
                            return Success(SpawnStdio, SpawnStdio__new(input, output, error_output));
                        },
                        error(code, message)
                        {
                            return Error(SpawnStdio, message, code);
                        }
                    );
                    return Error(SpawnStdio, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
                },
                error(code, message)
                {
                    return Error(SpawnStdio, message, code);
                }
            );
            return Error(SpawnStdio, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
        },
        error(code, message)
        {
            return Error(SpawnStdio, message, code);
        }
    );
    return Error(SpawnStdio, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

void Process__prepare_startup_stdio(ProcessPtr self)
{
    Process__set_startup_stdio(self, SpawnStdio__stdin_fd(self->startup_stdio),
                               SpawnStdio__stdout_fd(self->startup_stdio), SpawnStdio__stderr_fd(self->startup_stdio));
}
