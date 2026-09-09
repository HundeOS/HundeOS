#ifndef __COMMON_TYPES_FIND_RESULT_H
#define __COMMON_TYPES_FIND_RESULT_H

#include <stdbool.h>

#include "optional.h"

#define match_find_result(opt, some_branch, none_branch)                                                               \
    {                                                                                                                  \
        __auto_type __find_optional = (opt);                                                                           \
        typedef __typeof__(*__find_optional.some.val.value) value_type;                                                \
        struct                                                                                                         \
        {                                                                                                              \
            struct                                                                                                     \
            {                                                                                                          \
                value_type val;                                                                                        \
            } some;                                                                                                    \
        } __optional;                                                                                                  \
        if (__find_optional.present && ((__optional.some.val = *__find_optional.some.val.value), true))                \
            some_branch                                                                                                \
    }                                                                                                                  \
    else none_branch                                                                                                   \
    }                                                                                                                  \
    }

#define match_find_result_ptr(opt, some_branch, none_branch)                                                           \
    {                                                                                                                  \
        __auto_type __find_optional = (opt);                                                                           \
        typedef __typeof__(__find_optional.some.val.value) value_type;                                                 \
        struct                                                                                                         \
        {                                                                                                              \
            struct                                                                                                     \
            {                                                                                                          \
                value_type val;                                                                                        \
            } some;                                                                                                    \
        } __optional;                                                                                                  \
        if (__find_optional.present && ((__optional.some.val = __find_optional.some.val.value), true))                 \
            some_branch                                                                                                \
    }                                                                                                                  \
    else none_branch                                                                                                   \
    }                                                                                                                  \
    }

#endif
