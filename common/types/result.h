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


#define ResultMatch(T, res) \
{ \
    typedef Result_##T##_type __ResultValueType __attribute__((unused));\
    Result_##T ___result = res; \

#define End \
} \

#define IsSuccess(val_name, block) \
    if (___result.ok) \
    { \
        __ResultValueType val_name = ___result.success.val; \
        block; \
    } \

#define IsError(code_name, message_name, block) \
    if (!___result.ok) \
    { \
        BString message_name = ___result.error.error; \
        Code code_name = ___result.error.code; \
        block; \
    } \

#define ResultUnwrap(T, res, alternate_val) \
    res.ok ? res.success.val : alternate_val\

#endif
