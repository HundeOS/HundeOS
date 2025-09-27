// timer_gicv3.c
#include <stdint.h>

#include "../kernel/debug.h"
#include "cpu_timer.h"

#include <stdint.h>

#include "cpu.h"

#define GICD_BASE   0x08000000UL
#define GICR_BASE   0x080A0000UL   // per-CPU redistributor frames, stride 0x20000
#define GICR_STRIDE 0x20000UL

#define GICD_CTLR   0x0000


// GICR offsets we need (RD_base + SGI_base 0x10000)
#define GICR_CTLR           0x0000
#define GICR_WAKER          0x0014
#define GICR_IGROUPR0       0x10080
#define GICR_ISENABLER0     0x10100
#define GICR_ICFGR1         0x10C04      // PPIs [31:16] config (edge/level)
#define GICR_IPRIORITYR(n) (0x10400 + (n)) // byte-per-int

static inline void writel(uint32_t v, volatile void *a){ *(volatile uint32_t*)a = v; }
static inline uint32_t readl(volatile void *a){ return *(volatile uint32_t*)a; }

static inline uint64_t rd_mpidr(void){ uint64_t x; __asm__ volatile("mrs %0, mpidr_el1":"=r"(x)); return x; }
static inline uint64_t rd_cntfrq(void){ uint64_t x; __asm__ volatile("mrs %0, cntfrq_el0":"=r"(x)); return x; }
static inline void     wr_cntv_tval(uint64_t v){ __asm__ volatile("msr cntv_tval_el0, %0" :: "r"(v)); }
static inline void     wr_cntv_ctl(uint64_t v){  __asm__ volatile("msr cntv_ctl_el0,  %0" :: "r"(v)); }
static inline void     wr_cntp_tval(uint64_t v){ __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(v)); }
static inline void     wr_cntp_ctl(uint64_t v){  __asm__ volatile("msr cntp_ctl_el0,  %0" :: "r"(v)); }

static inline void     wr_icc_pmr(uint64_t v){   __asm__ volatile("msr ICC_PMR_EL1, %0" :: "r"(v)); }
static inline void     wr_icc_igrpen1(uint64_t v){__asm__ volatile("msr ICC_IGRPEN1_EL1, %0" :: "r"(v)); }
static inline void     wr_icc_sre(uint64_t v){   __asm__ volatile("msr ICC_SRE_EL1, %0" :: "r"(v)); __asm__ volatile("isb"); }
static inline void     enable_irq(void){         __asm__ volatile("msr daifclr, #2"); }  // clear I-bit
static inline unsigned cpu_aff0(void){ return (unsigned)(rd_mpidr() & 0xff); }


static inline uint32_t mmio_read32(const volatile void *addr) {
    return *(const volatile uint32_t *)addr;
}
static inline void mmio_write32(uint32_t v, volatile void *addr) {
    *(volatile uint32_t *)addr = v;
}

static inline void dmb_ish(void){ __asm__ __volatile__("dmb ish" ::: "memory"); }
static inline void dsb_ish(void){ __asm__ __volatile__("dsb ish" ::: "memory"); }
static inline void isb(void){     __asm__ __volatile__("isb" ::: "memory"); }

static inline uint32_t r32(uintptr_t a){ return *(volatile uint32_t*)a; }
static inline void     w32(uintptr_t a, uint32_t v){ *(volatile uint32_t*)a = v; }

void gicv3_dist_enable_grp1ns(void){
    // Enable ARE_NS (bit 4) and EnableGrp1NS (bit 1). Leave grp0 off.
    uint32_t ctlr = r32(GICD_BASE + GICD_CTLR);
    ctlr |= (1u<<4) /*ARE_NS*/ | (1u<<1) /*EnableGrp1NS*/;
    w32(GICD_BASE + GICD_CTLR, ctlr);
    __asm__ __volatile__("dsb ish; isb");
}

// Map: redistributor base for this CPU (QEMU virt packs by Aff0)
inline volatile uint8_t* gicr_base_this_cpu(void){
    return (volatile uint8_t*)(GICR_BASE + (uint64_t)cpu_aff0() * GICR_STRIDE);
}

