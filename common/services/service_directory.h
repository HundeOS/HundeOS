#ifndef __COMMON_SERVICES_SERVICE_DIRECTORY_H
#define __COMMON_SERVICES_SERVICE_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../ipc/queue_types.h"
#include "../types/bstring.h"

#define HUND_SERVICE_DIRECTORY_VADDR 0x70000000ULL
#define HUND_SERVICE_MAX_SERVICES 1024U
#define HUND_SERVICE_NAME_SIZE BSTRING_MAX_SIZE
#define HUND_SERVICE_DIRECTORY_MAGIC 0x48554E4453514449ULL
#define HUND_SERVICE_DIRECTORY_VERSION 2ULL
#define HUND_SERVICE_DIRECTORY_PAGE_SIZE 4096ULL

typedef struct ServiceDirectoryEntry
{
    uint64_t present;
    uint64_t generation;
    uint64_t service_pid;
    BString name;
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

#define HUND_SERVICE_DIRECTORY_SIZE                                                                                    \
    (((sizeof(ServiceDirectory) + HUND_SERVICE_DIRECTORY_PAGE_SIZE - 1ULL) / HUND_SERVICE_DIRECTORY_PAGE_SIZE) *       \
     HUND_SERVICE_DIRECTORY_PAGE_SIZE)

_Static_assert(HUND_SERVICE_DIRECTORY_VADDR + HUND_SERVICE_DIRECTORY_SIZE <= 0x71000000ULL,
               "Service directory overlaps managed payload pages");

ServiceDirectoryPtr ServiceDirectory__shared(void);
bool ServiceDirectoryEntry__name_equals(ServiceDirectoryEntryPtr self, ConstBStringPtr name);
Optional(QueueEndpointID) ServiceDirectory__find_endpoint(ServiceDirectoryPtr self, ConstBStringPtr name);
Optional(uint64_t) ServiceDirectory__find_service_pid(ServiceDirectoryPtr self, ConstBStringPtr name);

#endif
