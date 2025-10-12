#ifndef __KERNEL_STUBS_PAGE_TABLE_H
#define __KERNEL_STUBS_PAGE_TABLE_H

bool check_address_mapped(
    PageTable * page_table,
    Address virtual_address,
    bool read,
    bool write,
    bool execute,
    size_t size);


#endif