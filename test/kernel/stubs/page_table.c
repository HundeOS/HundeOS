#include <stdlib.h>
#include "../../../kernel/address.h"
#include "../../../kernel/arch/page_table.h"
#include "../../../kernel/types/optional.h"
#include "../../../kernel/types/comparison.h"
#include "../../../kernel/types/fixed_array.h"

#include "../../../kernel/debug.h"

LinkedListBodyDefine(ProcessPageTablePages);

bool ProcessPageTablePage__equals(KernelAddressPtr self, KernelAddressPtr other)
{
    return self->valid == other->valid &&
        self->virtual_addr == other->virtual_addr &&
        self->phys_addr == other->phys_addr;
}

typedef struct MapRequest {
    UserAddress address;
    size_t size;
} MapRequest;

typedef MapRequest * MapRequestPtr;

bool MapRequest__equals(MapRequestPtr self, MapRequestPtr other)
{
    return self->address.virtual_addr == other->address.virtual_addr && self->size == other->size;
}

static MapRequest MapRequest__new(UserAddress address, size_t size)
{
    return (MapRequest) {
        .address = address,
        .size = size,
    };
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

static Result(KAllocCode) ProcessPageTable__record_page(ProcessPageTablePtr self, KernelAddress page)
{
    match_result(CollectionCode, ProcessPageTablePages__push(&self->pages, page),
        success(_) { return Success(KAllocCode, KALLOC_OK); },
        error(code, message) { return Error(KAllocCode, message, code); }
    );

    return Error(KAllocCode, bstr("Unable to record page table page"), KALLOC_ALLOCATION_FAILED);
}

Result(ProcessPageTable) ProcessPageTable__new(KernelPtr kernel)
{
    PageTablePtr table = v_ptr(Kernel__alloc(sizeof(PageTable)), PageTable);
    table->mapped_addresses = EmptyArray(MapRequestArray);

    ProcessPageTable process_page_table = (ProcessPageTable) {
        .root = KERNEL_ADDR(addr(table), addr(table)),
        .pages = ProcessPageTablePages__new(),
    };

    match_result(KAllocCode, ProcessPageTable__record_page(&process_page_table, process_page_table.root),
        success_do_nothing(),
        error(code, message) { return Error(ProcessPageTable, message, code); }
    );

    return Success(ProcessPageTable, process_page_table);
}

KernelAddress ProcessPageTable__root(ProcessPageTablePtr self)
{
    return self->root;
}

PageTablePtr ProcessPageTable__root_table(ProcessPageTablePtr self)
{
    return v_ptr(ProcessPageTable__root(self), PageTable);
}

size_t ProcessPageTable__page_count(ProcessPageTablePtr self)
{
    return ProcessPageTablePages__len(&self->pages);
}

Result(KAllocCode) ProcessPageTable__dispose(ProcessPageTablePtr self)
{
    for_each_ll(ProcessPageTablePages, self->pages, page,
    {
        match_result(KAllocCode, Kernel__free(page.virtual_addr),
            success_do_nothing(),
            error(code, message) { return Error(KAllocCode, message, code); }
        );
    });

    match_result(CollectionCode, ProcessPageTablePages__dispose(&self->pages),
        success_do_nothing(),
        error(code, message) { return Error(KAllocCode, message, code); }
    );

    self->root = NULL_KERNEL_ADDRESS;
    return Success(KAllocCode, KALLOC_OK);
}

void PageTable__map(ProcessPageTablePtr self, KernelPtr kernel, UserAddress address, size_t size)
{
    MapRequestArray__push(&ProcessPageTable__root_table(self)->mapped_addresses, MapRequest__new(address, size));
}

void PageTable__map_mmio(ProcessPageTablePtr self, KernelPtr kernel, UserAddress address, size_t size)
{
    PageTable__map(self, kernel, address, size);
}

void PageTable__unmap(ProcessPageTablePtr self, KernelPtr kernel, Address virtual_address, size_t size)
{
    PageTablePtr root = ProcessPageTable__root_table(self);
    for_each_fa(MapRequestArray, root->mapped_addresses, i, mapped_address,
    {
        if (MapRequest__is_covered_by_unmap(&mapped_address, virtual_address, size))
        {
            MapRequestArray__remove(&root->mapped_addresses, mapped_address);
            return;
        }
    });
}

bool check_address_mapped(PageTable *page_table, Address virtual_address, bool read, bool write, bool execute, size_t size)
{
    for_each_fa(MapRequestArray, page_table->mapped_addresses, i, mapped_address,
    {
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
    });

    return false;
}
