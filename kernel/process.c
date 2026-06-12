#include "process.h"
#include "scheduler/parked.h"
#include "bin/elf.h"
#include "arch/page_table.h"
#include "arch/process.h"
#include "arch/process_context.h"
#include "types/kstring.h"
#include "types/result.h"
#include "types/optional.h"
#include "../common_includes/syscalls/spawn.h"

#include "kalloc.h"
#include "stack.h"

FixedArrayBodyDefine(ProcessTable);
LinkedListBodyDefine(UserAllocations);
LinkedListBodyDefine(ProcessFileDescriptors);

static ProcessID pid_counter = 0;
static mutex_t pid_lock = MUTEX_INIT;

static KString no_entry_point_message = KSTRING_NEW("No entry point found in ELF");
static KString user_allocation_not_found = KSTRING_NEW("User allocation not found");
static KString static_user_allocation = KSTRING_NEW("Cannot free static user allocation");
static KString user_allocation_full = KSTRING_NEW("Too many user allocations");
static KString invalid_heap_break = KSTRING_NEW("Invalid heap break");
static KString file_descriptor_full = KSTRING_NEW("Too many file descriptors");
static KString file_descriptor_not_found = KSTRING_NEW("File descriptor not found");
static KString argument_too_long = KSTRING_NEW("Argument too long");

