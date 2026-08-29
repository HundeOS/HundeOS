#ifndef __AARCH64_PSCI_H
#define __AARCH64_PSCI_H

#include <stdint.h>

// PSCI v0.2+ function IDs (SMC64)
#define PSCI_0_2_FN64_CPU_ON        0xC4000003ull
#define PSCI_0_2_FN64_AFFINITY_INFO 0xC4000004ull
#define PSCI_0_2_FN64_SYSTEM_OFF    0x84000008ull
#define PSCI_0_2_FN64_SYSTEM_RESET  0x84000009ull

// PSCI return codes
#define PSCI_RET_SUCCESS     0
#define PSCI_RET_ALREADY_ON -3
#define PSCI_RET_ON_PENDING -4


// SMCCC result
typedef struct smccc_res { int64_t x0, x1, x2, x3; } SMCCCRes;

inline SMCCCRes smc_call(uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3)
{
    register uint64_t r0 __asm__("x0") = x0;
    register uint64_t r1 __asm__("x1") = x1;
    register uint64_t r2 __asm__("x2") = x2;
    register uint64_t r3 __asm__("x3") = x3;
    __asm__ volatile("smc #0"
                     : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
                     : : "x4","x5","x6","x7","memory","cc");
    return (SMCCCRes){ (int64_t) r0, (int64_t) r1, (int64_t) r2, (int64_t) r3 }; // hundelint: ignore-constructor-required
}

inline SMCCCRes smccc_hvc(uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3)
{
    register uint64_t r0 __asm__("x0") = x0;
    register uint64_t r1 __asm__("x1") = x1;
    register uint64_t r2 __asm__("x2") = x2;
    register uint64_t r3 __asm__("x3") = x3;
    __asm__ volatile("hvc #0"
        : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
        : : "x4","x5","x6","x7","memory","cc");
    return (SMCCCRes){ (int64_t) r0, (int64_t) r1, (int64_t) r2, (int64_t) r3 }; // hundelint: ignore-constructor-required
}

#endif
