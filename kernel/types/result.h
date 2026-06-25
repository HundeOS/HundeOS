#ifndef __KERNEL_TYPES_RESULT_H
#define __KERNEL_TYPES_RESULT_H
#include <stddef.h>
#include <stdint.h>
#include "kstring.h"

typedef struct ProcessContext ProcessContext;
void Kernel__panic(const char * message, ProcessContext * context);

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
        KString error; \
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
    static T ___res##T##_ZERO;\
    typedef T Result_##T##_type; \
    static inline T ___res##T##_panic(const char * message) \
    { \
        Kernel__panic(message, NULL); \
        return ___res##T##_ZERO; \
    } \



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
        (void) val_name; \
        block; \
    } \

#define IsError(code_name, message_name, block) \
    if (!___result.ok) \
    { \
        KString message_name = ___result.error.error; \
        Code code = ___result.error.code; \
        (void) message_name; \
        (void) code_name; \
        block; \
    } \

#define ResultUnwrapPanic(T, res) \
    res.ok ? res.success.val : ___res##T##_panic("No result value to unwrap\n")\

#define ResultUnwrap(T, opt, alternate_val) \
    res.ok ? res.success.val : alternate_val\

#endif
