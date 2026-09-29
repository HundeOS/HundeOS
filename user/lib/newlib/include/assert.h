#ifndef __USER_LIB_NEWLIB_ASSERT_H
#define __USER_LIB_NEWLIB_ASSERT_H

#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            __builtin_trap();                                                                                          \
        }                                                                                                              \
    } while (0)
#endif

#endif
