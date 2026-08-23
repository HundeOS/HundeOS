#ifndef __COMMON_TYPES_OPTIONAL_H
#define __COMMON_TYPES_OPTIONAL_H

#include <stdbool.h>

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

#define End \
} \

#define is_something(optional) \
    optional.present

#define is_nothing(optional) \
    !optional.present

#define some(val_name) \
    { \
        value_type val_name = __optional.some.val;

#define none() \
    {

#define match_optional(T, opt, some_branch, none_branch) \
{ \
    typedef opt__##T##__type value_type; \
    _Optional(T) __optional = opt; \
    if (__optional.present) \
        some_branch \
    } \
    else \
        none_branch \
    } \
} \

#define Unwrap(T, opt, alternate_val) \
    ((opt).present ? (opt).some.val : (alternate_val))\

#endif
