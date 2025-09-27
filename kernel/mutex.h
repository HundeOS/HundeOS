#ifndef __KERNEL_MUTEX_H
#define  __KERNEL_MUTEX_H
#include <stdatomic.h>
#include <stdbool.h>
#include "noop.h"

typedef struct {
    volatile uint32_t v;
} mutex_t;

#define MUTEX_INIT (mutex_t){ .v = 0 }

static inline int mutex_trylock(mutex_t *m){
    return __atomic_exchange_n(&m->v, 1u, __ATOMIC_ACQUIRE) == 0u;
}
static inline void mutex_lock(mutex_t *m){
    while (__atomic_exchange_n(&m->v, 1u, __ATOMIC_ACQUIRE) != 0u)
        cpu_yield();
}
static inline void mutex_unlock(mutex_t *m){
    __atomic_store_n(&m->v, 0u, __ATOMIC_RELEASE);
}

#endif
