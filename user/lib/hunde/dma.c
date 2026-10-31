#include "dma.h"

#include "sys/mem.h"
#include "sys/service.h"
#include "sys/sync.h"

static HundeDmaAllocation HundeDmaAllocation__new(HundeStatusResult status, uint64_t address, uint64_t device_address)
{
    return (HundeDmaAllocation){.status = status, .address = address, .device_address = device_address};
}

static HundeDmaAllocation HundeDmaAllocation__error(int32_t code)
{
    return HundeDmaAllocation__new(RustNative__error(code), 0, 0);
}

HundeDmaAllocation hunde_dma_allocate(uint64_t size, uint64_t alignment)
{
    if (size == 0 || size > SIZE_MAX || alignment < MEM_SHARED_PAGE_SIZE || alignment % MEM_SHARED_PAGE_SIZE != 0 ||
        size % MEM_SHARED_PAGE_SIZE != 0)
    {
        return HundeDmaAllocation__error(SERVICE_INVALID_BUFFER);
    }

    match_result(
        mem_alloc_aligned_pages_result((size_t)size, (size_t)alignment),
        success(allocation)
        {
            uint64_t first = 0;
            ServiceCode code = service_device_address((void *)allocation, &first);
            if (!service_ok(code))
            {
                mem_free_pages((void *)allocation);
                return HundeDmaAllocation__error(code);
            }
            for (uint64_t offset = MEM_SHARED_PAGE_SIZE; offset < size; offset += MEM_SHARED_PAGE_SIZE)
            {
                uint64_t current = 0;
                code = service_device_address((void *)((Address)allocation + offset), &current);
                if (!service_ok(code) || current != first + offset)
                {
                    mem_free_pages((void *)allocation);
                    return HundeDmaAllocation__error(SERVICE_INVALID_BUFFER);
                }
            }
            return HundeDmaAllocation__new(HundeStatusResult__success(), (uint64_t)allocation, first);
        },
        error(code, message)
        {
            return HundeDmaAllocation__error(code);
        }
    );
    return HundeDmaAllocation__error(SERVICE_INVALID_BUFFER);
}

void hunde_dma_clean(uint64_t address, uint64_t size)
{
    __sync_cache_clean((void *)address, size);
}

void hunde_dma_invalidate(uint64_t address, uint64_t size)
{
    __sync_cache_invalidate((void *)address, size);
}

void hunde_dma_clean_invalidate(uint64_t address, uint64_t size)
{
    __sync_cache_clean_invalidate((void *)address, size);
}

void hunde_dma_boundary(void)
{
    __sync_boundary();
}
