#ifndef __KERNEL_TYPES_OPTIONAL_H
#define __KERNEL_TYPES_OPTIONAL_H

#define _Optional(alias) \
    Optional__##alias
#define _Some(alias) \
    Some__##alias
#define _None(alias) \
    None__##alias
#define _Type(alias) \
    alias##__type

#define OptionalDef(T, alias) \
    typedef struct _None(T) { \
        bool present; \
    } _None(T); \
    typedef _None(T) _None(alias);\
    \
    typedef struct _Some(T) { \
        bool present; \
        T val; \
    } _Some(T); \
    typedef _Some(T) _Some(alias);\
    \
    typedef union _Optional(T) { \
        _Some(T) some; \
        _None(T) none;\
    } _Optional(T); \
    typedef _Optional(T) alias; \
    typedef _Optional(T) _Optional(alias); \
    typedef T _Type(alias); \


#define Some(alias, value) \
    (_Optional(alias)) { \
        .some = (_Some(alias)) { \
            .present = true, \
            .val = value, \
        } \
    }; \

#define None(alias) \
    (_Optional(alias)) { \
        .none = (_None(alias)) { \
            .present = false, \
        } \
    }; \

#define Match(T, opt) \
{ \
    _Optional(T) optional = opt; \
    typedef T value_type; \
    __d_println(FOCUS, "Match"); \

#define End \
} \

#define IsSome(val_name, block) \
    if (optional.some.present) \
    { \
        __d_println(FOCUS, "Got something"); \
        value_type val_name = optional.some.val; \
        block; \
    } \

#define IsNone(block) \
    if (!optional.none.present) \
    { \
        __d_println(FOCUS, "Got nothing"); \
        block; \
    } \

#endif