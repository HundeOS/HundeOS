#ifndef __KERNEL_TYPES_LINKED_LIST_H
#define __KERNEL_TYPES_LINKED_LIST_H

#include "../debug.h"
#include "../kmalloc.h"
#include "common.h"
#include "comparison.h"

#define LINKED_LIST_ALLOC(size) ((void *)Kernel__malloc(size).virtual_addr)
#define LINKED_LIST_FREE(ptr)                                                                                          \
    {                                                                                                                  \
        Result(KAllocCode) free_result = Kernel__free_malloc(addr(ptr));                                               \
        match_result(                                                                                                  \
            free_result, success_do_nothing(), error(code, message) {                                                  \
                __d_printf(SYSLOG, "linked list free failed source=%s ptr=%p addr=%p code=%i message=%s\n", __func__,  \
                           ptr, addr(ptr), code, BString__cstr(&message));                                             \
            });                                                                                                        \
    }

#include "../../common/types/linked_list.h"

#endif
