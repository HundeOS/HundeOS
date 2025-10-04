#ifndef __AARCH64_BOOT_MUTEX_H_
#define __AARCH64_BOOT_MUTEX_H_

typedef struct boot_mutex {
    uint32_t lock;
} boot_mutex;

inline void lock(boot_mutex *m){
    while (__atomic_exchange_n(&m->lock, 1u, __ATOMIC_ACQUIRE) != 0u) {
        __asm__ volatile("wfe");
    }
}

inline void unlock(boot_mutex *m){
    __atomic_store_n(&m->lock, 0u, __ATOMIC_RELEASE);
}

#endif