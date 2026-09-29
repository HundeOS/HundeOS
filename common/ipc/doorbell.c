#include "doorbell.h"

IpcEventGeneration IpcEvent__generation(ConstIpcEventPtr self)
{
    return __atomic_load_n(self, __ATOMIC_ACQUIRE);
}

bool IpcEvent__changed(ConstIpcEventPtr self, IpcEventGeneration observed)
{
    return IpcEvent__generation(self) != observed;
}

void IpcEvent__ring(IpcEventPtr self)
{
    __atomic_add_fetch(self, 1, __ATOMIC_RELEASE);
}

ConstIpcEventPtr IpcDoorbell__event(ConstIpcDoorbellPtr self)
{
    return &self->event;
}

IpcEventPtr IpcDoorbell__mutable_event(IpcDoorbellPtr self)
{
    return &self->event;
}
