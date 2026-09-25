#ifndef KERNEL_EXEC_LOAD_H
#define KERNEL_EXEC_LOAD_H
#include "exec.h"
#include "../ipc/kernel_channel.h"
#include "ipc/io_protocol.h"

typedef enum ExecLoadPhase
{
    EXEC_LOAD_OPEN,
    EXEC_LOAD_READ,
    EXEC_LOAD_CLOSE
} ExecLoadPhase;

struct ExecLoad
{
    KernelChannel channel;
    OriginatingContext context;
    BString path;
    ProcessArguments arguments;
    Optional(UserID) user;
    SpawnStdio stdio;
    bool wait;
    bool pending;
    ExecLoadPhase phase;
    IOHandle handle;
    KernelAddress buffer;
    BufferDescriptor transfer;
    QueueProgress observation;
    bool observation_valid;
    size_t capacity;
    size_t size;
};

Result(ProcessPtr)
    ExecLoad__begin(ConstOriginatingContextPtr context, ConstBStringPtr path, bool wait, OptionalPtr(UserID) user,
                    ConstProcessArgumentsPtr arguments, ConstSpawnStdioPtr stdio);
Result(ProcessPtr) ExecLoad__continue(ProcessPtr process);
bool ExecLoad__ready(ProcessPtr process);
void ExecLoad__cancel(ProcessPtr process);
#endif
