#include <stdlib.h>
#include "../../../kernel/address.h"
#include "../../../kernel/arch/page_table.h"
#include "../../../kernel/types/optional.h"
#include "../../../kernel/types/comparison.h"
#include "../../../kernel/types/fixed_array.h"

#include "../../../kernel/debug.h"

typedef struct MapRequest {
    UserAddress address;
    size_t size;
} MapRequest;

typedef MapRequest * MapRequestPtr;

bool MapRequest__equals(MapRequestPtr self, MapRequestPtr other)
{
    return self->address.virtual_addr == other->address.virtual_addr && self->size == other->size;
}

static bool MapRequest__is_covered_by_unmap(MapRequestPtr mapped_address, Address virtual_address, size_t size)
{
    Address unmap_start = virtual_address;
    Address unmap_end = virtual_address + size;
    Address mapped_start = mapped_address->address.virtual_addr;
    Address mapped_end = mapped_start + mapped_address->size;

    return mapped_start >= unmap_start && mapped_end <= unmap_end;
}

OptionalDef(MapRequest);
FixedArrayDefine(MapRequest, 1024, MapRequestArray, (EqualFunction) MapRequest__equals);
FixedArrayBodyDefine(MapRequestArray);

typedef struct PageTable
{
    MapRequestArray mapped_addresses;
} PageTable;

KernelAddress PageTable__new(KernelPtr kernel)
{
    PageTablePtr table = (PageTablePtr) malloc(sizeof(PageTable));
    table->mapped_addresses = EmptyArray(MapRequestArray);
    return (KernelAddress) {
        .virtual_addr = addr(table),
        .phys_addr = 0,
        .valid = true
    };
}

void PageTable__map(PageTablePtr self, KernelPtr kernel, UserAddress address, size_t size)
{
    MapRequestArray__push(&self->mapped_addresses, (MapRequest){.address = address, .size = size});
}

void PageTable__map_mmio(PageTablePtr self, KernelPtr kernel, UserAddress address, size_t size)
{
    PageTable__map(self, kernel, address, size);
}

void PageTable__unmap(PageTablePtr self, KernelPtr kernel, Address virtual_address, size_t size)
{
    FAIterate(MapRequestArray, self->mapped_addresses, i, mapped_address)
        if (MapRequest__is_covered_by_unmap(&mapped_address, virtual_address, size))
        {
            MapRequestArray__remove(&self->mapped_addresses, mapped_address);
            return;
        }
    End
}

bool check_address_mapped(PageTable *page_table, Address virtual_address, bool read, bool write, bool execute, size_t size)
{
    FAIterate(MapRequestArray, page_table->mapped_addresses, i, mapped_address)
        UserAddress mapped = mapped_address.address;
        __d_printf(PROGRESS,
            "Checking %i mapped address calls vaddr=%p mapped=%p size=%i expected_size=%i rwx=%i%i%i expected rwx=%i%i%i\n",
            i, virtual_address, mapped.virtual_addr, mapped_address.size, size,
            mapped_address.address.read, mapped_address.address.write, mapped_address.address.execute,
            read, write, execute);
        bool match = mapped.virtual_addr == virtual_address;
        match &= mapped.read == read;
        match &= mapped.write == write;
        match &= mapped.execute == execute;
        match &= mapped_address.size == size;
        if (match) {
            return true;
        }
    End

    return false;
}