static size_t Process__page_aligned_size(size_t size)
{
    return (size + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
}

bool UserAllocation__equals(UserAllocationPtr self, UserAllocationPtr other)
{
    return self->address.virtual_addr == other->address.virtual_addr;
}

bool ProcessFileDescriptor__equals(ProcessFileDescriptorPtr self, ProcessFileDescriptorPtr other)
{
    return self->fd == other->fd;
}

static bool allocation_matches_virtual(UserAllocationPtr allocation, void * context)
{
    Address virtual_address = *(Address *) context;
    return allocation->address.virtual_addr == virtual_address;
}

static bool file_descriptor_matches_fd(ProcessFileDescriptorPtr descriptor, void * context)
{
    FileDescriptor fd = *(FileDescriptor *) context;
    return descriptor->fd == fd;
}

static ProcessFileDescriptors Process__inherit_file_descriptors(Optional(ProcessPtr) parent)
{
    ProcessFileDescriptors descriptors = ProcessFileDescriptors__new();
    Match(ProcessPtr, parent)
        IsSome(parent_process,
        {
            LLIterate(ProcessFileDescriptors, parent_process->file_descriptors, descriptor)
                ProcessFileDescriptors__push(&descriptors, descriptor);
            End
            return descriptors;
        })
    End

    return descriptors;
}

static FileDescriptor Process__inherit_next_file_descriptor(Optional(ProcessPtr) parent)
{
    Match(ProcessPtr, parent)
        IsSome(parent_process, return parent_process->next_file_descriptor)
    End

    return 3;
}

static size_t Process__copy_arg(char * target, KStringPtr arg)
{
    for (size_t i = 0; i < arg->len; i++)
    {
        target[i] = arg->data[i];
    }
    target[arg->len] = '\0';
    return arg->len + 1;
}

static Result(Address) Process__setup_args(
    ProcessPtr self,
    KString arg0,
    Optional(KString) arg1,
    uint64_t * argc
)
{
    ResultMatch(UserAddress, Process__alloc_user_pages(self, PAGE_SIZE))
        IsSuccess(args_page,
        {
            Address user_base = args_page.virtual_addr;
            Address kernel_base = args_page.kernel_addr.virtual_addr;
            Address * argv = ptr(kernel_base, Address);

            *argc = 1;
            Match(KString, arg1)
                IsSome(_, *argc = 2)
            End

            size_t cursor = sizeof(Address) * (*argc + 1);
            if (cursor + arg0.len + 1 > PAGE_SIZE)
            {
                return Error(Address, argument_too_long, SPAWN_ARG_TOO_LONG);
            }

            argv[0] = user_base + cursor;
            cursor += Process__copy_arg(ptr(kernel_base + cursor, char), &arg0);

            Match(KString, arg1)
                IsSome(arg,
                {
                    if (cursor + arg.len + 1 > PAGE_SIZE)
                    {
                        return Error(Address, argument_too_long, SPAWN_ARG_TOO_LONG);
                    }
                    argv[1] = user_base + cursor;
                    cursor += Process__copy_arg(ptr(kernel_base + cursor, char), &arg);
                })
            End

            argv[*argc] = 0;
            return Success(Address, user_base);
        })
        IsError(code, message, return Error(Address, message, code))
    End

    return Error(Address, argument_too_long, SPAWN_ARG_TOO_LONG);
}

Result(ProcessPtr) Process__new(
    KString name,
    KernelAddress page_table,
    UserAddress entry,
    Stack stack,
    Optional(ProcessPtr) parent,
    KString arg0,
    Optional(KString) arg1
)
{
    ProcessID pid;

    CriticalSection(pid_lock, pid = ++pid_counter);

    __d_printf(PROCESS, "Creating process with entry point %p\n", entry);
    ProcessPtr proc = ptr(Kernel__alloc_kernel(sizeof(Process)), Process);
    proc->entry_point = entry;
    proc->page_table = page_table;
    proc->state = PROC_STATE_NEW;
    proc->pid = pid;
    proc->stack = stack;
    proc->last_tick = 0;
    proc->name = name;
    proc->parked_context = None(ParkedKernelContextPtr);
    proc->parent = parent;
    proc->heap_break = USER_HEAP_BASE;
    proc->heap_mapped_end = USER_HEAP_BASE;
    proc->allocation_next = USER_ALLOC_BASE;
    proc->user_allocations = UserAllocations__new();
    proc->file_descriptors = Process__inherit_file_descriptors(parent);
    proc->next_file_descriptor = Process__inherit_next_file_descriptor(parent);

    uint64_t argc;
    ResultMatch(Address, Process__setup_args(proc, arg0, arg1, &argc))
        IsSuccess(argv,
        {
            proc->context = ProcessContext__init(proc->pid, &proc->stack, entry, page_table, argc, argv);
            return Success(ProcessPtr, proc);
        })
        IsError(code, message, return Error(ProcessPtr, message, code))
    End

    return Error(ProcessPtr, argument_too_long, SPAWN_ARG_TOO_LONG);
}

Result(ProcessPtr) Process__setup(Elf binary, Optional(ProcessPtr) parent)
{
    return Process__setup_with_args(binary, parent, None(KString));
}

Result(ProcessPtr) Process__setup_with_args(Elf binary, Optional(ProcessPtr) parent, Optional(KString) arg1)
{
    __d_println(PROCESS, "Process__spawn_from_elf");
    ExpandedSegmentArray expanded_elf = Elf__map(binary);
    __d_printf(PROCESS, "ELF: %s %x %x\n", cstr(binary.name), binary.start, binary.end);
    __d_printf(PROCESS, "ELF mapped %i\n", ExpandedSegmentArray__len(&expanded_elf));

    __d_println(PROCESS, "Creating page table");
    KernelAddress page_table = PageTable__new();

    __d_println(PROCESS, "Page table created");

    FAIterate(ExpandedSegmentArray, expanded_elf, i, segment)
        UserAddress addr = segment.address;
        __d_printf(MMU, "Mapping segment %i phys=%p, virt=%p\n", i, addr.kernel_addr.phys_addr, addr.virtual_addr);
        PageTable__map(v_ptr(page_table, PageTable), segment.address, segment.size);
    End
    __d_println(PROCESS, "Code mapped");

    Stack stack = Stack__new();
    Stack__map(&stack, v_ptr(page_table, PageTable));
    __d_println(PROCESS, "Stack mapped");

    Match(ExpandedSegment, ExpandedSegmentArray__at(&expanded_elf, 0))
        IsNone(
        {
            __d_println(PANIC, cstr(no_entry_point_message));
            return Error(ProcessPtr, no_entry_point_message, PROCESS_FAILED_TO_SETUP);
        });
        IsSome(entry_point,
        {
            ResultMatch(ProcessPtr, Process__new(binary.name, page_table, entry_point.address, stack, parent, binary.name, arg1))
                IsSuccess(proc,
                {
                    FAIterate(ExpandedSegmentArray, expanded_elf, segment_index, segment)
                        Process__record_user_allocation(proc, segment.address, segment.size, USER_ALLOCATION_ELF);
                    End
                    Process__record_user_allocation(proc, stack.base, STACK_SIZE, USER_ALLOCATION_STACK);
                    return Success(ProcessPtr, proc);
                })
                IsError(code, message, return Error(ProcessPtr, message, code))
            End
        })
    End

    return Error(ProcessPtr, no_entry_point_message, PROCESS_FAILED_TO_SETUP);
}

Result(ProcessMemoryCode) Process__record_user_allocation(
    ProcessPtr self, UserAddress address, size_t size, UserAllocationType type
)
{
    UserAllocation allocation = (UserAllocation) {
        .address = address,
        .size = Process__page_aligned_size(size),
        .type = type,
    };

    ResultMatch(CollectionCode, UserAllocations__push(&self->user_allocations, allocation))
        IsSuccess(_, return Success(ProcessMemoryCode, PROCESS_MEMORY_OK));
        IsError(code, message, return Error(ProcessMemoryCode, user_allocation_full, PROCESS_MEMORY_FULL));
    End

    return Error(ProcessMemoryCode, user_allocation_full, PROCESS_MEMORY_FULL);
}

Result(UserAddress) Process__alloc_user_pages(ProcessPtr self, size_t size)
{
    size_t allocation_size = Process__page_aligned_size(size);
    Address virtual_address = self->allocation_next;

    UserAddress allocation = Kernel__alloc_user(virtual_address, allocation_size, true, true, false);
    PageTable__map(v_ptr(self->page_table, PageTable), allocation, allocation_size);

    ResultMatch(ProcessMemoryCode, Process__record_user_allocation(
        self, allocation, allocation_size, USER_ALLOCATION_DYNAMIC
    ))
        IsSuccess(_,
        {
            self->allocation_next += allocation_size;
            return Success(UserAddress, allocation);
        });
        IsError(code, message, return Error(UserAddress, message, code));
    End

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

        PageTable__map(v_ptr(self->page_table, PageTable), allocation, allocation_size);
        ResultMatch(ProcessMemoryCode, Process__record_user_allocation(
            self, allocation, allocation_size, USER_ALLOCATION_HEAP
        ))
            IsSuccess(_,
            {
                self->heap_mapped_end = mapped_end;
            });
            IsError(code, message, return Error(Address, message, code));
        End
    }

    self->heap_break = new_break;
    return Success(Address, old_break);
}

