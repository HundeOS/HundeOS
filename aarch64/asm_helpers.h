#ifndef __AARCH64_ASM_HELPERS_H
#define __AARCH64_ASM_HELPERS_H

inline void write_sctlr_el1(uint64_t v){ __asm__ volatile("msr sctlr_el1, %0" :: "r"(v)); }
inline void write_tcr_el1(uint64_t v){ __asm__ volatile("msr tcr_el1, %0" :: "r"(v)); }
inline void write_ttbr0_el1(uint64_t v){ __asm__ volatile("msr ttbr0_el1, %0" :: "r"(v)); }
inline void write_mair_el1(uint64_t v){ __asm__ volatile("msr mair_el1, %0" :: "r"(v)); }
inline uint64_t read_sctlr_el1(void){ uint64_t v; __asm__ volatile("mrs %0, sctlr_el1":"=r"(v)); return v; }

inline void isb(void){ __asm__ volatile("isb"); }
inline void dsb_ish(void){ __asm__ volatile("dsb ish"); }
inline void dmb_ish(void){ __asm__ __volatile__("dmb ish" ::: "memory"); }
inline void tlbi_all(void){ __asm__ volatile("tlbi vmalle1"); }

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
inline unsigned cpu_aff0(void){ return (unsigned)(rd_mpidr() & 0xff); }

#endif