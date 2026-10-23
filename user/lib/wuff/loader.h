#ifndef __USER_LIB_WUFF_LOADER_H
#define __USER_LIB_WUFF_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include "../../../common/types/address.h"
#include "../../../common/types/bstring.h"
#include "../../../common/types/optional.h"
#include "../../../common/types/result.h"
#include "../../../common_includes/wuff.h"
#include "../types/fixed_array.h"
#include "syscalls/mem.h"
#include "elf.h"

#define WUFF_PAGE_SIZE 4096ULL
#define WUFF_MAX_MAPPINGS 16
#define WUFF_MAX_PROGRAM_HEADERS 64
#define WUFF_MAX_OBJECTS 16
#define WUFF_MAX_DEPENDENCIES 16

typedef enum WuffLoadCode
{
    WUFF_LOAD_OK = 0,
    WUFF_LOAD_INVALID_IMAGE = -1,
    WUFF_LOAD_UNSUPPORTED = -2,
    WUFF_LOAD_NO_MEMORY = -3,
    WUFF_LOAD_MAPPING_FAILED = -4,
    WUFF_LOAD_RELOCATION_FAILED = -5,
    WUFF_LOAD_PROTECTION_FAILED = -6,
    WUFF_LOAD_SYMBOL_NOT_FOUND = -7,
    WUFF_LOAD_CLEANUP_FAILED = -8,
    WUFF_LOAD_DEPENDENCY_NOT_FOUND = -9,
    WUFF_LOAD_IO_FAILED = -10,
} WuffLoadCode;

ResultDef(WuffLoadCode);
ResultDef(Address);

typedef struct WuffMemoryRegion
{
    Address virtual_address;
    uint8_t * writable_address;
} WuffMemoryRegion;

typedef WuffMemoryRegion * WuffMemoryRegionPtr;
typedef const WuffMemoryRegion * ConstWuffMemoryRegionPtr;

ResultDef(WuffMemoryRegion);
WuffMemoryRegion WuffMemoryRegion__new(Address virtual_address, uint8_t * writable_address);

typedef Result(WuffMemoryRegion) (*WuffAllocateAligned)(void * context, size_t size, size_t alignment);
typedef MemStatus (*WuffProtect)(void * context, Address address, size_t size, MemPagePermissions permissions);
typedef MemStatus (*WuffFree)(void * context, Address address);

typedef struct WuffMemoryOperations
{
    void * context;
    WuffAllocateAligned allocate_aligned;
    WuffProtect protect;
    WuffFree free;
} WuffMemoryOperations;

typedef const WuffMemoryOperations * ConstWuffMemoryOperationsPtr;

WuffMemoryOperations WuffMemoryOperations__new(void * context, WuffAllocateAligned allocate_aligned,
                                               WuffProtect protect, WuffFree free);

typedef struct WuffMapping
{
    Address virtual_address;
    uint8_t * writable_address;
    size_t size;
    MemPagePermissions final_permissions;
} WuffMapping;

typedef WuffMapping * WuffMappingPtr;

bool WuffMapping__equals(WuffMappingPtr self, WuffMappingPtr other);
OptionalDef(WuffMapping);
FixedArrayDefine(WuffMapping, WUFF_MAX_MAPPINGS, WuffMappings, (EqualFunction)WuffMapping__equals);

typedef struct WuffSymbol
{
    Address address;
    size_t size;
} WuffSymbol;

OptionalDef(WuffSymbol);
ResultDef(WuffSymbol);

typedef struct WuffObject
{
    Address load_bias;
    Address entry;
    Optional(WuffMapping) image_mapping;
    WuffMappings mappings;
    WuffMemoryOperations memory;
    const WuffSymbolEntry * symbols;
    size_t symbol_count;
    const char * strings;
    size_t string_size;
} WuffObject;

typedef WuffObject * WuffObjectPtr;
typedef const WuffObject * ConstWuffObjectPtr;

ResultDef(WuffObject);

typedef struct WuffImage
{
    BString name;
    const uint8_t * data;
    size_t size;
} WuffImage;

typedef const WuffImage * ConstWuffImagePtr;

ResultDef(WuffImage);
WuffImage WuffImage__new(ConstBStringPtr name, const uint8_t * data, size_t size);

typedef Result(WuffImage) (*WuffDependencyProvider)(void * context, ConstBStringPtr name);
typedef void (*WuffDependencyRelease)(void * context, ConstWuffImagePtr image);

typedef struct WuffDependencyOperations
{
    void * context;
    WuffDependencyProvider load;
    WuffDependencyRelease release;
} WuffDependencyOperations;

typedef const WuffDependencyOperations * ConstWuffDependencyOperationsPtr;

WuffDependencyOperations WuffDependencyOperations__new(void * context, WuffDependencyProvider load,
                                                       WuffDependencyRelease release);

typedef struct WuffLoadedObject
{
    BString name;
    WuffObject object;
} WuffLoadedObject;

typedef WuffLoadedObject * WuffLoadedObjectPtr;

bool WuffLoadedObject__equals(WuffLoadedObjectPtr self, WuffLoadedObjectPtr other);
OptionalDef(WuffLoadedObject);
FixedArrayDefine(WuffLoadedObject, WUFF_MAX_OBJECTS, WuffLoadedObjects, (EqualFunction)WuffLoadedObject__equals);

typedef struct WuffGraph
{
    WuffLoadedObjects objects;
} WuffGraph;

typedef WuffGraph * WuffGraphPtr;
typedef const WuffGraph * ConstWuffGraphPtr;

ResultDef(WuffGraph);

WuffMemoryOperations WuffMemoryOperations__system(void);
Result(WuffObject) WuffLoader__load(const uint8_t * image, size_t image_size, ConstWuffMemoryOperationsPtr memory);
Result(WuffGraph) WuffLoader__load_graph(ConstWuffImagePtr root, ConstWuffMemoryOperationsPtr memory,
                                         ConstWuffDependencyOperationsPtr dependencies);
Result(WuffGraph) WuffLoader__load_module_graph(ConstWuffImagePtr root, ConstWuffMemoryOperationsPtr memory,
                                                ConstWuffDependencyOperationsPtr dependencies);
Result(WuffLoadCode) WuffObject__dispose(WuffObjectPtr self);
Optional(WuffSymbol) WuffObject__find_symbol(ConstWuffObjectPtr self, ConstBStringPtr name);
Result(WuffLoadCode) WuffGraph__dispose(WuffGraphPtr self);
Optional(WuffSymbol) WuffGraph__find_symbol(ConstWuffGraphPtr self, ConstBStringPtr name);
Result(WuffSymbol) WuffGraph__find_root_export(ConstWuffGraphPtr self, ConstBStringPtr name);
Result(Address) WuffGraph__entry(ConstWuffGraphPtr self);

#endif
