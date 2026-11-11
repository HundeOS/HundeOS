#include "thread_local.h"
#include "thread_local_arch.h"
#include "mem.h"
#include "syscall.h"
#include "../newlib/thread_state.h"

#include <string.h>

typedef struct ThreadLocalControl
{
    uint64_t reserved;
    Address state;
} ThreadLocalControl;

typedef ThreadLocalControl * ThreadLocalControlPtr;

typedef struct ThreadLocalState
{
    ThreadLocalBlock block;
    ThreadLocalImage image;
    Address libc_state;
} ThreadLocalState;

typedef ThreadLocalState * ThreadLocalStatePtr;

static ThreadLocalBlock ThreadLocalBlock__new(Address allocation, Address pointer)
{
    return (ThreadLocalBlock){.allocation = allocation, .thread_pointer = pointer};
}

static ThreadLocalState ThreadLocalState__new(ConstThreadLocalBlockPtr block, ConstThreadLocalImagePtr image,
                                              Address libc_state)
{
    return (ThreadLocalState){.block = *block, .image = *image, .libc_state = libc_state};
}

static ThreadLocalControl ThreadLocalControl__new(Address state)
{
    return (ThreadLocalControl){.reserved = 0, .state = state};
}

static Address ThreadLocal__state_address(void)
{
    Address pointer = ThreadLocalArch__pointer();
    if (pointer == 0)
    {
        return 0;
    }
    ThreadLocalControlPtr control = (ThreadLocalControlPtr)(pointer - ThreadLocalArch__control_prefix_size());
    return control->state;
}

Address ThreadLocal__libc_state(void)
{
    Address state_address = ThreadLocal__state_address();
    if (state_address == 0)
    {
        return 0;
    }
    ThreadLocalStatePtr state = (ThreadLocalStatePtr)state_address;
    return state->libc_state;
}

Result(ThreadLocalBlock) ThreadLocal__new(ConstThreadLocalImagePtr image)
{
    if (image->alignment == 0 || image->alignment > 65536 || (image->alignment & (image->alignment - 1)) != 0 ||
        image->initialized_size > image->size || image->size > 16 * 1024 * 1024 ||
        (image->initialized_size > 0 && image->address == 0))
    {
        return Error(ThreadLocalBlock, bstr("Invalid thread-local image"), THREAD_OPERATION_INVALID);
    }
    size_t alignment = image->alignment < 16 ? 16 : image->alignment;
    size_t libc_offset = (sizeof(ThreadLocalState) + 15) & ~(size_t)15;
    size_t libc_size = LibC__thread_state_size();
    size_t control_prefix = ThreadLocalArch__control_prefix_size();
    if (libc_size > SIZE_MAX - libc_offset - 2 * alignment - sizeof(ThreadLocalControl) - image->size - 1)
    {
        return Error(ThreadLocalBlock, bstr("Thread-local state too large"), THREAD_OPERATION_INVALID);
    }
    size_t pointer_offset = (libc_offset + libc_size + control_prefix + alignment - 1) & ~(alignment - 1);
    size_t control_suffix = sizeof(ThreadLocalControl) - control_prefix;
    size_t image_offset = pointer_offset + ((control_suffix + image->alignment - 1) & ~(image->alignment - 1));
    size_t allocation_size = image_offset + (image->size > 0 ? image->size : 1);
    size_t allocation_alignment = alignment > 4096 ? alignment : 4096;
    match_result(
        mem_alloc_aligned_pages_result(allocation_size, allocation_alignment),
        success(allocation)
        {
            memset((void *)allocation, 0, allocation_size);
            if (image->initialized_size > 0)
            {
                memcpy((void *)(allocation + image_offset), (const void *)image->address, image->initialized_size);
            }
            ThreadLocalBlock block = ThreadLocalBlock__new(allocation, allocation + pointer_offset);
            ThreadLocalStatePtr state = (ThreadLocalStatePtr)allocation;
            *state = ThreadLocalState__new(&block, image, allocation + libc_offset);
            ThreadLocalControlPtr control = (ThreadLocalControlPtr)(block.thread_pointer - control_prefix);
            *control = ThreadLocalControl__new(allocation);
            return Success(ThreadLocalBlock, block);
        },
        error(code, message)
        {
            return Error(ThreadLocalBlock, message, THREAD_OPERATION_NO_MEMORY);
        }
    );
    return Error(ThreadLocalBlock, bstr("Thread-local allocation failed"), THREAD_OPERATION_NO_MEMORY);
}

Result(ThreadLocalBlock) ThreadLocal__new_thread(void)
{
    Address state_address = ThreadLocal__state_address();
    if (state_address == 0)
    {
        return Error(ThreadLocalBlock, bstr("Thread-local runtime not initialized"), THREAD_OPERATION_INVALID);
    }
    ThreadLocalStatePtr state = (ThreadLocalStatePtr)state_address;
    return ThreadLocal__new(&state->image);
}

ThreadLocalStatus ThreadLocal__release(ConstThreadLocalBlockPtr block)
{
    return mem_free_pages((void *)block->allocation);
}

ThreadLocalStatus ThreadLocal__initialize(void)
{
    if (ThreadLocalArch__pointer() != 0)
    {
        return THREAD_OPERATION_OK;
    }
    ThreadLocalImage image = ThreadLocalImage__new(0, 0, 0, 1);
    int64_t status = (int64_t)syscall(SYSCALL_THREAD_LOCAL_IMAGE, (Anything)&image, 0, 0, 0, 0);
    if (status != THREAD_OPERATION_OK)
    {
        return status;
    }
    match_result(
        ThreadLocal__new(&image),
        success(block)
        {
            ThreadLocalArch__set_pointer(block.thread_pointer);
            return THREAD_OPERATION_OK;
        },
        error(code, message)
        {
            return code;
        }
    );
    return THREAD_OPERATION_NO_MEMORY;
}

ThreadLocalStatus ThreadLocal__destroy_current(void)
{
    Address state_address = ThreadLocal__state_address();
    if (state_address == 0)
    {
        return THREAD_OPERATION_OK;
    }
    ThreadLocalStatePtr state = (ThreadLocalStatePtr)state_address;
    ThreadLocalBlock block = state->block;
    ThreadLocalArch__set_pointer(0);
    return ThreadLocal__release(&block);
}
