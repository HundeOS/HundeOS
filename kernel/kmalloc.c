#include "kmalloc.h"

#include <stdint.h>

#include "memory_page.h"
#include "mutex.h"

#define KERNEL_MALLOC_ALIGNMENT 16U
#define KERNEL_MALLOC_CLASS_COUNT 7U
#define KERNEL_MALLOC_BITMAP_WORDS 2U
#define KERNEL_MALLOC_MAGIC 0x4b4d414c4c4f4341ULL
#define KERNEL_MALLOC_SLAB_MAGIC 0x4b4d534c41425047ULL

OptionalDef(size_t);

typedef struct KernelMallocSlab KernelMallocSlab;
typedef KernelMallocSlab * KernelMallocSlabPtr;
OptionalDef(KernelMallocSlabPtr);

typedef struct KernelMallocAllocation
{
    uint64_t magic;
    Optional(KernelMallocSlabPtr) slab;
} KernelMallocAllocation;

typedef KernelMallocAllocation * KernelMallocAllocationPtr;

#define KERNEL_MALLOC_ALLOCATION_HEADER_SIZE                                                                           \
    ((sizeof(KernelMallocAllocation) + KERNEL_MALLOC_ALIGNMENT - 1U) & ~(KERNEL_MALLOC_ALIGNMENT - 1U))

struct KernelMallocSlab
{
    uint64_t magic;
    KernelAddress page;
    Optional(KernelMallocSlabPtr) next;
    size_t slot_size;
    size_t slot_offset;
    uint8_t class_index;
    uint16_t slot_count;
    uint16_t free_count;
    uint64_t free_bitmap[KERNEL_MALLOC_BITMAP_WORDS];
};

typedef struct KernelMalloc
{
    mutex_t lock;
    Optional(KernelMallocSlabPtr) slabs[KERNEL_MALLOC_CLASS_COUNT];
} KernelMalloc;

static KernelMalloc kernel_malloc;

static BString invalid_malloc_address = bstr("Invalid kernel malloc address");

static size_t KernelMalloc__align_up(size_t value, size_t alignment)
{
    return (value + alignment - 1U) & ~(alignment - 1U);
}

static Optional(size_t) KernelMalloc__class_for_size(size_t size)
{
    if (size > SIZE_MAX - KERNEL_MALLOC_ALLOCATION_HEADER_SIZE - (KERNEL_MALLOC_ALIGNMENT - 1U))
    {
        return None(size_t);
    }

    size_t required = KernelMalloc__align_up(size + KERNEL_MALLOC_ALLOCATION_HEADER_SIZE, KERNEL_MALLOC_ALIGNMENT);
    size_t slot_size = 32U;
    for (size_t class_index = 0; class_index < KERNEL_MALLOC_CLASS_COUNT; class_index++)
    {
        if (required <= slot_size)
        {
            return Some(size_t, class_index);
        }
        slot_size *= 2U;
    }
    return None(size_t);
}

static size_t KernelMalloc__slot_size(size_t class_index)
{
    return 32ULL << class_index;
}

static KernelMallocSlab KernelMallocSlab__new(KernelAddress page, size_t slot_size, size_t slot_offset,
                                              size_t class_index, uint16_t slot_count)
{
    return (KernelMallocSlab){
        .magic = KERNEL_MALLOC_SLAB_MAGIC,
        .page = page,
        .next = None(KernelMallocSlabPtr),
        .slot_size = slot_size,
        .slot_offset = slot_offset,
        .class_index = (uint8_t)class_index,
        .slot_count = slot_count,
        .free_count = slot_count,
    };
}

static KernelMallocAllocation KernelMallocAllocation__new_small(KernelMallocSlabPtr slab)
{
    return (KernelMallocAllocation){
        .magic = KERNEL_MALLOC_MAGIC,
        .slab = Some(KernelMallocSlabPtr, slab),
    };
}

static KernelMallocAllocation KernelMallocAllocation__new_large(void)
{
    return (KernelMallocAllocation){
        .magic = KERNEL_MALLOC_MAGIC,
        .slab = None(KernelMallocSlabPtr),
    };
}

