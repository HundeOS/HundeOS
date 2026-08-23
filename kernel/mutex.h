#ifndef __KERNEL_MUTEX_H
#define  __KERNEL_MUTEX_H
#include <stdatomic.h>
#include <stdbool.h>
#include "debug.h"

typedef struct {
    volatile uint32_t v;
} mutex_t;

#define MUTEX_INIT (mutex_t){ .v = 0 }

int mutex_trylock(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);

#define cs_return\
    mutex_unlock(__mutex); \
    __d_printf(MUTEX, "Returning from critical_section %s:%i\n", __FILE__, __LINE__);\
    return \

#define cs_exit_then(code)\
    __d_printf(MUTEX, "Early exit from critical_section %s:%i\n", __FILE__, __LINE__);\
    mutex_unlock(__mutex); \
    code; \

#define critical_section(m, block) \
    { \
        __d_printf(MUTEX, "Entering critical_section (%s) %s:%i\n", #m, __FILE__, __LINE__);\
        mutex_t * __mutex = &m;\
        mutex_lock(__mutex); \
        __d_printf(MUTEX, "Obtained lock (%s) %s:%i \n", #m, __FILE__, __LINE__);\
        block; \
        \
        mutex_unlock(__mutex); \
        __d_printf(MUTEX, "Leaving critical_section (%s) %s:%i\n", #m, __FILE__, __LINE__);\
    }

#define critical_section_try(m, block, failed_block) \
    { \
        __d_printf(MUTEX, "Trying critical_section (%s) %s:%i\n", #m, __FILE__, __LINE__);\
        mutex_t * __mutex = &m;\
        if (mutex_trylock(__mutex)) \
        { \
            __d_printf(MUTEX, "Obtained lock (%s) %s:%i \n", #m, __FILE__, __LINE__);\
            block; \
            mutex_unlock(__mutex); \
            __d_printf(MUTEX, "Leaving critical_section (%s) %s:%i\n", #m, __FILE__, __LINE__);\
        } \
        else \
        { \
            failed_block; \
        } \
    }

#endif
