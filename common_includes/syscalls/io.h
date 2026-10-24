#ifndef _COMMON_INCLUDES_SYSCALLS_IO_H
#define _COMMON_INCLUDES_SYSCALLS_IO_H

#include <stddef.h>
#include <stdint.h>
#include "process.h"
#include "types/bstring.h"
#include "types/result.h"

#define IO_FILE_WATCH_CAPACITY 16U
#define IO_TERMINAL_CONTROL_CHARACTER_COUNT 20U
#define IO_TERMINAL_INPUT_CR_TO_NL 0000400U
#define IO_TERMINAL_INPUT_START_STOP_OUTPUT 0002000U
#define IO_TERMINAL_OUTPUT_PROCESSING 0000001U
#define IO_TERMINAL_OUTPUT_NL_TO_CR_NL 0000004U
#define IO_TERMINAL_CONTROL_CHARACTER_SIZE_8 0000060U
#define IO_TERMINAL_LOCAL_SIGNALS 0000001U
#define IO_TERMINAL_LOCAL_CANONICAL 0000002U
#define IO_TERMINAL_LOCAL_ECHO 0000010U
#define IO_TERMINAL_LOCAL_ECHO_ERASE 0000020U
#define IO_TERMINAL_LOCAL_ECHO_KILL 0000040U
#define IO_TERMINAL_LOCAL_EXTENDED 0100000U
#define IO_TERMINAL_SPEED_38400 0000017U

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
    IO_BROKEN_PIPE = -18,
    IO_WOULD_BLOCK = -19,
    IO_PANIC = -255,
} IOStatus;

ResultDef(IOStatus);

typedef enum IOFlags
{
    IO_OPEN_READ = 0x1,
    IO_OPEN_WRITE = 0x2,
    IO_REMOVE_RECURSIVE = 0x4,
    IO_OPEN_EXECUTE = 0x8,
    IO_OPEN_BIND = 0x10,
    IO_OPEN_LISTEN = 0x20,
    IO_OPEN_CREATE = 0x40,
    IO_OPEN_EXCLUSIVE = 0x80,
    IO_OPEN_TRUNCATE = 0x100,
} IOFlags;

typedef enum IOTransferFlags
{
    IO_TRANSFER_NONBLOCK = 0x1,
    IO_TRANSFER_APPEND = 0x2,
} IOTransferFlags;

typedef struct IOTerminalAttributes
{
    uint32_t input_flags;
    uint32_t output_flags;
    uint32_t control_flags;
    uint32_t local_flags;
    uint32_t input_speed;
    uint32_t output_speed;
    uint8_t control_characters[IO_TERMINAL_CONTROL_CHARACTER_COUNT];
} IOTerminalAttributes;

typedef IOTerminalAttributes * IOTerminalAttributesPtr;
ResultDef(IOTerminalAttributes);

typedef struct IOTerminalAttributesResult
{
    IOStatus status;
    IOTerminalAttributes attributes;
} IOTerminalAttributesResult;

static inline IOTerminalAttributes IOTerminalAttributes__default(void)
{
    IOTerminalAttributes attributes = {
        .input_flags = IO_TERMINAL_INPUT_CR_TO_NL | IO_TERMINAL_INPUT_START_STOP_OUTPUT,
        .output_flags = IO_TERMINAL_OUTPUT_PROCESSING | IO_TERMINAL_OUTPUT_NL_TO_CR_NL,
        .control_flags = IO_TERMINAL_CONTROL_CHARACTER_SIZE_8,
        .local_flags = IO_TERMINAL_LOCAL_SIGNALS | IO_TERMINAL_LOCAL_CANONICAL | IO_TERMINAL_LOCAL_ECHO |
                       IO_TERMINAL_LOCAL_ECHO_ERASE | IO_TERMINAL_LOCAL_ECHO_KILL | IO_TERMINAL_LOCAL_EXTENDED,
        .input_speed = IO_TERMINAL_SPEED_38400,
        .output_speed = IO_TERMINAL_SPEED_38400,
    };
    attributes.control_characters[0] = 3;
    attributes.control_characters[1] = 28;
    attributes.control_characters[2] = 127;
    attributes.control_characters[3] = 21;
    attributes.control_characters[4] = 4;
    attributes.control_characters[6] = 1;
    attributes.control_characters[8] = 17;
    attributes.control_characters[9] = 19;
    attributes.control_characters[10] = 26;
    return attributes;
}

static inline IOTerminalAttributesResult IOTerminalAttributesResult__new(IOStatus status,
                                                                         IOTerminalAttributes attributes)
{
    return (IOTerminalAttributesResult){.status = status, .attributes = attributes};
}

typedef int64_t IOHandle;
typedef IOHandle * IOHandlePtr;
ResultDef(IOHandle);

typedef struct IOPipe
{
    IOHandle read;
    IOHandle write;
} IOPipe;

typedef IOPipe * IOPipePtr;
ResultDef(IOPipe);

static inline IOPipe IOPipe__new(IOHandle read, IOHandle write)
{
    return (IOPipe){.read = read, .write = write};
}

#define IO_ENDPOINT_TTY_INPUT (1LL << 60)
#define IO_ENDPOINT_CONSOLE_OUTPUT (IO_ENDPOINT_TTY_INPUT + 1)
#define IO_ENDPOINT_KEYBOARD_INPUT (IO_ENDPOINT_TTY_INPUT + 2)

static inline bool IOEndpoint__is_terminal(IOHandle handle)
{
    return handle >= IO_ENDPOINT_TTY_INPUT && handle <= IO_ENDPOINT_KEYBOARD_INPUT;
}

typedef int64_t ByteCount;
ResultDef(ByteCount);
typedef int64_t DirectoryEntryCount;
ResultDef(DirectoryEntryCount);
typedef uint64_t IOPosition;
ResultDef(IOPosition);
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

ResultDef(IOFileChange);

typedef struct IOFileWatch
{
    IOHandle handle;
    uint64_t generation;
} IOFileWatch;

static inline IOFileWatch IOFileWatch__new(IOHandle handle, uint64_t generation)
{
    return (IOFileWatch){.handle = handle, .generation = generation};
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
#define IO_MODE_TYPE_CHAR 0x2000U

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
