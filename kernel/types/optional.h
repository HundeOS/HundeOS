#ifndef __KERNEL_TYPES_OPTIONAL_H
#define __KERNEL_TYPES_OPTIONAL_H
#include "../debug.h"

typedef struct ProcessContext ProcessContext;
void Kernel__panic(const char * message, ProcessContext * context);

#define _Optional(T) \
    Optional__##T \

#define _Some(T) \
    Some__##T \

#define _None(T) \
    None__##T \

#define _Type(T) \
    opt__##T##__type \

#define Optional(T) \
    _Optional(T) \

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
    } _None(T); \
    \
    typedef struct _Some(T) { \
        T val; \
    } _Some(T); \
    \
    typedef struct _Optional(T) { \
        bool present; \
        union { \
          _Some(T) some; \
          _None(T) none;\
        }; \
    } _Optional(T); \
    typedef T _Type(T); \
    \
    static T ___opt##T##_ZERO;\
    static inline T ___opt_##T##_panic(const char * message) \
    { \
        Kernel__panic(message, NULL); \
        return ___opt##T##_ZERO; \
    } \

#define OptionalType(T, alias) \
    typedef T alias; \
    OptionalDef(alias); \


#define Some(T, value) \
    (_Optional(T)) { \
        .present = true,\
        .some = (_Some(T)) { \
            .val = value, \
        } \
    } \

#define None(T) \
    (_Optional(T)) { \
        .present = false, \
        .none = (_None(T)) { \
        } \
    } \

#define Match(T, opt) \
{ \
    typedef opt__##T##__type value_type; \
    _Optional(T) optional = opt; \

#define End \
} \

#define is_something(optional) \
    optional.present

#define is_nothing(optional) \
    !optional.present

#define IsSome(val_name, block) \
    if (optional.present) \
    { \
        value_type val_name = optional.some.val; \
        block; \
    } \

#define IsNone(block) \
    if (!optional.present) \
    { \
        block; \
    } \



#define UnwrapPanic(T, opt) \
    opt.present ? opt.some.val : ___opt_##T##_panic("No optional value to unwrap\n")\


#define Unwrap(T, opt, alternate_val) \
    opt.present ? opt.some.val : alternate_val;\

#endif