static Result(ProcessMemoryCode) Process__free_user_allocation_at(ProcessPtr self, ListIndex index)
{
    Match(UserAllocation, UserAllocations__at(&self->user_allocations, index))
        IsSome(allocation,
        {
            PageTable__unmap(v_ptr(self->page_table, PageTable), allocation.address.virtual_addr, allocation.size);
            Address kernel_virtual = allocation.address.kernel_addr.virtual_addr;

            UserAllocations__remove_at(&self->user_allocations, index);

            ResultMatch(KAllocCode, Kernel__free(kernel_virtual))
                IsError(code, message, return Error(ProcessMemoryCode, message, PROCESS_MEMORY_FREE_FAILED));
            End

            return Success(ProcessMemoryCode, PROCESS_MEMORY_OK);
        })
    End

    return Error(ProcessMemoryCode, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
}

Result(ProcessMemoryCode) Process__free_user_pages(ProcessPtr self, Address virtual_address)
{
    LLFindResultMatch(UserAllocations, UserAllocations__find(
        &self->user_allocations, allocation_matches_virtual, &virtual_address
    ))
        IsSome(found,
        {
            if (found.value->type != USER_ALLOCATION_DYNAMIC)
            {
                return Error(ProcessMemoryCode, static_user_allocation, PROCESS_MEMORY_STATIC_ALLOCATION);
            }

            return Process__free_user_allocation_at(self, found.index);
        })
        IsNone(return Error(ProcessMemoryCode, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND))
    End

    return Error(ProcessMemoryCode, user_allocation_not_found, PROCESS_MEMORY_NOT_FOUND);
}

Result(ProcessMemoryCode) Process__dispose_user_allocations(ProcessPtr self)
{
    while (self->user_allocations.len > 0)
    {
        ResultMatch(ProcessMemoryCode, Process__free_user_allocation_at(self, 0))
            IsError(code, message, return Error(ProcessMemoryCode, message, code));
        End
    }

    return Success(ProcessMemoryCode, PROCESS_MEMORY_OK);
}

Result(FileDescriptor) Process__set_file_descriptor(ProcessPtr self, FileDescriptor fd, ProcessIOHandle handle)
{
    LLFindResultMatch(ProcessFileDescriptors, ProcessFileDescriptors__find(
        &self->file_descriptors, file_descriptor_matches_fd, &fd
    ))
        IsSome(found,
        {
            found.value->handle = handle;
            if (fd >= self->next_file_descriptor)
            {
                self->next_file_descriptor = fd + 1;
            }
            return Success(FileDescriptor, fd);
        })
    End

    ProcessFileDescriptor descriptor = (ProcessFileDescriptor) {
        .fd = fd,
        .handle = handle,
    };

    ResultMatch(CollectionCode, ProcessFileDescriptors__push(&self->file_descriptors, descriptor))
        IsSuccess(_, {
            if (fd >= self->next_file_descriptor)
            {
                self->next_file_descriptor = fd + 1;
            }
            return Success(FileDescriptor, fd);
        });
        IsError(code, message, return Error(FileDescriptor, file_descriptor_full, IO_REGISTER_FAILED));
    End

    return Error(FileDescriptor, file_descriptor_full, IO_REGISTER_FAILED);
}

Result(FileDescriptor) Process__allocate_file_descriptor(ProcessPtr self, ProcessIOHandle handle)
{
    FileDescriptor fd = self->next_file_descriptor;
    while (fd < MAX_HANDLES_PER_PROCESS)
    {
        LLFindResultMatch(ProcessFileDescriptors, ProcessFileDescriptors__find(
            &self->file_descriptors, file_descriptor_matches_fd, &fd
        ))
            IsNone(return Process__set_file_descriptor(self, fd, handle))
        End

        fd++;
    }

    return Error(FileDescriptor, file_descriptor_full, IO_REGISTER_FAILED);
}

Result(ProcessIOHandle) Process__get_io_handle(ProcessPtr self, FileDescriptor fd)
{
    LLFindResultMatch(ProcessFileDescriptors, ProcessFileDescriptors__find(
        &self->file_descriptors, file_descriptor_matches_fd, &fd
    ))
        IsSome(found, return Success(ProcessIOHandle, found.value->handle))
        IsNone(return Error(ProcessIOHandle, file_descriptor_not_found, IO_NOT_OPEN))
    End

    return Error(ProcessIOHandle, file_descriptor_not_found, IO_NOT_OPEN);
}

Result(ProcessIOHandle) Process__remove_file_descriptor(ProcessPtr self, FileDescriptor fd)
{
    ResultMatch(ProcessIOHandle, Process__get_io_handle(self, fd))
        IsSuccess(handle,
        {
            LLFindResultMatch(ProcessFileDescriptors, ProcessFileDescriptors__find(
                &self->file_descriptors, file_descriptor_matches_fd, &fd
            ))
                IsSome(found, ProcessFileDescriptors__remove_at(&self->file_descriptors, found.index))
            End
            if (fd < self->next_file_descriptor)
            {
                self->next_file_descriptor = fd;
            }
            return Success(ProcessIOHandle, handle);
        });
        IsError(code, message, return Error(ProcessIOHandle, message, code));
    End

    return Error(ProcessIOHandle, file_descriptor_not_found, IO_NOT_OPEN);
}