static void KernelMallocSlab__init(KernelMallocSlabPtr self, KernelAddress page, size_t class_index)
{
    size_t slot_offset = KernelMalloc__align_up(sizeof(KernelMallocSlab), KERNEL_MALLOC_ALIGNMENT);
    size_t slot_size = KernelMalloc__slot_size(class_index);
    uint16_t slot_count = (uint16_t)((PAGE_SIZE - slot_offset) / slot_size);

    *self = KernelMallocSlab__new(page, slot_size, slot_offset, class_index, slot_count);
    for (uint16_t slot = 0; slot < slot_count; slot++)
    {
        self->free_bitmap[slot / 64U] |= 1ULL << (slot % 64U);
    }
}

static Optional(KernelMallocSlabPtr) KernelMalloc__available_slab(size_t class_index)
{
    Optional(KernelMallocSlabPtr) current = kernel_malloc.slabs[class_index];
    while (is_something(current))
    {
        KernelMallocSlabPtr slab;
        match_optional(current,
            some(value)
            {
                slab = value;
            },
            none()
            {
                return None(KernelMallocSlabPtr);
            }
        );
        if (slab->free_count > 0)
        {
            return Some(KernelMallocSlabPtr, slab);
        }
        current = slab->next;
    }
    return None(KernelMallocSlabPtr);
}

static KernelAddress KernelMallocSlab__allocate(KernelMallocSlabPtr self)
{
    for (size_t word_index = 0; word_index < KERNEL_MALLOC_BITMAP_WORDS; word_index++)
    {
        uint64_t available = self->free_bitmap[word_index];
        if (available == 0)
        {
            continue;
        }

        size_t bit_index = 0;
        while ((available & (1ULL << bit_index)) == 0)
        {
            bit_index++;
        }
        size_t slot_index = word_index * 64U + bit_index;
        if (slot_index >= self->slot_count)
        {
            return INVALID_KERNEL_ADDR;
        }

        self->free_bitmap[word_index] &= ~(1ULL << bit_index);
        self->free_count--;
        Address allocation_address = self->page.virtual_addr + self->slot_offset + slot_index * self->slot_size;
        KernelMallocAllocationPtr allocation = ptr(allocation_address, KernelMallocAllocation);
        *allocation = KernelMallocAllocation__new_small(self);
        size_t payload_offset = allocation_address - self->page.virtual_addr + KERNEL_MALLOC_ALLOCATION_HEADER_SIZE;
        return KERNEL_ADDR(allocation_address + KERNEL_MALLOC_ALLOCATION_HEADER_SIZE,
                           self->page.phys_addr + payload_offset);
    }
    return INVALID_KERNEL_ADDR;
}

static KernelAddress KernelMalloc__allocate_small(size_t class_index)
{
    KernelAddress allocation = INVALID_KERNEL_ADDR;
    critical_section(
        kernel_malloc.lock,
        {
            match_optional(
                KernelMalloc__available_slab(class_index),
                some(slab)
                {
                    allocation = KernelMallocSlab__allocate(slab);
                },
                none_do_nothing()
            );
        }
    );
    if (allocation.valid)
    {
        return allocation;
    }

    KernelAddress page = Kernel__alloc_page();
    if (!page.valid)
    {
        return INVALID_KERNEL_ADDR;
    }
    KernelMallocSlabPtr slab = v_ptr(page, KernelMallocSlab);
    KernelMallocSlab__init(slab, page, class_index);

    critical_section(
        kernel_malloc.lock,
        {
            slab->next = kernel_malloc.slabs[class_index];
            kernel_malloc.slabs[class_index] = Some(KernelMallocSlabPtr, slab);
            allocation = KernelMallocSlab__allocate(slab);
        }
    );
    return allocation;
}

static KernelAddress KernelMalloc__allocate_large(size_t size)
{
    if (size > SIZE_MAX - KERNEL_MALLOC_ALLOCATION_HEADER_SIZE)
    {
        return INVALID_KERNEL_ADDR;
    }
    KernelAddress backing = Kernel__alloc(size + KERNEL_MALLOC_ALLOCATION_HEADER_SIZE);
    if (!backing.valid)
    {
        return INVALID_KERNEL_ADDR;
    }

    KernelMallocAllocationPtr allocation = v_ptr(backing, KernelMallocAllocation);
    *allocation = KernelMallocAllocation__new_large();
    return KERNEL_ADDR(backing.virtual_addr + KERNEL_MALLOC_ALLOCATION_HEADER_SIZE,
                       backing.phys_addr + KERNEL_MALLOC_ALLOCATION_HEADER_SIZE);
}

