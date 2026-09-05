#ifndef __KERN_KALLOC_H__
#define __KERN_KALLOC_H__
#include <stddef.h>
#include <stdint.h>
#include "address.h"
#include "types/result.h"
#include "heap.h"

typedef struct Kernel Kernel;
typedef Kernel * KernelPtr;

#ifndef __KERNEL_KALLOC_CODE_DEFINED
#define __KERNEL_KALLOC_CODE_DEFINED
typedef uint64_t KAllocCode;
ResultDef(KAllocCode);
#endif

typedef enum KAllocCodes {
    KALLOC_OK = 0,
    KALLOC_NOT_IMPLEMENTED = -1,
    KALLOC_INVALID_ADDRESS = -2,
    KALLOC_ALLOCATION_FAILED = -3,
} KAllocCodes;

typedef struct KAllocSource {
    const char * file;
    const char * function;
    uint64_t line;
} KAllocSource;

static inline KAllocSource KAllocSource__new(const char * file, const char * function, uint64_t line)
{
    return (KAllocSource) {
        .file = file,
        .function = function,
        .line = line,
    };
}

Address Kernel__alloc_kernel(size_t size);
UserAddress Kernel__alloc_user(Address virtual_address,
                               size_t size,
                               bool read,
                               bool write,
                               bool execute);
Result(UserAddress) Kernel__try_alloc_user(Address virtual_address,
                                           size_t size,
                                           bool read,
                                           bool write,
                                           bool execute);
KernelAddress Kernel__alloc(size_t size);
KernelAddress Kernel__alloc_page(void);
KernelAddress Kernel__alloc_block(void);
KernelAddress Kernel__from_physical_address(KernelPtr self, Address physical);
KernelAddress Kernel__from_virtual_address(KernelPtr self, Address virtual_addr);
KernelAddress Kernel__try_from_virtual_address(KernelPtr self, Address virtual_addr);

Result(KAllocCode) Kernel__free(Address virtual_addr);

#if defined(HEAP_DEBUG) && !defined(KALLOC_IMPLEMENTATION) && !defined(KALLOC_DISABLE_MACROS)
KernelAddress Kernel__alloc_debug(size_t size, const KAllocSource * source);
KernelAddress Kernel__alloc_page_debug(const KAllocSource * source);
KernelAddress Kernel__alloc_block_debug(const KAllocSource * source);
Address Kernel__alloc_kernel_debug(size_t size, const KAllocSource * source);
UserAddress Kernel__alloc_user_debug(Address virtual_address,
                                     size_t size,
                                     bool read,
                                     bool write,
                                     bool execute,
                                     const KAllocSource * source);
Result(UserAddress) Kernel__try_alloc_user_debug(Address virtual_address,
                                                 size_t size,
                                                 bool read,
                                                 bool write,
                                                 bool execute,
                                                 const KAllocSource * source);
Result(KAllocCode) Kernel__free_debug(Address virtual_addr, const KAllocSource * source);

#define KALLOC_SOURCE KAllocSource__new(__FILE__, __func__, __LINE__)
#define Kernel__alloc(size) Kernel__alloc_debug((size), &(KAllocSource){ KALLOC_SOURCE })
#define Kernel__alloc_page() Kernel__alloc_page_debug(&(KAllocSource){ KALLOC_SOURCE })
#define Kernel__alloc_block() Kernel__alloc_block_debug(&(KAllocSource){ KALLOC_SOURCE })
#define Kernel__alloc_kernel(size) Kernel__alloc_kernel_debug((size), &(KAllocSource){ KALLOC_SOURCE })
#define Kernel__alloc_user(virtual_address, size, read, write, execute) \
    Kernel__alloc_user_debug((virtual_address), (size), (read), (write), (execute), &(KAllocSource){ KALLOC_SOURCE })
#define Kernel__try_alloc_user(virtual_address, size, read, write, execute) \
    Kernel__try_alloc_user_debug((virtual_address), (size), (read), (write), (execute), &(KAllocSource){ KALLOC_SOURCE })
#define Kernel__free(virtual_addr) Kernel__free_debug((virtual_addr), &(KAllocSource){ KALLOC_SOURCE })
#endif

void Kernel__memcopy(Address src, Address target, size_t size);
void Kernel__memset(Address addr, int val, size_t size);

#endif
