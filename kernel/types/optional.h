#ifndef __KERNEL_TYPES_OPTIONAL_H
#define __KERNEL_TYPES_OPTIONAL_H
#include "../debug.h"

typedef struct ProcessContext ProcessContext;
void Kernel__panic(const char * message, ProcessContext * context);

#define _Optional(alias) \
    Optional__##alias \

#define _Some(alias) \
    Some__##alias \

#define _None(alias) \
    None__##alias \

#define _Type(alias) \
    alias##__type \

#define Optional(alias) \
    _Optional(alias) \

#define AliasOptional(T, alias) \
    typedef _Optional(T) _Optional(alias); \
    typedef _Some(T) _Some(alias); \
    typedef _None(T) _None(alias); \
    typedef T _Type(alias); \

#define OptionalPreDefine(T) \
    typedef struct _None(T) _None(T); \
    typedef struct _Some(T) _Some(T); \
    typedef union _Optional(T) _Optional(T); \
    typedef T _Type(T);

#define OptionalDef(T) \
    typedef struct _None(T) { \
        bool present; \
    } _None(T); \
    \
    typedef struct _Some(T) { \
        bool present; \
        T val; \
    } _Some(T); \
    \
    typedef union _Optional(T) { \
        _Some(T) some; \
        _None(T) none;\
    } _Optional(T); \
    typedef T _Type(T); \
    \
    static T ___opt##T##_ZERO;\
    inline T ___opt_##T##_panic(const char * message) \
    { \
        Kernel__panic(message, NULL); \
        return ___opt##T##_ZERO; \
    } \

#define OptionalType(T, alias) \
    typedef T alias; \
    OptionalDef(alias); \


#define Some(alias, value) \
    (_Optional(alias)) { \
        .some = (_Some(alias)) { \
            .present = true, \
            .val = value, \
        } \
    } \

#define None(alias) \
    (_Optional(alias)) { \
        .none = (_None(alias)) { \
            .present = false, \
        } \
    } \

#define Match(alias, opt) \
{ \
    typedef alias##__type value_type; \
    _Optional(alias) optional = opt; \

#define End \
} \

#define is_something(optional) \
    optional.some.present

#define is_nothing(optional) \
    !optional.none.present

#define IsSome(val_name, block) \
    if (optional.some.present) \
    { \
        value_type val_name = optional.some.val; \
        block; \
    } \

#define IsNone(block) \
    if (!optional.none.present) \
    { \
        block; \
    } \



#define UnwrapPanic(T, opt) \
    opt.some.present ? opt.some.val : ___opt_##T##_panic("No optional value to unwrap\n")\


#define Unwrap(T, opt, alternate_val) \
    opt.some.present ? opt.some.val : alternate_val;\

#endif