KernelAddress Kernel__malloc(size_t size)
{
    if (size == 0)
    {
        return INVALID_KERNEL_ADDR;
    }
    match_optional(
        KernelMalloc__class_for_size(size),
        some(class_index)
        {
            return KernelMalloc__allocate_small(class_index);
        },
        none()
        {
            return KernelMalloc__allocate_large(size);
        }
    );
}

static bool KernelMalloc__unlink_slab(size_t class_index, KernelMallocSlabPtr target)
{
    Optional(KernelMallocSlabPtr) current = kernel_malloc.slabs[class_index];
    Optional(KernelMallocSlabPtr) previous = None(KernelMallocSlabPtr);
    while (is_something(current))
    {
        KernelMallocSlabPtr slab;
        match_optional(current,
            some(value)
            {
                slab = value;
            },
            none()
            {
                return false;
            }
        );
        if (slab == target)
        {
            match_optional(
                previous,
                some(previous_slab)
                {
                    previous_slab->next = slab->next;
                },
                none()
                {
                    kernel_malloc.slabs[class_index] = slab->next;
                }
            );
            return true;
        }

        previous = current;
        current = slab->next;
    }
    return false;
}

static bool KernelMalloc__has_other_slab(size_t class_index, KernelMallocSlabPtr target)
{
    Optional(KernelMallocSlabPtr) current = kernel_malloc.slabs[class_index];
    while (is_something(current))
    {
        KernelMallocSlabPtr slab;
        match_optional(current,
            some(value)
            {
                slab = value;
            },
            none()
            {
                return false;
            }
        );
        if (slab != target)
        {
            return true;
        }

        current = slab->next;
    }
    return false;
}

static Result(KAllocCode) KernelMalloc__free_small(KernelMallocAllocationPtr allocation, KernelMallocSlabPtr slab)
{
    bool valid = false;
    bool release_page = false;
    size_t class_index = 0;

    critical_section(
        kernel_malloc.lock,
        {
            if (slab->magic == KERNEL_MALLOC_SLAB_MAGIC && slab->slot_size >= 32U)
            {
                class_index = slab->class_index;
                Address allocation_address = addr(allocation);
                Address slots_start = slab->page.virtual_addr + slab->slot_offset;
                if (class_index < KERNEL_MALLOC_CLASS_COUNT && allocation_address >= slots_start &&
                    allocation_address < slab->page.virtual_addr + PAGE_SIZE &&
                    (allocation_address - slots_start) % slab->slot_size == 0)
                {
                    size_t slot_index = (allocation_address - slots_start) / slab->slot_size;
                    size_t word_index = slot_index / 64U;
                    uint64_t bit = 1ULL << (slot_index % 64U);
                    if (slot_index < slab->slot_count && (slab->free_bitmap[word_index] & bit) == 0)
                    {
                        allocation->magic = 0;
                        slab->free_bitmap[word_index] |= bit;
                        slab->free_count++;
                        valid = true;
                        if (slab->free_count == slab->slot_count &&
                            KernelMalloc__has_other_slab(class_index, slab) &&
                            KernelMalloc__unlink_slab(class_index, slab))
                        {
                            slab->magic = 0;
                            release_page = true;
                        }
                    }
                }
            }
        }
    );

    if (!valid)
    {
        return Error(KAllocCode, invalid_malloc_address, KALLOC_INVALID_ADDRESS);
    }
    if (release_page)
    {
        return Kernel__free(slab->page.virtual_addr);
    }
    return Success(KAllocCode, KALLOC_OK);
}

Result(KAllocCode) Kernel__free_malloc(Address payload)
{
    if (payload < KERNEL_MALLOC_ALLOCATION_HEADER_SIZE)
    {
        return Error(KAllocCode, invalid_malloc_address, KALLOC_INVALID_ADDRESS);
    }
    KernelMallocAllocationPtr allocation = ptr(payload - KERNEL_MALLOC_ALLOCATION_HEADER_SIZE, KernelMallocAllocation);
    if (allocation->magic != KERNEL_MALLOC_MAGIC)
    {
        return Error(KAllocCode, invalid_malloc_address, KALLOC_INVALID_ADDRESS);
    }

    match_optional(
        allocation->slab,
        some(slab)
        {
            return KernelMalloc__free_small(allocation, slab);
        },
        none()
        {
            allocation->magic = 0;
            return Kernel__free(addr(allocation));
        }
    );
}
