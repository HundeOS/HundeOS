#include <stdint.h>

#include "../../kernel/debug.h"
#include "../../kernel/all_defs.h"
#include "../../kernel/arch/cpu.h"
#include "../../kernel/cpu_timer.h"
#include "../../kernel/kernel_core.h"
#include "../../kernel/kernel_init.h"

#include "cpu_timer.h"
#include "cpu.h"
#include "asm_helpers.h"
#include "mmio_helpers.h"
#include "gicr.h"
#include "boot/boot_debug.h"

static uint64_t timer_interval = 1;

static inline unsigned CPUTimer__cpu_affinity(void)
{
    return (unsigned)(rd_mpidr() & 0xff);
}

static void CPUTimer__gicv3_dist_enable_grp1ns(InterruptControllerInfoPtr interrupt_controller)
{
    // Enable ARE_NS (bit 4) and EnableGrp1NS (bit 1). Leave grp0 off.
    uint32_t ctlr = r32(interrupt_controller->details.gic.distributor_virtual_base + GICD_CTLR_OFFSET);
    ctlr |= (1u << 4) /*ARE_NS*/ | (1u << 1) /*EnableGrp1NS*/;
    w32(interrupt_controller->details.gic.distributor_virtual_base + GICD_CTLR_OFFSET, ctlr);
    dsb_ish();
    isb();
}

// Map: redistributor base for this CPU (QEMU virt packs by Aff0)
static inline volatile uint8_t * CPUTimer__gicr_base_this_cpu(InterruptControllerInfoPtr interrupt_controller)
{
    return (volatile uint8_t *)(interrupt_controller->details.gic.redistributor_virtual_base +
                                (uint64_t)CPUTimer__cpu_affinity() * GICR_STRIDE);
}

static void CPUTimer__gicv2_enable_ppi(InterruptControllerInfoPtr interrupt_controller, unsigned ppi)
{
    Address distributor = interrupt_controller->details.gic.distributor_virtual_base;
    volatile uint32_t * group = (uint32_t *)(distributor + 0x080u);
    volatile uint32_t * enable = (uint32_t *)(distributor + 0x100u);
    volatile uint32_t * configuration = (uint32_t *)(distributor + 0x0c04u);
    volatile uint8_t * priority = (uint8_t *)(distributor + 0x400u + ppi);
    unsigned configuration_bit = ((ppi - 16u) * 2u) + 1u;

    *group |= (1u << ppi);
    *configuration &= ~(1u << configuration_bit);
    *priority = 0x80u;
    *enable = (1u << ppi);
    dsb_ish();
    isb();
}

/* Configure the GIC CPU interface and enable the selected timer PPI. */
__attribute__((noinline)) // keeps return addresses clean for debugging
static void CPUTimer__enable(SystemInfoPtr system_info, bool use_virtual_timer, uint64_t hz)
{
    InterruptControllerInfoPtr interrupt_controller = &system_info->interrupt_controller;
    unsigned ppi = use_virtual_timer ? PPI_CNTV : PPI_CNTP;

    if (interrupt_controller->kind == INTERRUPT_CONTROLLER_GIC_V2)
    {
        CPUTimer__gicv2_enable_ppi(interrupt_controller, ppi);
    }
    else if (interrupt_controller->kind == INTERRUPT_CONTROLLER_GIC_V3)
    {
        CPUTimer__gicv3_dist_enable_grp1ns(interrupt_controller);
        volatile uint8_t * gicr = CPUTimer__gicr_base_this_cpu(interrupt_controller);

        // --- Wake up this redistributor ---
        uint32_t waker = mmio_read32(gicr + GICR_WAKER_OFFSET);

        if (waker & (1u << 1))
        {
            mmio_write32(waker & ~(1u << 1), gicr + GICR_WAKER_OFFSET);
            do
            {
                waker = mmio_read32(gicr + GICR_WAKER_OFFSET);
            } while (waker & (1u << 2));
        }

        // All non-secure interrupts to Group1
        mmio_write32(0xFFFFFFFFu, gicr + GICR_IGROUPR0_OFFSET);

        unsigned bit1 = ((ppi - 16u) * 2u) + 1u;

        uint32_t icfgr1 = mmio_read32(gicr + GICR_ICFGR1_OFFSET);

        icfgr1 &= ~(1u << bit1);
        mmio_write32(icfgr1, gicr + GICR_ICFGR1_OFFSET);

        volatile uint8_t * priority_reg = (gicr + GICR_IPRIORITYR_OFFSET(ppi));
        *(volatile uint8_t *)(priority_reg) = 0x80;
        *(volatile uint8_t *)(gicr + GICR_IPRIORITYR_OFFSET(CPU_RESCHEDULE_INTERRUPT)) = 0x40;

        // Enable the timer PPI and the scheduler SGI.
        mmio_write32((1u << ppi) | (1u << CPU_RESCHEDULE_INTERRUPT), gicr + GICR_ISENABLER0_OFFSET);

        // Ensure GICR writes land before we enable the CPU interface
        dsb_ish();
        isb();

        // --- CPU interface: enable system registers & Group1Ns, unmask priority ---
        wr_icc_sre(1);
        isb();
        wr_icc_pmr(0xFF);
        wr_icc_igrpen1(1);
    }
    else
    {
        return;
    }

    // --- Arm the generic timer (per-CPU) ---
    uint64_t frq = rd_cntfrq();
    timer_interval = CPUTimer__interval(frq, hz);

    if (use_virtual_timer)
    {
        wr_cntv_ctl(0); // disable while programming
        wr_cntv_tval(timer_interval);
        wr_cntv_ctl(1); // enable
    }
    else
    {
        wr_cntp_ctl(0);
        wr_cntp_tval(timer_interval);
        wr_cntp_ctl(1);
    }

    // Make sure timer registers are visible before enabling IRQs
    dsb_ish();
    isb();

    enable_irq();
}

void CPUTimer__start(SystemInfoPtr system_info, uint64_t hz)
{
    CPUTimer__enable(system_info, true, hz);
}

void CPUTimer__rearm(IRQNumber irq)
{
    if (irq == PPI_CNTV)
    {
        __asm__ volatile("msr cntv_tval_el0, %0" ::"r"(timer_interval));
    }
    if (irq == PPI_CNTP)
    {
        __asm__ volatile("msr cntp_tval_el0, %0" ::"r"(timer_interval));
    }

    // Force system register synchronization right here!
    dsb_sy();
    isb();
}

void CPUTimer__suspend(void)
{
    wr_cntv_ctl(0);
    dsb_sy();
    isb();
}

void CPUTimer__resume(void)
{
    wr_cntv_ctl(0);
    wr_cntv_tval(timer_interval);
    wr_cntv_ctl(1);
    dsb_sy();
    isb();
}
