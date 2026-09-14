#ifndef HUND_MEMORY_ACCOUNTING_H
#define HUND_MEMORY_ACCOUNTING_H
#include "types/linked_list.h"
#include "mutex.h"
#include "../common/process/process_registry.h"

typedef enum MemoryMappingKind
{
    MEMORY_PRIVATE,
    MEMORY_SHARED,
    MEMORY_DEVICE,
} MemoryMappingKind;

typedef struct MemoryMapping
{
    Address space;
    Address start;
    Address backing;
    size_t size;
    MemoryMappingKind kind;
} MemoryMapping;
OptionalDef(MemoryMapping);
typedef MemoryMapping * MemoryMappingPtr;
typedef const MemoryMapping * ConstMemoryMappingPtr;
MemoryMapping MemoryMapping__new(Address space, Address start, Address backing, size_t size, MemoryMappingKind kind);
bool MemoryMapping__equals(void * first, void * second);
LinkedListDefine(MemoryMapping, MemoryMappings, MemoryMapping__equals);

typedef struct MemoryAccounting
{
    mutex_t lock;
    MemoryMappings mappings;
    bool failed;
} MemoryAccounting;
typedef MemoryAccounting * MemoryAccountingPtr;
MemoryAccounting MemoryAccounting__new(void);
void MemoryAccounting__map(MemoryAccountingPtr self, ConstMemoryMappingPtr mapping);
void MemoryAccounting__unmap(MemoryAccountingPtr self, Address space, Address start, size_t size);
void MemoryAccounting__forget(MemoryAccountingPtr self, Address space);
Optional(ProcessMemoryUsage) MemoryAccounting__usage(MemoryAccountingPtr self, Address space);
#endif
