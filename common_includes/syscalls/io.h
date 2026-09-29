#ifndef _COMMON_INCLUDES_SYSCALLS_IO_H
#define _COMMON_INCLUDES_SYSCALLS_IO_H

#include <stddef.h>
#include <stdint.h>
#include "process.h"
#include "types/bstring.h"
#include "types/result.h"

#define IO_FILE_WATCH_CAPACITY 16U

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
    /* Operations 8-13 were the retired mount and stream-server APIs. */
    IO_ACCEPT = 14,
} IOOperation;

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
    IO_PATH_UNMANAGED = -14,
    IO_INTERRUPTED = -15,
    IO_ALREADY_EXISTS = -16,
    IO_REDIRECT = -17,
    IO_PANIC = -255,
} IOStatus;

typedef enum IOFlags
{
    IO_OPEN_READ = 0x1,
    IO_OPEN_WRITE = 0x2,
    IO_REMOVE_RECURSIVE = 0x4,
    IO_OPEN_EXECUTE = 0x8,
    IO_OPEN_BIND = 0x10,
    IO_OPEN_LISTEN = 0x20,
} IOFlags;

typedef int64_t IOHandle;
typedef IOHandle * IOHandlePtr;
#define IO_ENDPOINT_TTY_INPUT (1LL << 60)
#define IO_ENDPOINT_CONSOLE_OUTPUT (IO_ENDPOINT_TTY_INPUT + 1)
#define IO_ENDPOINT_KEYBOARD_INPUT (IO_ENDPOINT_TTY_INPUT + 2)

static inline bool IOEndpoint__is_terminal(IOHandle handle)
{
    return handle >= IO_ENDPOINT_TTY_INPUT && handle <= IO_ENDPOINT_KEYBOARD_INPUT;
}

typedef int64_t ByteCount;
typedef int64_t DirectoryEntryCount;
ResultDef(DirectoryEntryCount);
typedef uint64_t Flags;
typedef uint16_t FileMode;
typedef uint64_t DirectoryEntryAttributes;

typedef enum IOSeekWhence
{
    IO_SEEK_SET,
    IO_SEEK_CURRENT,
    IO_SEEK_END,
} IOSeekWhence;

typedef enum IOFileChangeFlags
{
    IO_FILE_CHANGE_WRITE = 1 << 0,
    IO_FILE_CHANGE_DELETE = 1 << 1,
    IO_FILE_CHANGE_RENAME = 1 << 2,
    IO_FILE_CHANGE_TRUNCATE = 1 << 3,
} IOFileChangeFlags;

typedef struct IOFileChange
{
    IOHandle handle;
    uint64_t generation;
    Flags events;
} IOFileChange;

typedef struct IOFileWatch
{
    IOHandle handle;
    uint64_t generation;
} IOFileWatch;

static inline IOFileWatch IOFileWatch__new(IOHandle handle, uint64_t generation)
{
    return (IOFileWatch){.handle = handle, .generation = generation};
}

typedef struct IOSeekResult
{
    IOStatus status;
    uint64_t position;
} IOSeekResult;

typedef struct IOWatchResult
{
    IOStatus status;
    IOFileChange change;
} IOWatchResult;

static inline IOSeekResult IOSeekResult__new(IOStatus status, uint64_t position)
{
    return (IOSeekResult){.status = status, .position = position};
}

static inline IOWatchResult IOWatchResult__new(IOStatus status, IOFileChange change)
{
    return (IOWatchResult){.status = status, .change = change};
}

typedef struct IOFileInfo
{
    FileMode mode;
    UserID user_id;
    GroupID group_id;
    uint64_t size;
    uint64_t identity;
} IOFileInfo;

typedef IOFileInfo * IOFileInfoPtr;
OptionalDef(IOFileInfo);
ResultDef(IOFileInfo);

