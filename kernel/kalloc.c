#define KALLOC_IMPLEMENTATION
#include "kalloc.h"

#include <stdint.h>

#include "memory.h"
#include "debug.h"
#include "address.h"
#include "kernel_core.h"

static BString not_implemented = bstr("Not implemented");
static BString invalid_address = bstr("Invalid address");
static BString allocation_failed = bstr("Allocation failed");

inline HeapPtr heap_ptr()
{
    KernelPtr kernel = Kernel__instance();
    return &kernel->heap;
}

KernelAddress Kernel__from_physical_address(KernelPtr self, Address physical)
{
    match_optional(
        self->system_info,
        some(system_info)
        {
            KernelAddress address = SystemInfo__kernel_address_for_physical(&system_info, physical);
            if (address.valid)
            {
                return address;
            }
        },
        none_do_nothing()
    );

    return Heap__from_physical_address(heap_ptr(), physical);
}

KernelAddress Kernel__from_virtual_address(KernelPtr self, Address virtual_addr)
{
    match_optional(
        self->system_info,
        some(system_info)
        {
            KernelAddress address = SystemInfo__kernel_address_for_virtual(&system_info, virtual_addr);
            if (address.valid)
            {
                return address;
            }
        },
        none_do_nothing()
    );

    return Heap__from_virtual_address(heap_ptr(), virtual_addr);
}

KernelAddress Kernel__try_from_virtual_address(KernelPtr self, Address virtual_addr)
{
    match_optional(
        self->system_info,
        some(system_info)
        {
            KernelAddress address = SystemInfo__kernel_address_for_virtual(&system_info, virtual_addr);
            if (address.valid)
            {
                return address;
            }
        },
        none_do_nothing()
    );

    return Heap__try_from_virtual_address(heap_ptr(), virtual_addr);
}

KernelAddress Kernel__alloc(size_t size)
{
    return Heap__alloc_size(heap_ptr(), size);
}

KernelAddress Kernel__alloc_page(void)
{
    return Heap__alloc(heap_ptr());
}

KernelAddress Kernel__alloc_block(void)
{
    HeapPtr heap = heap_ptr();
    return Heap__alloc_size(heap, heap->coarse_block_size);
}

Address Kernel__alloc_kernel(size_t size)
{
    KernelAddress address = Kernel__alloc(size);
    __d_printf(MEMORY, "kalloc kind=kernel size=%i addr=%p phys=%p valid=%i\n", size, address.virtual_addr,
               address.phys_addr, (int)address.valid);

    return address.virtual_addr;
}

UserAddress Kernel__alloc_user(Address virtual_address, size_t size, bool read, bool write, bool execute)
{
    __d_println(DEBUG, "Kernel__alloc(size)");
    KernelAddress address = Kernel__alloc(size);
    __d_printf(MEMORY, "kalloc kind=user requested_virt=%p size=%i addr=%p phys=%p valid=%i perms=%i%i%i\n",
               virtual_address, size, address.virtual_addr, address.phys_addr, (int)address.valid, (int)read,
               (int)write, (int)execute);
    UserAddress allocated = UserAddress__new(virtual_address, address, read, write, execute, true);
    return allocated;
}

Result(UserAddress) Kernel__try_alloc_user(Address virtual_address, size_t size, bool read, bool write, bool execute)
{
    KernelAddress address = Heap__try_alloc_size(heap_ptr(), size);
    __d_printf(MEMORY, "kalloc try kind=user requested_virt=%p size=%i addr=%p phys=%p valid=%i perms=%i%i%i\n",
               virtual_address, size, address.virtual_addr, address.phys_addr, (int)address.valid, (int)read,
               (int)write, (int)execute);

    if (!address.valid)
    {
        return Error(UserAddress, allocation_failed, KALLOC_ALLOCATION_FAILED);
    }

    return Success(UserAddress, UserAddress__new(virtual_address, address, read, write, execute, true));
}

Result(UserAddress) Kernel__try_alloc_user_aligned(Address virtual_address, size_t size, size_t alignment, bool read,
                                                   bool write, bool execute)
{
    KernelAddress address = Heap__try_alloc_size_aligned(heap_ptr(), size, alignment);
    if (!address.valid)
    {
        return Error(UserAddress, allocation_failed, KALLOC_ALLOCATION_FAILED);
    }

    return Success(UserAddress, UserAddress__new(virtual_address, address, read, write, execute, true));
}

Result(KAllocCode) Kernel__free(Address virtual_address)
{
    KernelAddress address = Kernel__from_virtual_address(Kernel__instance(), virtual_address);
    if (!address.valid)
    {
        __d_printf(MEMORY, "kfree invalid addr=%p\n", virtual_address);
        return Error(KAllocCode, invalid_address, KALLOC_INVALID_ADDRESS);
    }

    match_result(
        Heap__free(heap_ptr(), address),
        success(_)
        {
            __d_printf(MEMORY, "kfree addr=%p phys=%p\n", address.virtual_addr, address.phys_addr);
            return Success(KAllocCode, KALLOC_OK);
        },
        error(code, message)
        {
            return Error(KAllocCode, message, code);
        }
    );

    return Error(KAllocCode, not_implemented, KALLOC_NOT_IMPLEMENTED);
}

