#ifndef __KERNEL_IO_SUBSYSTEM_H
#define __KERNEL_IO_SUBSYSTEM_H

#include <stdint.h>

#include "../../types/result.h"
#include "../../types/optional.h"
#include "../../address.h"
#include "../../mutex.h"
#include "../../process.h"
#include "../../types/comparison.h"

#include "io.h"
#include "file/filesystem.h"
#include "subsystems/message/message_broker.h"
#include "syscalls/io.h"
#include "types.h"

typedef struct IOContext IOContext;
typedef struct Scheduler Scheduler;
typedef Scheduler * SchedulerPtr;

typedef IOContext * IOContextPtr;


FixedArrayDefine(IOHandle, MAX_HANDLES, PIDHandles, SimpleEquals(uint64_t));
typedef PIDHandles * PIDHandlesPtr;
OptionalDef(PIDHandles);
OptionalDef(PIDHandlesPtr);

ResultDef(PIDHandles);
ResultDef(PIDHandlesPtr);

MapDefine(ProcessID, PIDHandles, MAX_PROCESSES, ProcessHandleMap, SimpleEquals(uint64_t));
typedef ProcessHandleMap * ProcessHandleMapPtr;
MapDefine(DeviceHandle, ProcessPtr, IO_MAX_DEVICES, IOServerProcessMap, SimpleEquals(uint32_t));
MapDefine(IOHandle, IOServerRole, MAX_HANDLES, IOServerRoleMap, SimpleEquals(uint64_t));

typedef struct IOSubsystem
{
    IODevicesArray devices;
    IOHandlesMap handles;

    SchedulerPtr scheduler;
    MessageBrokerPtr message_broker;
    ProcessHandleMap process_handles;
    IOServerProcessMap read_server_processes;
    IOServerProcessMap write_server_processes;
    IOServerRoleMap server_roles;
    Optional(FilesystemPtr) root_filesystem;

    mutex_t lock;
}
IOSubsystem;

typedef IOSubsystem * IOSubsystemPtr;

ResultDef(IOSubsystem);
ResultDef(IOSubsystemPtr);
#ifndef __KERNEL_DIRECTORY_ENTRY_COUNT_RESULT_DEFINED
#define __KERNEL_DIRECTORY_ENTRY_COUNT_RESULT_DEFINED
ResultDef(DirectoryEntryCount);
#endif
#ifndef __KERNEL_IO_STATUS_RESULT_DEFINED
#define __KERNEL_IO_STATUS_RESULT_DEFINED
ResultDef(IOStatus);
#endif

Result(DeviceHandle) IOSubsystem__register_device(IOSubsystemPtr self, BString path, IOPtr device, IOStatePtr device_state);

Result(IOHandle) IOSubsystem__record_open_handle(
    IOSubsystemPtr self, IOContextPtr context, BString path, IOInternalHandle handle
);
Result(IOHandle) IOSubsystem__open(IOSubsystemPtr self, IOContextPtr context, BString path, Flags flags);
Result(IOHandle) IOSubsystem__close(IOSubsystemPtr self, IOContextPtr context);
Result(ByteCount) IOSubsystem__read(IOSubsystemPtr self, IOContextPtr context);
Result(ByteCount) IOSubsystem__write(IOSubsystemPtr self, IOContextPtr context);
Result(IOHandle) IOSubsystem__server_register(
    IOSubsystemPtr self, IOContextPtr context, ProcessPtr process, BString path, IOServerRole role
);
Result(IOStatus) IOSubsystem__server_get_buffer(
    IOSubsystemPtr self, IOContextPtr context, ProcessPtr process, IOServerBufferPtr target
);
Result(IOStatus) IOSubsystem__server_data_available(
    IOSubsystemPtr self, IOContextPtr context, size_t byte_count
);
Result(IOStatus) IOSubsystem__server_data_consumed(
    IOSubsystemPtr self, IOContextPtr context, size_t byte_count
);
Result(IOStatus) IOSubsystem__server_eof(IOSubsystemPtr self, IOContextPtr context);
Result(DirectoryEntryCount) IOSubsystem__list_dir(
    IOSubsystemPtr self, BString path, DirectoryEntryPtr entries, size_t max_entries
);
Result(CollectionCode) IOSubsystem__append_dev_filesystem_nodes(
    IOSubsystemPtr self, FilesystemPtr filesystem, FilesystemNodes * target
);
Result(IOStatus) IOSubsystem__mkdir(IOSubsystemPtr self, BString path);
Result(IOStatus) IOSubsystem__remove(IOSubsystemPtr self, BString path, Flags flags);
Result(IOStatus) IOSubsystem__link(IOSubsystemPtr self, BString source_path, BString target_path);
Result(IOStatus) IOSubsystem__mount(
    IOSubsystemPtr self, BString device_path, BString filesystem_type, BString mountpoint
);

Result(IOStatus) IOSubsystem__data_arrived(IOSubsystemPtr self, IOContextPtr parked_context);
void IOSubsystem__message_received(KernelListenerPtr listener, MessagePtr message);

// called by IRQs
Result(IOHandle) IOSubsystem__async_receive(IOSubsystemPtr self, BString path, Flags flags);

// registering message broker listeners
Result(ListenerRegisterResult) IOSubsystem__register_listeners(KernelPtr kernel);
IOSubsystemPtr IOSubsystem__heap_new(SchedulerPtr scheduler, MessageBrokerPtr message_broker);

IOInternalHandle device_internal_handle(IOHandle handle);

#define NEW_IO_SUBSYSTEM(sched, broker) \
    (IOSubsystem) { \
        .devices = EmptyArray(IODevicesArray), \
        .handles = EmptyMap(IOHandlesMap), \
        .scheduler = sched, \
        .process_handles = EmptyMap(ProcessHandleMap),\
        .read_server_processes = EmptyMap(IOServerProcessMap),\
        .write_server_processes = EmptyMap(IOServerProcessMap),\
        .server_roles = EmptyMap(IOServerRoleMap),\
        .root_filesystem = None(FilesystemPtr),\
        .lock = MUTEX_INIT,\
        .message_broker = broker,\
        \
    }

#define IO_SUBSYSTEM_INIT(subsystem, sched) \
        ArrayInit(IODevicesArray, subsystem.devices); \
        MapInit(IOHandlesMap, subsystem.handles); \
        subsystem.scheduler = sched; \
        subsystem.lock = MUTEX_INIT;\
        MapInit(ProcessHandleMap, subsystem.process_handles);\
        MapInit(IOServerProcessMap, subsystem.read_server_processes);\
        MapInit(IOServerProcessMap, subsystem.write_server_processes);\
        MapInit(IOServerRoleMap, subsystem.server_roles);\
        subsystem.root_filesystem = None(FilesystemPtr);\

#endif
