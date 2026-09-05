#ifndef __COMMON_TYPES_FIND_RESULT_H
#define __COMMON_TYPES_FIND_RESULT_H

#include <stdbool.h>

#include "optional.h"

#define match_find_result(alias_name, opt, some_branch, none_branch) \
{ \
    typedef alias_name##__type value_type; \
    _Optional(alias_name##__FindResult) __find_optional = opt; \
    struct { struct { value_type val; } some; } __optional; \
    if (__find_optional.present && ((__optional.some.val = *__find_optional.some.val.value), true)) \
        some_branch \
    } \
    else \
        none_branch \
    } \
}

#define match_find_result_ptr(alias_name, opt, some_branch, none_branch) \
{ \
    typedef alias_name##__type * value_type; \
    _Optional(alias_name##__FindResult) __find_optional = opt; \
    struct { struct { value_type val; } some; } __optional; \
    if (__find_optional.present && ((__optional.some.val = __find_optional.some.val.value), true)) \
        some_branch \
    } \
    else \
        none_branch \
    } \
}

#endif
