#ifndef _COMMON_INCLUDES_SYSCALLS_IO_H
#define _COMMON_INCLUDES_SYSCALLS_IO_H

#include <stdint.h>
#include "process.h"
#include "types/bstring.h"

typedef enum IOOperation
{
    IO_OPEN,
    IO_CLOSE,
    IO_READ,
    IO_WRITE,
    IO_LIST_DIR,
    IO_MKDIR,
    IO_REMOVE,
    IO_LINK,
    IO_MOUNT,
    IO_SERVER_REGISTER,
    IO_SERVER_GET_BUFFER,
    IO_SERVER_DATA_AVAILABLE,
    IO_SERVER_DATA_CONSUMED,
    IO_SERVER_EOF,
}
IOOperation;

typedef enum IOStatus
{
    IO_OK = 0,
    IO_NO_DEVICE_FOUND = -1,
    IO_REGISTER_FAILED = -2,
    IO_NOT_OPEN = -3,
    IO_UNKNOWN_HANDLE = -4,
    IO_WAIT_FOR_DATA = -5,
    IO_OPERATION_NOT_IMPLEMENTED = -6,
    IO_OPERATION_NOT_SUPPORTED = -7,
    IO_INVALID_MOUNT_POINT = -8,
    IO_INVALID_PATH = -9,
    IO_EOF = -10,
    IO_UNKNOWN_BUFFER = -11,
    IO_UNKNOWN_PID = -12,
    IO_PERMISSION_DENIED = -13,
    IO_PANIC = -255,
}
IOStatus;

typedef enum IOFlags
{
    IO_OPEN_READ = 0x1,
    IO_OPEN_WRITE = 0x2,
    IO_REMOVE_RECURSIVE = 0x4,
    IO_OPEN_EXECUTE = 0x8,
}
IOFlags;

typedef int64_t IOHandle;
typedef int64_t ByteCount;
typedef int64_t DirectoryEntryCount;
typedef uint64_t Flags;
typedef uint16_t FileMode;
typedef uint64_t DirectoryEntryAttributes;

typedef char * IOServerBufferDataPtr;

typedef struct IOServerBuffer
{
    IOServerBufferDataPtr data;
    uint64_t capacity;
}
IOServerBuffer;

typedef IOServerBuffer * IOServerBufferPtr;

typedef enum IOServerRole
{
    IO_SERVER_READ = 1,
    IO_SERVER_WRITE = 2,
}
IOServerRole;

#define IO_DIRENT_NAME_MAX 256

#define IO_MODE_TYPE_MASK 0xF000U
#define IO_MODE_TYPE_FILE 0x8000U
#define IO_MODE_TYPE_DIR 0x4000U

#define IO_MODE_OWNER_READ  0x0100U
#define IO_MODE_OWNER_WRITE 0x0080U
#define IO_MODE_OWNER_EXEC  0x0040U
#define IO_MODE_GROUP_READ  0x0020U
#define IO_MODE_GROUP_WRITE 0x0010U
#define IO_MODE_GROUP_EXEC  0x0008U
#define IO_MODE_OTHER_READ  0x0004U
#define IO_MODE_OTHER_WRITE 0x0002U
#define IO_MODE_OTHER_EXEC  0x0001U

#define IO_DIRENT_ATTR_NONE 0ULL
#define IO_DIRENT_ATTR_HIDDEN (1ULL << 0)

typedef struct DirectoryEntry
{
    DirectoryEntryAttributes attributes;
    UserID user_id;
    GroupID group_id;
    FileMode mode;
    uint64_t name_len;
    char name[IO_DIRENT_NAME_MAX];
}
DirectoryEntry;

typedef DirectoryEntry * DirectoryEntryPtr;

static inline DirectoryEntry DirectoryEntry__new(
    DirectoryEntryAttributes attributes,
    UserID user_id,
    GroupID group_id,
    FileMode mode,
    ConstBStringPtr name
)
{
    DirectoryEntry entry = (DirectoryEntry) {
        .attributes = attributes,
        .user_id = user_id,
        .group_id = group_id,
        .mode = mode,
        .name_len = 0,
    };

    entry.name_len = (uint64_t) BString__copy_to(name, entry.name, IO_DIRENT_NAME_MAX);
    return entry;
}

#endif
