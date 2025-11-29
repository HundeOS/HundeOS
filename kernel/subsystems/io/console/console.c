#include "console.h"
#include "debug.h"
#include "../io.h"
#include "../../../types/result.h"
#include "../../../types/kstring.h"
#include "scheduler/scheduler.h"
#include "subsystems/io/io_subsystem.h"

FixedArrayBodyDefine(ParkedContexts);

static Result(IOInternalHandle) Console__open(IOStatePtr state, KString path, Flags flags)
{
    ConsoleStatePtr self = (ConsoleStatePtr) state;
    IOInternalHandle handle = -1;
    CriticalSection(self->lock,
    {
        handle = ++self->handle_counter;
    });
    return Success(IOInternalHandle, handle);
}

bool io_context_equals_by_handle(void * a, void * b)
{
    IOContextPtr a_context = (IOContextPtr) a;
    IOContextPtr b_context = (IOContextPtr) b;

    Match(IOHandle, a_context->handle)
        IsSome(parked_handle, return parked_handle == Unwrap(IOHandle, b_context->handle, -1);)
    End

    return false;
}

static Result(IOInternalHandle) Console__close(IOStatePtr state, IOInternalHandle handle)
{
    ConsoleStatePtr self = (ConsoleStatePtr) state;
    IOBuffer__reset(&self->read_buffer);
    IOBuffer__reset(&self->write_buffer);

    KernelContext dummy_kernel_context = KernelContext__new(
        None(ProcessPtr), None(ProcessID), None(SchedulerPtr));

    IOContext test_context = IOContext__new(dummy_kernel_context,
        None(IOSubsystemPtr), Some(IOHandle, handle), None(ExternalBuffer));

    ResultMatch(ArrayCode, ParkedContexts__remove(&self->parked_contexts, test_context))
        IsError(code, message, __d_println(PANIC, "no parked context found for handle"));
    End

    return Success(IOInternalHandle, handle);
}

static Result(IOBufferPtr) Console__read(IOStatePtr state, IOInternalHandle handle, size_t size)
{
    ConsoleStatePtr self = (ConsoleStatePtr) state;
    if (IOBuffer__item_count(&self->read_buffer) == 0)
    {
        return Error(IOBufferPtr, kstring("wait for IO"), IO_WAIT_FOR_DATA);
    }
    return Success(IOBufferPtr, &self->read_buffer);
}

static Result(ByteCount) Console__write(IOStatePtr state, IOInternalHandle handle, DataPtr data, size_t size)
{
    return Success(ByteCount, 0);
}

static Result(ParkStatus) Console__park(IOStatePtr state, IOContext parked_context)
{
    ConsoleStatePtr self = (ConsoleStatePtr) state;

    ParkedContexts__push(&self->parked_contexts, parked_context);
    __d_println(IODEV, "parked console context\n");
    return Success(ParkStatus, IO_PARK_OK);
}

static Result(ParkStatus) Console__unpark(IOStatePtr state)
{
    ConsoleStatePtr self = (ConsoleStatePtr) state;
    ParkedContexts__pop(&self->parked_contexts);
    return Success(ParkStatus, IO_UNPARK_OK);
}

IOPtr Console__heap_new()
{
    IOPtr result = v_ptr(Kernel__alloc(sizeof(IOPtr)), IO);

    *result = (IO) {
        .read = Console__read,
        .write = Console__write,
        .open = Console__open,
        .close = Console__close,
        .park = Console__park,
        .unpark = Console__unpark,
    };

    return result;
}

extern ConsoleState console_state;

ConsoleStatePtr ConsoleState__instance()
{
    return &console_state;
}
