#ifndef __KERNEL_TYPES_RESULT_H
#define __KERNEL_TYPES_RESULT_H
#include <stdint.h>

#include "kstring.h"

typedef uint64_t Code;

#define ResultDef(T, alias) \
    typedef struct Success_##T { \
        bool success; \
        T val; \
    } Success_##T; \
    typedef Success_##T Success_##alias;\
    \
    typedef struct Error_##T { \
        bool success; \
        Code code; \
        KString error; \
    } Error_##T; \
    typedef Error_##T Error_##alias;\
    \
    typedef union Result_##T { \
        Success_##T success;\
        Error_##T error;\
    } Result_##T;\
    \
    typedef Result_##T alias;\
    typedef Result_##T Result_##alias;\
    typedef T Result_##alias##_type;

#define Success(alias, value) \
    ( Result_##alias ) { \
        .success = (Success_##alias) { \
            .success = true, \
            .val = value, \
        } \
    } \

#define Error(alias, error_message, error_code) \
    ( Result_##alias ) { \
        .error = (Error_##alias) { \
            .success = false, \
            .code = error_code, \
            .error = kstring(error_message), \
        } \
    } \


#define ResultMatch(alias, res) \
{ \
    Result_##alias result = res; \

#define End \
} \

#define IsSuccess(val_name, block) \
    if (result.error.success) \
    { \
        Result_##alias##_type val_name = optional.val; \
        block; \
    } \

#define IsError(code_name, message_name, block) \
    if (!result.error.success) \
    { \
        KString message_name = result.error.error; \
        Code code = result.error.code; \
        block; \
    } \


#endif