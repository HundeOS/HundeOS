#ifndef __KERNEL_TYPES_LINKED_LIST_H
#define __KERNEL_TYPES_LINKED_LIST_H

#include "../kalloc.h"
#include "../panic.h"
#include "common.h"
#include "comparison.h"

#define LINKED_LIST_ALLOC(size) ((void *) Kernel__alloc(size).virtual_addr)
#define LINKED_LIST_FREE(ptr) \
    { \
        Result(KAllocCode) free_result = Kernel__free(addr(ptr)); \
        match_result(KAllocCode, free_result, \
            success_do_nothing(), \
            error(code, message) { Kernel__panic(BString__cstr(&message), NULL); } /* hundelint: ignore-no-null */ \
        ); \
    }

#include "../../common/types/linked_list.h"

#endif
