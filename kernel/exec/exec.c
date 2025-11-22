#include "exec.h"

#include "../../common_includes/syscalls/io.h"
#include "../types/result.h"
#include "../bin/elf.h"

FixedArrayBodyDefine(ElfReadBuffer);

static KString unreachable_code = KSTRING_NEW("unreachable code");

static Result(ProcessPtr) Exec__launch(OriginatingContext context, Elf elf, bool wait)
{
    __d_printf(EXEC, "Launching elf %s\n", cstr(elf.name));
    KernelPtr kernel = context.kernel;
    Result(ProcessPtr) proc = Process__setup(elf, context.process);
    ResultMatch(ProcessPtr, proc)
        IsSuccess(process,
        {
            ProcessPtr new_proc = process;
            Match(ProcessPtr, context.process)
                IsSome(parent,
                {
                    if (wait)
                    {
                        // TODO we need a continuation to give the result of the proc back to the parent
                        KernelContextPtr kernel_context = KernelContext__from_originating_context_heap(context);
                        Scheduler__park(kernel->scheduler, parent, kernel_context, None(Continuation));
                    }
                    Scheduler__schedule(kernel->scheduler, new_proc);
                    return Success(ProcessPtr, new_proc);
                })
            End
        })
        IsError(code, message,
        {
            return Error(ProcessPtr, message, EXEC_FAILED);
        })
    End
    return Error(ProcessPtr, unreachable_code, EXEC_NOT_IMPLEMENTED);
}

static Result(Elf) Exec__read_elf(OriginatingContext context, IOHandle handle, KString path)
{
    Data buffer[_128KB];
    IOContext io_context = IOContext__from_originating_context(context, handle, buffer, sizeof(buffer));
    Result(ByteCount) read = IOSubsystem__read(io_context.io_subsystem, io_context);
    __d_printf(EXEC, "Read complete %s\n", cstr(path));

    ResultMatch(ByteCount, read)
        IsSuccess(bytes_read,
        {
            __d_printf(EXEC, "Copying elf %s\n", cstr(path));
            Address elf_start = Kernel__alloc_kernel(bytes_read);
            Kernel__memcopy(addr(&buffer), elf_start, bytes_read);

            Elf elf = Elf__new(elf_start, offset_addr(elf_start, bytes_read), path);
            return Success(Elf, elf);
        });
        IsError(code, message,
        {
            if (code == IO_EOF)
            {
                __d_printf(PANIC, "EOF Reading ELF handle = %x\n", handle);
            }
            return Error(Elf, message, code);
        })
    End

    return Error(Elf, unreachable_code, EXEC_NOT_IMPLEMENTED);
}

Result(ProcessPtr) Exec__exec(OriginatingContext context, KString path, bool wait)
{
    __d_printf(EXEC, "Executing elf %s\n", cstr(path));
    __d_printf(EXEC, "kernel %p \n", context.kernel);
    KernelPtr kernel = context.kernel;
    Optional(ProcessPtr) parent = context.process;

    Result(IOHandle) handle = IOSubsystem__open(&kernel->io_subsystem, parent, path, IO_OPEN_READ);
    __d_printf(EXEC, "Opened elf %s\n", cstr(path));
    ResultMatch(IOHandle, handle)
        IsError(code, message,
        {
            return Error(ProcessPtr, message, EXEC_NOT_FOUND);
        })
        IsSuccess(handle,
        {
            Result(Elf) result_elf = Exec__read_elf(context, handle, path);
            ResultMatch(Elf, result_elf)
                IsSuccess(elf,
                {
                    return Exec__launch(context, elf, wait);
                })
                IsError(code, message,
                {
                    __d_printf(EXEC, "loading elf %s failed: %s\n", cstr(path), cstr(message));
                    return Error(ProcessPtr, message, EXEC_FAILED);
                })
            End
        })
    End
    return Error(ProcessPtr, unreachable_code, EXEC_NOT_IMPLEMENTED);
}