static inline IOFileInfo IOFileInfo__new(FileMode mode, UserID user_id, GroupID group_id, uint64_t size)
{
    return (IOFileInfo){.mode = mode, .user_id = user_id, .group_id = group_id, .size = size};
}

typedef struct DirectoryEntry DirectoryEntry;
typedef DirectoryEntry * DirectoryEntryPtr;

typedef char * IOServerBufferDataPtr; // hundelint: ignore-no-raw-char-buffer

typedef struct IOServerBuffer
{
    IOServerBufferDataPtr data;
    uint64_t capacity;
} IOServerBuffer;

typedef IOServerBuffer * IOServerBufferPtr;

static inline IOServerBuffer IOServerBuffer__new(IOServerBufferDataPtr data, uint64_t capacity)
{
    return (IOServerBuffer){
        .data = data,
        .capacity = capacity,
    };
}

typedef enum IOServerRole
{
    IO_SERVER_READ = 1,
    IO_SERVER_WRITE = 2,
} IOServerRole;

typedef struct IOPathRequest
{
    BString path;
    Flags flags;
} IOPathRequest;

typedef IOPathRequest * IOPathRequestPtr;

typedef struct IOListDirRequest
{
    BString path;
    DirectoryEntryPtr entries;
    size_t max_entries;
} IOListDirRequest;

typedef IOListDirRequest * IOListDirRequestPtr;

typedef struct IOLinkRequest
{
    BString source_path;
    BString target_path;
} IOLinkRequest;

typedef IOLinkRequest * IOLinkRequestPtr;

#define IO_DIRENT_NAME_MAX 256

#define IO_MODE_TYPE_MASK 0xF000U
#define IO_MODE_TYPE_FILE 0x8000U
#define IO_MODE_TYPE_DIR 0x4000U

#define IO_MODE_OWNER_READ 0x0100U
#define IO_MODE_OWNER_WRITE 0x0080U
#define IO_MODE_OWNER_EXEC 0x0040U
#define IO_MODE_GROUP_READ 0x0020U
#define IO_MODE_GROUP_WRITE 0x0010U
#define IO_MODE_GROUP_EXEC 0x0008U
#define IO_MODE_OTHER_READ 0x0004U
#define IO_MODE_OTHER_WRITE 0x0002U
#define IO_MODE_OTHER_EXEC 0x0001U

#define IO_DIRENT_ATTR_NONE 0ULL
#define IO_DIRENT_ATTR_HIDDEN (1ULL << 0)

struct DirectoryEntry
{
    DirectoryEntryAttributes attributes;
    UserID user_id;
    GroupID group_id;
    FileMode mode;
    uint64_t name_len;
    char name[IO_DIRENT_NAME_MAX]; // hundelint: ignore-no-raw-char-buffer
};

static inline IOPathRequest IOPathRequest__new(ConstBStringPtr path, Flags flags)
{
    return (IOPathRequest){
        .path = *path,
        .flags = flags,
    };
}

static inline IOListDirRequest IOListDirRequest__new(ConstBStringPtr path, DirectoryEntryPtr entries,
                                                     size_t max_entries)
{
    return (IOListDirRequest){
        .path = *path,
        .entries = entries,
        .max_entries = max_entries,
    };
}

static inline IOLinkRequest IOLinkRequest__new(ConstBStringPtr source_path, ConstBStringPtr target_path)
{
    return (IOLinkRequest){
        .source_path = *source_path,
        .target_path = *target_path,
    };
}

static inline DirectoryEntry DirectoryEntry__new(DirectoryEntryAttributes attributes, UserID user_id, GroupID group_id,
                                                 FileMode mode, ConstBStringPtr name)
{
    DirectoryEntry entry = (DirectoryEntry){
        .attributes = attributes,
        .user_id = user_id,
        .group_id = group_id,
        .mode = mode,
        .name_len = 0,
    };

    entry.name_len = (uint64_t)BString__copy_to(name, entry.name, IO_DIRENT_NAME_MAX);
    return entry;
}

#endif
