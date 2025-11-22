#include "io.h"

FixedArrayBodyDefine(ReadBuffer);

Result(IOHandle) open(OriginatingContext context, KString path, Flags flags)
{
    return IOSubsystem__open(&context.kernel->io_subsystem, context.process, path, flags);

}