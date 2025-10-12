#include <stdlib.h>
#include "../../../kernel/address.h"
#include "../../../kernel/page_alloc.h"
#include "../../../kernel/arch/page_table.h"

#include "../../../kernel/debug.h"

typedef struct MapRequest {
    UserAddress address;
    size_t size;
} MapRequest;

FixedArrayDefine(MapRequest, 1024, MapRequestArray);
FixedArrayBodyDefine(MapRequestArray);

typedef struct PageTable
{
    MapRequestArray mapped_addresses;
} PageTable;

KernelAddress PageTable__new()
{
    PageTable * table = (PageTable *) malloc(sizeof(PageTable));
    table->mapped_addresses = (MapRequestArray) {
        .len = 0,
        .max = 1024
    };
    return (KernelAddress) {
        .virtual_addr = addr(table),
        .phys_addr = 0,
        .valid = true
    };
}

void PageTable__map(PageTable * self, UserAddress address, size_t size)
{
    MapRequestArray__push(&self->mapped_addresses, (MapRequest){.address = address, .size = size});
}

bool check_address_mapped(PageTable *page_table, Address virtual_address, bool read, bool write, bool execute, size_t size)
{
    __d_printf(FOCUS, "Checking %i mapped address calls vaddr=%p \n", page_table->mapped_addresses.len, virtual_address);
    for (int i = 0; i < page_table->mapped_addresses.len; i++)
    {
        MapRequest mapped_address = MapRequestArray__at(&page_table->mapped_addresses, i);
        UserAddress mapped = mapped_address.address;
        __d_printf(FOCUS,
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
    }
    return false;
}