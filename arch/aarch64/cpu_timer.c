#include <stdint.h>

#include "../../kernel/debug.h"
#include "../../kernel/arch/cpu.h"
#include "../../kernel/cpu_timer.h"
#include "../../kernel/kernel_core.h"
#include "../../kernel/kernel_init.h"

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
    uint32_t ctlr = r32(GICD_BASE + GICD_CTLR_OFFSET);
    ctlr |= (1u<<4) /*ARE_NS*/ | (1u<<1) /*EnableGrp1NS*/;
    w32(GICD_BASE + GICD_CTLR_OFFSET, ctlr);
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
    CPUTimer__gicv3_dist_enable_grp1ns();
    volatile uint8_t * gicr = CPUTimer__gicr_base_this_cpu();

    // --- Wake up this redistributor ---
    uint32_t waker = mmio_read32(gicr + GICR_WAKER_OFFSET);

    if (waker & (1u << 1))
    {                // ProcessorSleep
        mmio_write32(waker & ~(1u << 1), gicr + GICR_WAKER_OFFSET);
        do
        {
            waker = mmio_read32(gicr + GICR_WAKER_OFFSET);
        } while (waker & (1u << 2));        // wait ChildrenAsleep==0
    }

    // All non-secure interrupts to Group1
    mmio_write32(0xFFFFFFFFu, gicr + GICR_IGROUPR0_OFFSET);

    // --- Configure timer PPI as edge-triggered ---
    unsigned ppi  = use_virtual_timer ? PPI_CNTV : PPI_CNTP;
    unsigned bit1 = ((ppi - 16u) * 2u) + 1u; // ICFGR1: bit[1]=1 => edge

    uint32_t icfgr1 = mmio_read32(gicr + GICR_ICFGR1_OFFSET);
    icfgr1 |= (1u << bit1);
    mmio_write32(icfgr1, gicr + GICR_ICFGR1_OFFSET);

    uint8_t * priority_reg = (gicr + GICR_IPRIORITYR_OFFSET(ppi));
    // Priority (byte-per-interrupt). Lower = higher prio; 0x80 is mid.
    *(volatile uint8_t *)(priority_reg) = 0x80;

    // Enable the timer PPI
    mmio_write32(1u << ppi, gicr + GICR_ISENABLER0_OFFSET);

    // Ensure GICR writes land before we enable the CPU interface
    dsb_ish();
    isb();

    // --- CPU interface: enable system registers & Group1Ns, unmask priority ---
    wr_icc_sre(1);   // enable ICC_* system register interface
    isb();
    wr_icc_pmr(0xFF);
    wr_icc_igrpen1(1);

    // --- Arm the generic timer (per-CPU) ---
    uint64_t frq  = rd_cntfrq();
    uint64_t tick = frq / (hz ? hz : 1000u); // default 1 kHz if hz==0

    if (use_virtual_timer)
    {
        wr_cntv_ctl(0);               // disable while programming
        wr_cntv_tval(tick);
        wr_cntv_ctl(1);               // enable
    }
    else
    {
        wr_cntp_ctl(0);
        wr_cntp_tval(tick);
        wr_cntp_ctl(1);
    }

    // Make sure timer registers are visible before enabling IRQs
    dsb_ish();
    isb();

    enable_irq();
}

void CPUTimer__start(uint64_t hz)
{
    CPUTimer__enable(true, hz);
}

static void CPUTimer__rearm(IRQNumber irq)
{
    uint64_t frq;
    __asm__ volatile("mrs %0, cntfrq_el0":"=r"(frq));
    if (irq == PPI_CNTV)
    {
        __asm__ volatile("msr cntv_tval_el0, %0" :: "r"(frq / 1000));
    }
    if (irq == PPI_CNTP)
    {
        __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(frq / 1000));
    }
}

Result(IRQDispatchStatus) CPUTimer__handle_irq(IRQDispatchContextPtr context, void * data)
{

    CPUTimer__rearm(context->irq);
    isb();
    if (!context->current_el)
    {
        KernelPtr kernel = Kernel__instance();
        timer_handler(context->cpu, kernel->scheduler, context->process_context);
    }
    return Success(IRQDispatchStatus, IRQ_DISPATCH_HANDLED);
}
