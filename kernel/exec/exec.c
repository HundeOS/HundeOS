#include "exec.h"

#include "../../common_includes/syscalls/io.h"
#include "../../common_includes/syscalls/spawn.h"
#include "../types/result.h"
#include "../bin/elf.h"
#include "../kio/io.h"
#include "../kalloc.h"
#include "../syscall/deferred.h"

FixedArrayBodyDefine(ElfReadBuffer);

static KString unreachable_code = KSTRING_NEW("unreachable code");

static Result(ProcessPtr) Exec__launch(OriginatingContext context, Elf elf, bool wait, Optional(KString) arg0)
{
    __d_printf(EXEC, "Launching elf %s\n", cstr(elf.name));
    KernelPtr kernel = context.kernel;
    Result(ProcessPtr) proc = Process__setup_with_args(elf, context.process, arg0);
    ResultMatch(ProcessPtr, proc)
        IsSuccess(process,
        {
            ProcessPtr new_proc = process;
            Match(ProcessPtr, context.process)
                IsSome(parent,
                {
                    __d_printf(EXEC, "Parent %p\n", parent);
                    Scheduler__schedule(kernel->scheduler, new_proc);
                    if (wait)
                    {
                        __d_printf(EXEC, "Parking parent %p\n", parent);
                        // TODO we need a continuation to give the result of the proc back to the parent
                        KernelContextPtr kernel_context = KernelContext__from_originating_context_heap(context);

                        Scheduler__park(
                            kernel->scheduler, parent, kernel_context,
                            Some(Continuation, SystemCall__Complete), PARK_WAIT_CHILD
                        );
                    }
                    __d_printf(EXEC, "Scheduling elf %s\n", cstr(elf.name));


                    return Success(ProcessPtr, new_proc);
                })
            End
        })
        IsError(code, message,
        {
            return Error(ProcessPtr, message, SPAWN_FAILED);
        })
    End
    return Error(ProcessPtr, unreachable_code, SPAWN_FAILED);
}

static Result(Elf) Exec__read_elf(OriginatingContext context, IOHandle handle, KString path)
{
    DataBufferPtr buffer = DataBuffer__heap_new();
    __d_println(EXEC, "Reading elf");
    ResultMatch(ByteCount, read(context, handle, buffer))
        IsSuccess(bytes_read,
        {
            __d_printf(EXEC, "Copying elf %i %s\n", bytes_read, cstr(path));
            Address elf_start = Kernel__alloc_kernel(bytes_read);
            Kernel__memcopy(addr(&buffer->data), elf_start, bytes_read);

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

    return Error(Elf, unreachable_code, SPAWN_FAILED);
}

Result(ProcessPtr) Exec__exec(OriginatingContext context, KString path, bool wait, Optional(KString) arg0)
{
    __d_printf(EXEC, "Executing elf %s\n", cstr(path));

    ResultMatch(IOHandle, open(context, path, IO_OPEN_READ))
        IsError(code, message,
        {
            __d_printf(EXEC, "Failed to open elf %s\n", cstr(path));
            return Error(ProcessPtr, message, SPAWN_NO_FILE);
        })
        IsSuccess(handle,
        {
            __d_printf(EXEC, "Opened elf %s\n", cstr(path));

            ResultMatch(Elf, Exec__read_elf(context, handle, path))
                IsSuccess(elf,
                {
                    return Exec__launch(context, elf, wait, arg0);
                })
                IsError(code, message,
                {
                    __d_printf(EXEC, "loading elf %s failed: %s\n", cstr(path), cstr(message));
                    return Error(ProcessPtr, message, SPAWN_FAILED);
                })
            End
        })
    End
    return Error(ProcessPtr, unreachable_code, SPAWN_FAILED);
}
