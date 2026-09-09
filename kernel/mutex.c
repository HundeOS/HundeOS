#include "mutex.h"

int mutex_trylock(mutex_t * m)
{
    return __atomic_exchange_n(&m->v, 1u, __ATOMIC_ACQUIRE) == 0u;
}

void mutex_lock(mutex_t * m)
{
    while (__atomic_exchange_n(&m->v, 1u, __ATOMIC_ACQUIRE) != 0u)
        CPU__yield();
}

void mutex_unlock(mutex_t * m)
{
    __atomic_store_n(&m->v, 0u, __ATOMIC_RELEASE);
}
