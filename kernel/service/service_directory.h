#ifndef __KERNEL_SERVICE_SERVICE_DIRECTORY_H
#define __KERNEL_SERVICE_SERVICE_DIRECTORY_H

#include "../../common/services/service_directory.h"
#include "../address.h"
#include "../mutex.h"
#include "../process_class.h"
#include "../scheduler/types.h"
#include "../types/result.h"
#include "service.h"

typedef struct Process Process;
typedef Process * ProcessPtr;
typedef struct Kernel Kernel;
typedef Kernel * KernelPtr;

typedef struct KernelServiceDirectory
{
    mutex_t lock;
    UserAddress directory;
} KernelServiceDirectory;

typedef KernelServiceDirectory * KernelServiceDirectoryPtr;

#define KERNEL_SERVICE_DIRECTORY_INIT                                                                                  \
    (KernelServiceDirectory)                                                                                           \
    {                                                                                                                  \
        .lock = MUTEX_INIT, .directory = NULL_USER_ADDRESS,                                                            \
    }

Result(ServiceCode) KernelServiceDirectory__init(KernelServiceDirectoryPtr self);
Result(ServiceCode) KernelServiceDirectory__map_process(KernelServiceDirectoryPtr self, ProcessPtr process);
Result(ServiceCode) KernelServiceDirectory__publish_endpoint(KernelServiceDirectoryPtr self, ProcessPtr process,
                                                             ConstBStringPtr name, QueueEndpointID endpoint);
void KernelServiceDirectory__remove_endpoint(KernelServiceDirectoryPtr self, QueueEndpointID endpoint);
ServiceDirectoryPtr KernelServiceDirectory__kernel_directory(KernelServiceDirectoryPtr self);

#endif