/* Configure GICv3 CPU interface + enable PPI for CNTV (27) */
__attribute__((noinline))  // keeps return addresses clean for debugging
static void per_cpu_enable_timer_irq_and_gic(unsigned use_virtual_timer, uint32_t hz)
{
    __d_printf(DEBUG, "per_cpu_enable_timer_irq_and_gic: cpu=%u\n", cpu_id());
    gicv3_dist_enable_grp1ns();
    volatile uint8_t *gicr = gicr_base_this_cpu();
    __d_printf(DEBUG, "GICR base=%x cpu=%u\n", gicr, cpu_id());

    // --- Wake up this redistributor ---
    __d_printf(DEBUG, "mmio_read32 base=%x cpu=%u\n", gicr + GICR_WAKER, cpu_id());
    uint32_t waker = mmio_read32(gicr + GICR_WAKER);

    __d_printf(DEBUG, "waker=%x cpu=%u\n", waker, cpu_id());
    if (waker & (1u << 1)) {                // ProcessorSleep
        mmio_write32(waker & ~(1u << 1), gicr + GICR_WAKER);
        do {
            waker = mmio_read32(gicr + GICR_WAKER);
        } while (waker & (1u << 2));        // wait ChildrenAsleep==0
    }

    __d_printf(DEBUG, "write base=%x cpu=%u\n", gicr + GICR_IGROUPR0, cpu_id());
    // All non-secure interrupts to Group1
    mmio_write32(0xFFFFFFFFu, gicr + GICR_IGROUPR0);

    // --- Configure timer PPI as edge-triggered ---
    unsigned ppi  = use_virtual_timer ? PPI_CNTV : PPI_CNTP;
    unsigned bit1 = ((ppi - 16u) * 2u) + 1u; // ICFGR1: bit[1]=1 => edge

    __d_printf(DEBUG, "read base=%x cpu=%u\n", gicr + GICR_ICFGR1, cpu_id());
    uint32_t icfgr1 = mmio_read32(gicr + GICR_ICFGR1);
    icfgr1 |= (1u << bit1);
    __d_printf(DEBUG, "write base=%x cpu=%u\n", gicr + GICR_ICFGR1, cpu_id());
    mmio_write32(icfgr1, gicr + GICR_ICFGR1);

    // Priority (byte-per-interrupt). Lower = higher prio; 0x80 is mid.
    *(volatile uint8_t *)(gicr + GICR_IPRIORITYR(ppi)) = 0x80;

    // Enable the timer PPI
    __d_printf(DEBUG, "read base=%x cpu=%u\n", gicr + GICR_ISENABLER0, cpu_id());
    mmio_write32(1u << ppi, gicr + GICR_ISENABLER0);

    // Ensure GICR writes land before we enable the CPU interface
    __d_printf(DEBUG, "dsb_ish() cpu=%u\n", cpu_id());
    dsb_ish();
    __d_printf(DEBUG, "isb() cpu=%u\n", cpu_id());
    isb();

    // --- CPU interface: enable system registers & Group1Ns, unmask priority ---
    __d_printf(DEBUG, "wr_icc_sre(1) cpu=%u\n", cpu_id());
    wr_icc_sre(1);   // enable ICC_* system register interface
    __d_printf(DEBUG, "isb(1) cpu=%u\n", cpu_id());
    isb();
    __d_printf(DEBUG, "wr_icc_pmr(0xFF) cpu=%u\n", cpu_id());
    wr_icc_pmr(0xFF);
    __d_printf(DEBUG, "wr_icc_igrpen1(1) cpu=%u\n", cpu_id());
    wr_icc_igrpen1(1);

    // --- Arm the generic timer (per-CPU) ---
    __d_printf(DEBUG, "rd_cntfrq() cpu=%u\n", cpu_id());
    uint64_t frq  = rd_cntfrq();
    uint64_t tick = frq / (hz ? hz : 1000u); // default 1 kHz if hz==0

    if (use_virtual_timer) {
        __d_printf(DEBUG, "Using CNTV on cpu=%u\n", cpu_id());
        wr_cntv_ctl(0);               // disable while programming
        wr_cntv_tval(tick);
        wr_cntv_ctl(1);               // enable
    } else {
        __d_printf(DEBUG, "Using CNTP on cpu=%u\n", cpu_id());
        wr_cntp_ctl(0);
        wr_cntp_tval(tick);
        wr_cntp_ctl(1);
    }

    // Make sure timer registers are visible before enabling IRQs
    dsb_ish();
    isb();

    __d_printf(DEBUG, "Enabling IRQs in PSTATE on cpu=%u\n", cpu_id());
    enable_irq();

    __d_printf(DEBUG, "Timer IRQ armed on cpu=%u\n", cpu_id());
}

static inline uint64_t rd_cntv_ctl(void){ uint64_t x; __asm__ __volatile__("mrs %0, cntv_ctl_el0":"=r"(x)); return x; }

void timer_probe_pending(uint32_t hz){
    uint64_t frq; __asm__ __volatile__("mrs %0, cntfrq_el0":"=r"(frq));
    uint64_t tval = frq / (hz ? hz : 1000);

    __asm__ __volatile__("msr cntv_ctl_el0, %0" :: "r"(0));   // disable
    __asm__ __volatile__("msr cntv_tval_el0, %0":: "r"(tval)); // arm
    __asm__ __volatile__("msr cntv_ctl_el0, %0" :: "r"(1));   // ENABLE=1, IMASK=0

    for (int i=0;i<1000000;i++){
        if (rd_cntv_ctl() & (1u<<2)) {        // ISTATUS set?
            __d_println(DEBUG, "CNTV ISTATUS=1 (timer pending)");
            break;
        }
    }
}

void start_cpu_timer(uint32_t hz) {
    // timer_probe_pending(hz);
    per_cpu_enable_timer_irq_and_gic(1, hz);
}
