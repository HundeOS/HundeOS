#ifndef __AARCH64_ASM_HELPERS_H
#define __AARCH64_ASM_HELPERS_H
#include <stdint.h>

inline void write_sctlr_el1(uint64_t v){ __asm__ volatile("msr sctlr_el1, %0" :: "r"(v)); }
inline uint64_t read_sctlr_el1(void){ uint64_t v; __asm__ volatile("mrs %0, sctlr_el1":"=r"(v)); return v; }

inline uint64_t read_tcr_el1(){ uint64_t v; __asm__ volatile("mrs %0, tcr_el1" : "=r"(v)); return v; }
inline void write_tcr_el1(uint64_t v){ __asm__ volatile("msr tcr_el1, %0" :: "r"(v)); }

inline void write_ttbr0_el1(uint64_t v){ __asm__ volatile("msr ttbr0_el1, %0" :: "r"(v)); }
inline void write_ttbr1_el1(uint64_t v){ __asm__ volatile("msr ttbr1_el1, %0" :: "r"(v)); }

inline uint64_t read_ttbr0_el1(void){ uint64_t v; __asm__ volatile("mrs %0, ttbr0_el1":"=r"(v)); return v; }
inline uint64_t read_ttbr1_el1(void){ uint64_t v; __asm__ volatile("mrs %0, ttbr1_el1":"=r"(v)); return v; }

inline uint64_t read_mair_el1(){ uint64_t v; __asm__ volatile("mrs %0, mair_el1" :"=r"(v)); return v; }
inline void write_mair_el1(uint64_t v){ __asm__ volatile("msr mair_el1, %0" :: "r"(v)); }

inline void write_elr_el1(uint64_t v){ __asm__ volatile("msr elr_el1, %0" :: "r"(v)); }

inline void write_sp_el0(uint64_t v) { __asm__ volatile("msr sp_el0, %0" :: "r"(v)); }
inline void write_spsr_el1(uint64_t v) { __asm__ volatile("msr spsr_el1, %0" :: "r"(v)); }
inline void write_spsr_el0(uint64_t v) { __asm__ volatile("msr spsr_el0, %0" :: "r"(v)); }


inline void eret(void){ __asm__ volatile("eret"); }

inline void isb(void){ __asm__ volatile("isb"); }
inline void dsb_ish(void){ __asm__ volatile("dsb ish"); }
inline void dsb_ishst(void){ __asm__ volatile("dsb ishst" ::: "memory"); }
inline void dmb_ish(void){ __asm__ __volatile__("dmb ish" ::: "memory"); }

inline void tlbi_all(void){ __asm__ volatile("tlbi vmalle1"); }
inline uint64_t read_tpidr_el1(){ uint64_t v; __asm__ __volatile__("mrs %0, tpidr_el1" : "=r"(v)); return v; }
inline void write_tpidr_el1(uint64_t v){ __asm__ __volatile__("msr tpidr_el1, %0" :: "r"(v) : "memory"); }

inline void writel(uint32_t v, volatile void *a){ *(volatile uint32_t*)a = v; }
inline uint32_t readl(volatile void *a){ return *(volatile uint32_t*)a; }

inline uint64_t rd_mpidr(void){ uint64_t x; __asm__ volatile("mrs %0, mpidr_el1":"=r"(x)); return x; }
inline uint64_t rd_cntfrq(void){ uint64_t x; __asm__ volatile("mrs %0, cntfrq_el0":"=r"(x)); return x; }

inline void wr_cntv_tval(uint64_t v){ __asm__ volatile("msr cntv_tval_el0, %0" :: "r"(v)); }
inline void wr_cntv_ctl(uint64_t v){  __asm__ volatile("msr cntv_ctl_el0,  %0" :: "r"(v)); }
inline void wr_cntp_tval(uint64_t v){ __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(v)); }
inline void wr_cntp_ctl(uint64_t v){  __asm__ volatile("msr cntp_ctl_el0,  %0" :: "r"(v)); }

inline void wr_icc_pmr(uint64_t v){   __asm__ volatile("msr ICC_PMR_EL1, %0" :: "r"(v)); }
inline void wr_icc_igrpen1(uint64_t v){__asm__ volatile("msr ICC_IGRPEN1_EL1, %0" :: "r"(v)); }
inline void wr_icc_sre(uint64_t v){   __asm__ volatile("msr ICC_SRE_EL1, %0" :: "r"(v)); __asm__ volatile("isb"); }
inline void enable_irq(void){         __asm__ volatile("msr daifclr, #2"); }  // clear I-bit
inline uint64_t read_daifclr(){ uint64_t v; __asm__ __volatile__("mrs %0, daif" : "=r"(v)); return v; }
inline uint64_t read_icc_igrpen1(){ uint64_t v; __asm__ volatile("mrs %0, ICC_IGRPEN1_EL1" : "=r"(v)); return v; }

inline uint64_t read_current_el(){ uint64_t v; __asm__ volatile("mrs %0, CurrentEL" : "=r"(v)); return v; }
inline uint64_t read_spsel(){ uint64_t v; __asm__ volatile("mrs %0, SPSel" : "=r"(v)); return v; }
inline uint64_t read_hcr_el2(){ uint64_t v; __asm__ volatile("mrs %0, hcr_el2" : "=r"(v)); return v; }

inline uint32_t icc_iar1(void){ uint64_t x; __asm__ volatile("mrs %0, ICC_IAR1_EL1":"=r"(x)); return (uint32_t)x; }
inline void icc_eoir1(uint32_t id){ __asm__ volatile("msr ICC_EOIR1_EL1, %0" :: "r"((uint64_t)id)); }
inline void icc_dir(uint32_t id){   __asm__ volatile("msr ICC_DIR_EL1,   %0" :: "r"((uint64_t)id)); }

inline uint64_t read_esr_el1(void) {
    uint64_t x;
    __asm__ __volatile__("mrs %0, esr_el1" : "=r"(x));
    return x;
}

inline uint64_t read_elr_el1(void) {
    uint64_t x;
    __asm__ __volatile__("mrs %0, elr_el1" : "=r"(x));
    return x;
}

inline uint64_t read_far_el1(void) {
    uint64_t x;
    __asm__ __volatile__("mrs %0, far_el1" : "=r"(x));
    return x;
}

inline uint64_t read_spsr_el1(void) {
    uint64_t x;
    __asm__ __volatile__("mrs %0, spsr_el1" : "=r"(x));
    return x;
}


#endif