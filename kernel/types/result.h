#ifndef __KERNEL_TYPES_RESULT_H
#define __KERNEL_TYPES_RESULT_H
#include <stdint.h>
#include "kstring.h"

typedef int64_t Code;

#define Result(T) \
    Result_##T \

#define ResultDef(T) \
    typedef struct Success_##T { \
        bool success; \
        T val; \
    } Success_##T; \
    \
    typedef struct Error_##T { \
        bool success; \
        Code code; \
        KString error; \
    } Error_##T; \
    \
    typedef union Result_##T { \
        Success_##T success;\
        Error_##T error;\
    } Result_##T;\
    \
    typedef T Result_##T##_type;

#define Success(T, value) \
    ( Result_##T ) { \
        .success = (Success_##T) { \
            .success = true, \
            .val = value, \
        } \
    } \

#define Error(T, error_message, error_code) \
    ( Result_##T ) { \
        .error = (Error_##T) { \
            .success = false, \
            .code = error_code, \
            .error = error_message, \
        } \
    } \


#define ResultMatch(T, res) \
{ \
    typedef Result_##T##_type __ResultValueType;\
    Result_##T ___result = res; \

#define End \
} \

#define IsSuccess(val_name, block) \
    if (___result.error.success) \
    { \
        __ResultValueType val_name = ___result.success.val; \
        block; \
    } \

#define IsError(code_name, message_name, block) \
    if (!___result.error.success) \
    { \
        KString message_name = ___result.error.error; \
        Code code = ___result.error.code; \
        block; \
    } \


#define ___res_panic(message) \
    Kernel__panic(message, NULL);


#define ResultUnwrap(T, result, val_name, block) \
    { \
        T val_name; \
        Result(T) __res = result; \
        if(__res.error.success) \
        { \
            val_name = __res.success.val; \
        } \
        else \
        { \
            ___res_panic("Error, no value to unwrap"); \
        } \
        block; \
    }


#endif