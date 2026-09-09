#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_USERSPACE_FILESYSTEM_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_USERSPACE_FILESYSTEM_H

#include "../../../process.h"
#include "../../../subsystems/message/message_broker.h"
#include "filesystem.h"
#include "file_io.h"
#include "syscalls/service.h"

typedef struct UserspaceFilesystemRegistry UserspaceFilesystemRegistry;
typedef UserspaceFilesystemRegistry * UserspaceFilesystemRegistryPtr;

ResultDef(ServiceFilesystemRequest);

UserspaceFilesystemRegistryPtr UserspaceFilesystemRegistry__shared(void);
Result(ServiceCode)
    UserspaceFilesystemRegistry__register(UserspaceFilesystemRegistryPtr self, ProcessPtr service_process,
                                          ServiceFilesystemRegistrationPtr registration, FilesystemPtr root_filesystem,
                                          MessageBrokerPtr message_broker);
Result(ServiceFilesystemRequest)
    UserspaceFilesystemRegistry__get_request(UserspaceFilesystemRegistryPtr self, ProcessPtr service_process,
                                             uint64_t request_id);
Result(ServiceCode)
    UserspaceFilesystemRegistry__complete(UserspaceFilesystemRegistryPtr self, ProcessPtr service_process,
                                          ServiceFilesystemCompletionPtr completion);
void UserspaceFilesystemRegistry__reset(UserspaceFilesystemRegistryPtr self);

#endif
