#ifndef __COMMON_SERVICES_SERVICE_DIRECTORY_H
#define __COMMON_SERVICES_SERVICE_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../ipc/channel.h"
#include "../types/bstring.h"
#include "service_types.h"

#define HUND_SERVICE_DIRECTORY_VADDR 0x70000000ULL
#define HUND_SERVICE_QUEUE_REGION_VADDR 0x70001000ULL
#define HUND_SERVICE_PAGE_SIZE 4096ULL
#define HUND_SERVICE_MAX_SERVICES 16U
#define HUND_SERVICE_NAME_SIZE 64U
#define HUND_SERVICE_QUEUE_SLOTS 32U
#define HUND_SERVICE_QUEUE_PAYLOAD_SIZE 32U
#define HUND_SERVICE_DIRECTORY_MAGIC 0x48554E4453514449ULL
#define HUND_SERVICE_DIRECTORY_VERSION 1ULL
#define HUND_SERVICE_DIRECTORY_SIZE HUND_SERVICE_PAGE_SIZE
#define HUND_SERVICE_QUEUE_REGION_SIZE (HUND_SERVICE_MAX_SERVICES * HUND_SERVICE_PAGE_SIZE)
#define HUND_SERVICE_WORKSPACE_VADDR (HUND_SERVICE_QUEUE_REGION_VADDR + HUND_SERVICE_QUEUE_REGION_SIZE)
#define HUND_SERVICE_WORKSPACE_SIZE HUND_SERVICE_PAGE_SIZE
#define HUND_SERVICE_SHARED_REGION_SIZE (HUND_SERVICE_QUEUE_REGION_SIZE + HUND_SERVICE_WORKSPACE_SIZE)

typedef enum ServiceQueueStatus
{
    SERVICE_QUEUE_OK = 0,
    SERVICE_QUEUE_EMPTY = 1,
    SERVICE_QUEUE_FULL = 2,
    SERVICE_QUEUE_NOT_FOUND = 3,
} ServiceQueueStatus;

typedef enum ServiceQueueMessageKind
{
    SERVICE_QUEUE_MESSAGE_KERNEL_PROCESS_EXIT = 0x8000000000000001ULL,
} ServiceQueueMessageKind;

typedef struct ServiceQueueMessage
{
    uint64_t sender_pid;
    uint64_t kind;
    uint64_t arg0;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint8_t payload[HUND_SERVICE_QUEUE_PAYLOAD_SIZE];
} ServiceQueueMessage;
typedef ServiceQueueMessage * ServiceQueueMessagePtr;
typedef const ServiceQueueMessage * ConstServiceQueueMessagePtr;

typedef struct ServiceQueue
{
    ChannelState state;
    uint64_t head;
    uint64_t tail;
    uint64_t message_ready[HUND_SERVICE_QUEUE_SLOTS];
    ServiceQueueMessage messages[HUND_SERVICE_QUEUE_SLOTS];
} ServiceQueue;
typedef ServiceQueue * ServiceQueuePtr;
OptionalDef(ServiceQueuePtr);

typedef struct ServiceDirectoryName
{
    StringPos length;
    uint8_t data[HUND_SERVICE_NAME_SIZE];
} ServiceDirectoryName;
typedef ServiceDirectoryName * ServiceDirectoryNamePtr;
typedef const ServiceDirectoryName * ConstServiceDirectoryNamePtr;

typedef struct ServiceDirectoryEntry
{
    uint64_t present;
    uint64_t generation;
    uint64_t service_pid;
    uint64_t queue_address;
    uint64_t queue_size;
    uint64_t kernel_event_subscriptions;
    ServiceDirectoryName name;
} ServiceDirectoryEntry;
typedef ServiceDirectoryEntry * ServiceDirectoryEntryPtr;

typedef struct ServiceDirectory
{
    uint64_t magic;
    uint64_t version;
    uint64_t generation;
    uint64_t entry_count;
    ServiceDirectoryEntry entries[HUND_SERVICE_MAX_SERVICES];
} ServiceDirectory;
typedef ServiceDirectory * ServiceDirectoryPtr;

ServiceQueueMessage ServiceQueueMessage__new(uint64_t sender_pid, uint64_t kind);
ServiceQueueMessage ServiceQueueMessage__process_exit(uint64_t pid, uint64_t return_code);
ServiceDirectoryPtr ServiceDirectory__shared(void);
ServiceQueuePtr ServiceQueue__at_index(uint64_t index);
ChannelStatePtr ServiceQueue__state(ServiceQueuePtr self);
uint64_t ServiceQueue__data_epoch(ServiceQueuePtr self);
uint64_t ServiceQueue__space_epoch(ServiceQueuePtr self);
void ServiceQueue__notify_data(ServiceQueuePtr self);
void ServiceQueue__notify_space(ServiceQueuePtr self);
bool ServiceQueue__has_messages(ServiceQueuePtr self);
ServiceQueueStatus ServiceQueue__push(ServiceQueuePtr self, ConstServiceQueueMessagePtr message);
ServiceQueueStatus ServiceQueue__pop(ServiceQueuePtr self, ServiceQueueMessagePtr message);
void ServiceDirectoryName__copy_from_bstring(ServiceDirectoryNamePtr self, ConstBStringPtr name);
bool ServiceDirectoryName__equals_bstring(ConstServiceDirectoryNamePtr self, ConstBStringPtr name);
bool ServiceDirectoryEntry__name_equals(ServiceDirectoryEntryPtr self, ConstBStringPtr name);
Optional(ServiceQueuePtr) ServiceDirectory__find_queue(ServiceDirectoryPtr self, ConstBStringPtr name);
Optional(AtomicMessageQueueEndpointID) ServiceDirectory__find_endpoint(ServiceDirectoryPtr self, ConstBStringPtr name);
Optional(uint64_t) ServiceDirectory__find_service_pid(ServiceDirectoryPtr self, ConstBStringPtr name);

#endif
