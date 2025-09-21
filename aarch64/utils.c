#include <stdint.h>

static inline uint64_t get_current_el(void) {
    uint64_t el;
    asm volatile("mrs %0, CurrentEL" : "=r" (el));
    return (el >> 2) & 3;
}