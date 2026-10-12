#ifndef HUND_IPC_DOORBELL_H
#define HUND_IPC_DOORBELL_H

#include <stdbool.h>
#include <stdint.h>

#include "../types/address.h"
#include "../types/optional.h"

#define IPC_DOORBELL_ADDRESS ((Address)0x722ff000ULL)
#define IPC_DOORBELL_PAGE_SIZE 4096ULL

typedef uint64_t IpcEvent;
typedef IpcEvent * IpcEventPtr;
typedef const IpcEvent * ConstIpcEventPtr;
typedef uint64_t IpcEventGeneration;
OptionalDef(IpcEventGeneration);

typedef struct IpcDoorbell
{
    IpcEvent event;
} IpcDoorbell;

typedef IpcDoorbell * IpcDoorbellPtr;
typedef const IpcDoorbell * ConstIpcDoorbellPtr;

IpcEventGeneration IpcEvent__generation(ConstIpcEventPtr self);
bool IpcEvent__changed(ConstIpcEventPtr self, IpcEventGeneration observed);
void IpcEvent__ring(IpcEventPtr self);
ConstIpcEventPtr IpcDoorbell__event(ConstIpcDoorbellPtr self);
IpcEventPtr IpcDoorbell__mutable_event(IpcDoorbellPtr self);

#endif