#ifdef HEAP_DEBUG
KernelAddress Kernel__alloc_debug(size_t size, ConstKAllocSourcePtr source)
{
    KernelAddress address = Kernel__alloc(size);
    __d_printf(MEMORY, "kalloc trace call=Kernel__alloc size=%i addr=%p phys=%p valid=%i source=%s:%i function=%s\n",
               size, address.virtual_addr, address.phys_addr, (int)address.valid, source->file, source->line,
               source->function);
    return address;
}

KernelAddress Kernel__alloc_page_debug(ConstKAllocSourcePtr source)
{
    KernelAddress address = Kernel__alloc_page();
    __d_printf(MEMORY,
               "kalloc trace call=Kernel__alloc_page size=%i addr=%p phys=%p valid=%i source=%s:%i function=%s\n",
               PAGE_SIZE, address.virtual_addr, address.phys_addr, (int)address.valid, source->file, source->line,
               source->function);
    return address;
}

KernelAddress Kernel__alloc_block_debug(ConstKAllocSourcePtr source)
{
    HeapPtr heap = heap_ptr();
    KernelAddress address = Kernel__alloc_block();
    __d_printf(MEMORY,
               "kalloc trace call=Kernel__alloc_block size=%i addr=%p phys=%p valid=%i source=%s:%i function=%s\n",
               heap->coarse_block_size, address.virtual_addr, address.phys_addr, (int)address.valid, source->file,
               source->line, source->function);
    return address;
}

Address Kernel__alloc_kernel_debug(size_t size, ConstKAllocSourcePtr source)
{
    Address address = Kernel__alloc_kernel(size);
    __d_printf(MEMORY, "kalloc trace call=Kernel__alloc_kernel size=%i addr=%p source=%s:%i function=%s\n", size,
               address, source->file, source->line, source->function);
    return address;
}

UserAddress Kernel__alloc_user_debug(Address virtual_address, size_t size, bool read, bool write, bool execute,
                                     ConstKAllocSourcePtr source)
{
    UserAddress address = Kernel__alloc_user(virtual_address, size, read, write, execute);
    __d_printf(
        MEMORY,
        "kalloc trace call=Kernel__alloc_user requested_virt=%p size=%i addr=%p phys=%p valid=%i perms=%i%i%i source=%s:%i function=%s\n",
        virtual_address, size, address.kernel_addr.virtual_addr, address.kernel_addr.phys_addr, (int)address.valid,
        (int)read, (int)write, (int)execute, source->file, source->line, source->function);
    return address;
}

Result(UserAddress) Kernel__try_alloc_user_debug(Address virtual_address, size_t size, bool read, bool write,
                                                 bool execute, ConstKAllocSourcePtr source)
{
    Result(UserAddress) result = Kernel__try_alloc_user(virtual_address, size, read, write, execute);
    match_result(
        result,
        success(address)
        {
            __d_printf(
                MEMORY,
                "kalloc trace call=Kernel__try_alloc_user result=success requested_virt=%p size=%i addr=%p phys=%p valid=%i perms=%i%i%i source=%s:%i function=%s\n",
                virtual_address, size, address.kernel_addr.virtual_addr, address.kernel_addr.phys_addr,
                (int)address.valid, (int)read, (int)write, (int)execute, source->file, source->line, source->function);
        },
        error(code, message)
        {
            __d_printf(
                MEMORY,
                "kalloc trace call=Kernel__try_alloc_user result=error code=%i requested_virt=%p size=%i source=%s:%i function=%s\n",
                code, virtual_address, size, source->file, source->line, source->function);
        }
    );
    return result;
}

Result(KAllocCode) Kernel__free_debug(Address virtual_address, ConstKAllocSourcePtr source)
{
    Result(KAllocCode) result = Kernel__free(virtual_address);
    match_result(
        result,
        success(_)
        {
            __d_printf(MEMORY, "kfree trace result=success addr=%p source=%s:%i function=%s\n", virtual_address,
                       source->file, source->line, source->function);
        },
        error(code, message)
        {
            __d_printf(MEMORY, "kfree trace result=error code=%i addr=%p source=%s:%i function=%s\n", code,
                       virtual_address, source->file, source->line, source->function);
        }
    );
    return result;
}
#endif

void * memcpy(void * dst, const void * src, size_t n)
{
    return Mem__memcpy(dst, src, n);
}

void * memset(void * dst, int c, size_t n)
{
    return Mem__memset(dst, c, n);
}

void Kernel__memcopy(Address src, Address target, size_t size)
{
    Mem__memcpy(void_ptr(target), (const void *)src, size);
}

void Kernel__memset(Address addr, int val, size_t size)
{
    Mem__memset(void_ptr(addr), val, size);
}
