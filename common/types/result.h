#ifndef __COMMON_TYPES_RESULT_H
#define __COMMON_TYPES_RESULT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bstring.h"

typedef int64_t Code;

#define Result(T) \
    Result_##T \

#define ResultDef(T) \
    typedef struct Success_##T { \
        T val; \
    } Success_##T; \
    \
    typedef struct Error_##T { \
        Code code; \
        BString error; \
    } Error_##T; \
    \
    typedef struct Result_##T { \
        bool ok; \
        union { \
          Success_##T success;\
          Error_##T error;\
        };\
    } Result_##T;\
    \
    typedef T Result_##T##_type; \



#define Success(T, value) \
    ( Result_##T ) { \
        .ok = true, \
        .success = (Success_##T) { \
            .val = value, \
        } \
    } \

#define Error(T, error_message, error_code) \
    ( Result_##T ) { \
        .ok = false, \
        .error = (Error_##T) { \
            .code = error_code, \
            .error = error_message, \
        } \
    } \


#define End \
} \

#define success(val_name) \
    { \
        __ResultValueType val_name = __result.success.val;

#define error(code_name, message_name) \
    { \
        BString message_name = __result.error.error; \
        Code code_name = __result.error.code;

#define success_do_nothing() success(_) {}

#define match_result(T, res, success_branch, error_branch) \
{ \
    typedef Result_##T##_type __ResultValueType __attribute__((unused)); \
    Result_##T __result = res; \
    if (__result.ok) \
        success_branch \
    } \
    else \
        error_branch \
    } \
} \

#define result_unwrap(T, res, alternate_val) \
    ((res).ok ? (res).success.val : (alternate_val))\

#endif
