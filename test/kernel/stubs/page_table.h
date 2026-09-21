#ifndef __KERNEL_STUBS_PAGE_TABLE_H
#define __KERNEL_STUBS_PAGE_TABLE_H

bool check_address_mapped(PageTablePtr page_table, Address virtual_address, bool read, bool write, bool execute,
                          size_t size);
void PageTableStub__fail_mapping_after(int successful_maps);

#endif
