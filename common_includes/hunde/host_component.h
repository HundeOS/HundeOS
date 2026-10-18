#ifndef HUNDE_HOST_COMPONENT_H
#define HUNDE_HOST_COMPONENT_H

#include <stddef.h>
#include <stdint.h>

#define HUNDE_HOST_COMPONENT_ABI_VERSION 1U
#define HUNDE_HOST_COMPONENT_ENTRY_NAME "hunde_component_entry_v1"

typedef struct HundeAbiHeader
{
    uint32_t size;
    uint32_t version;
} HundeAbiHeader;

typedef struct HundeByteSpan
{
    const uint8_t * bytes;
    uint64_t length;
} HundeByteSpan;

typedef uint32_t HundeAbiResultKind;

enum
{
    HUNDE_ABI_SUCCESS = 0,
    HUNDE_ABI_ERROR = 1,
};

typedef struct HundeAbiError
{
    int32_t code;
    uint32_t reserved;
    HundeByteSpan message;
} HundeAbiError;

typedef struct HundeStatusResult
{
    HundeAbiResultKind kind;
    uint32_t reserved;
    HundeAbiError error;
} HundeStatusResult;

typedef void * HundeHostContext;
typedef void * HundeComponentContext;

typedef HundeStatusResult (*HundeHostLog)(HundeHostContext context, HundeByteSpan message);
typedef void (*HundeHostFatal)(HundeHostContext context, HundeByteSpan message);

typedef struct HundeHostOperationsV1
{
    HundeAbiHeader header;
    HundeHostContext context;
    HundeHostLog log;
    HundeHostFatal fatal;
} HundeHostOperationsV1;

typedef const HundeHostOperationsV1 * ConstHundeHostOperationsV1Ptr;

typedef HundeStatusResult (*HundeComponentInitialize)(HundeComponentContext context);
typedef HundeStatusResult (*HundeComponentDispose)(HundeComponentContext context);

typedef struct HundeComponentDescriptorV1
{
    HundeAbiHeader header;
    HundeComponentContext context;
    HundeComponentInitialize initialize;
    HundeComponentDispose dispose;
} HundeComponentDescriptorV1;

typedef struct HundeComponentLoadResult
{
    HundeAbiResultKind kind;
    uint32_t reserved;
    HundeComponentDescriptorV1 component;
    HundeAbiError error;
} HundeComponentLoadResult;

typedef HundeComponentLoadResult (*HundeComponentEntryV1)(ConstHundeHostOperationsV1Ptr host);

static inline HundeByteSpan HundeByteSpan__new(const uint8_t * bytes, uint64_t length)
{
    return (HundeByteSpan){.bytes = bytes, .length = length};
}

static inline HundeStatusResult HundeStatusResult__success(void)
{
    return (HundeStatusResult){.kind = HUNDE_ABI_SUCCESS};
}

static inline HundeStatusResult HundeStatusResult__error(int32_t code, HundeByteSpan message)
{
    return (HundeStatusResult){.kind = HUNDE_ABI_ERROR, .error = {.code = code, .message = message}};
}

static inline HundeComponentLoadResult HundeComponentLoadResult__success(HundeComponentDescriptorV1 component)
{
    return (HundeComponentLoadResult){.kind = HUNDE_ABI_SUCCESS, .component = component};
}

static inline HundeComponentLoadResult HundeComponentLoadResult__error(int32_t code, HundeByteSpan message)
{
    return (HundeComponentLoadResult){.kind = HUNDE_ABI_ERROR, .error = {.code = code, .message = message}};
}

_Static_assert(sizeof(void *) == 8, "Host components require a 64-bit ABI");
_Static_assert(sizeof(HundeAbiHeader) == 8, "HundeAbiHeader ABI changed");
_Static_assert(sizeof(HundeByteSpan) == 16, "HundeByteSpan ABI changed");
_Static_assert(offsetof(HundeByteSpan, length) == 8, "HundeByteSpan ABI changed");
_Static_assert(sizeof(HundeAbiError) == 24, "HundeAbiError ABI changed");
_Static_assert(offsetof(HundeAbiError, message) == 8, "HundeAbiError ABI changed");
_Static_assert(sizeof(HundeStatusResult) == 32, "HundeStatusResult ABI changed");
_Static_assert(offsetof(HundeStatusResult, error) == 8, "HundeStatusResult ABI changed");
_Static_assert(sizeof(HundeHostOperationsV1) == 32, "HundeHostOperationsV1 ABI changed");
_Static_assert(offsetof(HundeHostOperationsV1, context) == 8, "HundeHostOperationsV1 ABI changed");
_Static_assert(sizeof(HundeComponentDescriptorV1) == 32, "HundeComponentDescriptorV1 ABI changed");
_Static_assert(offsetof(HundeComponentDescriptorV1, context) == 8, "HundeComponentDescriptorV1 ABI changed");
_Static_assert(sizeof(HundeComponentLoadResult) == 64, "HundeComponentLoadResult ABI changed");
_Static_assert(offsetof(HundeComponentLoadResult, component) == 8, "HundeComponentLoadResult ABI changed");
_Static_assert(offsetof(HundeComponentLoadResult, error) == 40, "HundeComponentLoadResult ABI changed");

#endif
