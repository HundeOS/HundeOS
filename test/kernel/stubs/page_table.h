#ifndef __KERNEL_STUBS_PAGE_TABLE_H
#define __KERNEL_STUBS_PAGE_TABLE_H

bool check_address_mapped(PageTablePtr page_table, Address virtual_address, bool read, bool write, bool execute,
                          size_t size);
void PageTableStub__fail_mapping_after(int successful_maps);
void PageTableStub__reset_publish_tracking(void);
size_t PageTableStub__unpublished_map_count(void);
size_t PageTableStub__publish_count(void);

#endif
