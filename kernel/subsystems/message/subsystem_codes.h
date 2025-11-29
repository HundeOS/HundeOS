#ifndef __KERNEL_SUBSYSTEMS_MESSAGE_SUBSYSTEM_CODES_H
#define __KERNEL_SUBSYSTEMS_MESSAGE_SUBSYSTEM_CODES_H

typedef enum Sources
{
    SOURCE_KERNEL = 1,
    SOURCE_FAULT = 2,
    SOURCE_IRQ = 3,
    SOURCE_IO = 4,
    SOURCE_SCHEDULER = 5,
    SOURCE_PROCESS = 6,
}
Sources;

typedef enum AckCodes
{
    ACK_OK = 0,
    ACK_ERROR = 1,
}
AckCodes;

typedef enum PayloadTypes
{
    PAYLOAD_TYPE_ANYTHING = 0,
    PAYLOAD_TYPE_DATA = 1,
    PAYLOAD_TYPE_DEFERRED = 2,
}
PayloadTypes;

typedef enum ListenerRegisterResults
{
    LISTENER_REGISTER_OK = 0,
    LISTENER_REGISTER_ERROR = 1,
}
ListenerRegisterResults;


typedef enum IOSystemCodes
{
    SUBSYS_IO_PARK = 4001,
    SUBSYS_IO_AVAILABLE = 4002,
}
IOSystemCodes;

typedef enum ProcessCodes
{
    SUBSYS_PROCESS_TERMINATED = 6000,
    SUBSYS_PROCESS_WAIT = 6001,
    SUBSYS_PROCESS_RUNNING = 6002,
}
ProcessCodes;

#endif