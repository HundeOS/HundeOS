#ifndef HUNDE_IO_COMPONENT_H
#define HUNDE_IO_COMPONENT_H

#include "host_component.h"
#include "process_snapshot.h"
#include "boot_info.h"
#include "syscalls/io.h"

#define HUNDE_IO_COMPONENT_VERSION 6U
#define HUNDE_IO_COMPONENT_ENTRY "hunde_io_component_entry_v6"
#define HUNDE_IO_PENDING 2U

typedef void * (*HundeHostAllocate)(uint64_t size, uint64_t alignment);
typedef void (*HundeHostDeallocate)(void * allocation, uint64_t size, uint64_t alignment);

typedef struct HundeCredentialsResult
{
    HundeAbiResultKind kind;
    uint32_t reserved;
    uint64_t user_id;
    uint64_t group_id;
    HundeAbiError error;
} HundeCredentialsResult;

typedef HundeCredentialsResult (*HundeCredentialsLookup)(uint64_t pid);

typedef struct HundeIOFileInfo
{
    uint32_t mode;
    uint32_t reserved;
    uint64_t user_id;
    uint64_t group_id;
    uint64_t size;
    uint64_t identity;
} HundeIOFileInfo;

typedef struct HundeIODirectoryEntry
{
    HundeIOFileInfo info;
    uint64_t name_length;
    uint8_t name[256];
} HundeIODirectoryEntry;

typedef struct HundeIOHostOperationsV6
{
    HundeAbiHeader header;
    HundeHostContext context;
    HundeHostAllocate allocate;
    HundeHostDeallocate deallocate;
    HundeHostFatal fatal;
    HundeCredentialsLookup credentials;
    HundeProcessLookup process;
    HundeTraceLoad trace;
    RustBootInfoResult (*boot_info)(void);
} HundeIOHostOperationsV6;

typedef const HundeIOHostOperationsV6 * ConstHundeIOHostOperationsV6Ptr;

typedef struct HundeIORequestV6
{
    uint32_t operation;
    uint32_t reserved;
    uint64_t session;
    uint64_t pid;
    int64_t handle;
    uint64_t flags;
    uint64_t count;
    uint8_t * read_data;
    const uint8_t * write_data;
    uint64_t capacity;
    uint64_t offset;
    uint64_t inheritance_token;
    uint64_t inheritance_pid;
    uint32_t buffered;
    uint32_t padding;
} HundeIORequestV6;

typedef const HundeIORequestV6 * ConstHundeIORequestV6Ptr;

typedef struct HundeIOCompletion
{
    HundeAbiResultKind kind;
    uint32_t reserved;
    int64_t value;
    int64_t pipe_read;
    int64_t pipe_write;
    uint64_t inheritance_flags;
    HundeIOFileInfo file_info;
    uint64_t directory_count;
    HundeIODirectoryEntry directory[3];
    IOFileChange file_change;
    HundeAbiError error;
} HundeIOCompletion;

typedef HundeIOCompletion (*HundeIOExecute)(HundeComponentContext context, ConstHundeIORequestV6Ptr request);
typedef HundeStatusResult (*HundeIODisconnect)(HundeComponentContext context, uint64_t session);

typedef struct HundeIOComponentV6
{
    HundeAbiHeader header;
    HundeComponentContext context;
    HundeIOExecute execute;
    HundeIODisconnect disconnect;
    HundeComponentDispose dispose;
} HundeIOComponentV6;

typedef struct HundeIOComponentLoadResult
{
    HundeAbiResultKind kind;
    uint32_t reserved;
    HundeIOComponentV6 component;
    HundeAbiError error;
} HundeIOComponentLoadResult;

typedef HundeIOComponentLoadResult (*HundeIOComponentEntryV6)(ConstHundeIOHostOperationsV6Ptr host);

_Static_assert(sizeof(HundeIOHostOperationsV6) == 72, "I/O host ABI changed");
_Static_assert(sizeof(HundeIORequestV6) == 104, "I/O request ABI changed");
_Static_assert(offsetof(HundeIORequestV6, read_data) == 48, "I/O request ABI changed");
_Static_assert(sizeof(HundeIOCompletion) == 1048, "I/O completion ABI changed");
_Static_assert(sizeof(IOFileChange) == 24, "File change ABI changed");
_Static_assert(offsetof(IOFileChange, generation) == 8, "File generation ABI changed");
_Static_assert(offsetof(IOFileChange, events) == 16, "File events ABI changed");
_Static_assert(sizeof(HundeIOComponentV6) == 40, "I/O component ABI changed");
_Static_assert(sizeof(HundeIOComponentLoadResult) == 72, "I/O load result ABI changed");

#endif
