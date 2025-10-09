// timer_gicv3.c
#include <stdint.h>

#include "../kernel/debug.h"
#include "cpu_timer.h"

#include "asm_helpers.h"
#include "mmio_helpers.h"
#include "gicr.h"
#include "boot/boot_debug.h"


inline unsigned cpu_aff0(void)
{
    return (unsigned)(rd_mpidr() & 0xff);
}

void CPUTimer__gicv3_dist_enable_grp1ns(void)
{
    // Enable ARE_NS (bit 4) and EnableGrp1NS (bit 1). Leave grp0 off.
    uint32_t ctlr = r32(GICD_BASE + GICD_CTLR);
    ctlr |= (1u<<4) /*ARE_NS*/ | (1u<<1) /*EnableGrp1NS*/;
    w32(GICD_BASE + GICD_CTLR, ctlr);
    dsb_ish();
    isb();
}

// Map: redistributor base for this CPU (QEMU virt packs by Aff0)
inline volatile uint8_t* CPUTimer__gicr_base_this_cpu(void)
{
    return (volatile uint8_t*)(GICR_BASE + (uint64_t)cpu_aff0() * GICR_STRIDE);
}

/* Configure GICv3 CPU interface + enable PPI for CNTV (27) */
__attribute__((noinline))  // keeps return addresses clean for debugging
static void CPUTimer__enable(bool use_virtual_timer, uint64_t hz)
{
    __d_printf(DEBUG, "per_cpu_enable_timer_irq_and_gic: cpu=%u\n", CPU__id());
    CPUTimer__gicv3_dist_enable_grp1ns();
    volatile uint8_t * gicr = CPUTimer__gicr_base_this_cpu();
    __d_printf(DEBUG, "GICR base=%p cpu=%u\n", gicr, CPU__id());

    // --- Wake up this redistributor ---
    __d_printf(DEBUG, "mmio_read32 base=%p cpu=%u\n", gicr + GICR_WAKER, CPU__id());
    uint32_t waker = mmio_read32(gicr + GICR_WAKER);

    __d_printf(DEBUG, "waker=%p cpu=%u\n", waker, CPU__id());
    if (waker & (1u << 1)) {                // ProcessorSleep
        mmio_write32(waker & ~(1u << 1), gicr + GICR_WAKER);
        do {
            waker = mmio_read32(gicr + GICR_WAKER);
        } while (waker & (1u << 2));        // wait ChildrenAsleep==0
    }

    __d_printf(DEBUG, "grp1 write base=%p cpu=%u\n", gicr + GICR_IGROUPR0, CPU__id());
    // All non-secure interrupts to Group1
    mmio_write32(0xFFFFFFFFu, gicr + GICR_IGROUPR0);

    // --- Configure timer PPI as edge-triggered ---
    unsigned ppi  = use_virtual_timer ? PPI_CNTV : PPI_CNTP;
    unsigned bit1 = ((ppi - 16u) * 2u) + 1u; // ICFGR1: bit[1]=1 => edge

    __d_printf(DEBUG, "read base=%p cpu=%u\n", gicr + GICR_ICFGR1, CPU__id());
    uint32_t icfgr1 = mmio_read32(gicr + GICR_ICFGR1);
    icfgr1 |= (1u << bit1);
    __d_printf(DEBUG, "write base=%p cpu=%u\n", gicr + GICR_ICFGR1, CPU__id());
    mmio_write32(icfgr1, gicr + GICR_ICFGR1);

    uint8_t * priority_reg = (gicr + GICR_IPRIORITYR(ppi));
    __d_printf(DEBUG, "priority write base=%p cpu=%u\n",priority_reg, CPU__id());
    // Priority (byte-per-interrupt). Lower = higher prio; 0x80 is mid.
    *(volatile uint8_t *)(priority_reg) = 0x80;

    // Enable the timer PPI
    __d_printf(DEBUG, "read base=%p cpu=%u\n", gicr + GICR_ISENABLER0, CPU__id());
    mmio_write32(1u << ppi, gicr + GICR_ISENABLER0);

    // Ensure GICR writes land before we enable the CPU interface
    __d_printf(DEBUG, "dsb_ish() cpu=%u\n", CPU__id());
    dsb_ish();
    __d_printf(DEBUG, "isb() cpu=%u\n", CPU__id());
    isb();

    // --- CPU interface: enable system registers & Group1Ns, unmask priority ---
    __d_printf(DEBUG, "wr_icc_sre(1) cpu=%u\n", CPU__id());
    wr_icc_sre(1);   // enable ICC_* system register interface
    __d_printf(DEBUG, "isb(1) cpu=%u\n", CPU__id());
    isb();
    __d_printf(DEBUG, "wr_icc_pmr(0xFF) cpu=%u\n", CPU__id());
    wr_icc_pmr(0xFF);
    __d_printf(DEBUG, "wr_icc_igrpen1(1) cpu=%u\n", CPU__id());
    wr_icc_igrpen1(1);

    // --- Arm the generic timer (per-CPU) ---
    __d_printf(DEBUG, "rd_cntfrq() cpu=%u\n", CPU__id());
    uint64_t frq  = rd_cntfrq();
    uint64_t tick = frq / (hz ? hz : 1000u); // default 1 kHz if hz==0

    if (use_virtual_timer) {
        __d_printf(DEBUG, "Using CNTV on cpu=%u\n", CPU__id());
        wr_cntv_ctl(0);               // disable while programming
        wr_cntv_tval(tick);
        wr_cntv_ctl(1);               // enable
    } else {
        __d_printf(DEBUG, "Using CNTP on cpu=%u\n", CPU__id());
        wr_cntp_ctl(0);
        wr_cntp_tval(tick);
        wr_cntp_ctl(1);
    }

    // Make sure timer registers are visible before enabling IRQs
    dsb_ish();
    isb();

    __d_printf(DEBUG, "Enabling IRQs in PSTATE on cpu=%u\n", CPU__id());
    enable_irq();

    __d_printf(DEBUG, "Timer IRQ armed on cpu=%u\n", CPU__id());
}

void CPUTimer__start(uint64_t hz)
{
    CPUTimer__enable(true, hz);
}
