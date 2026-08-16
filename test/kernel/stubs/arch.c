#include "../../../kernel/arch/init.h"

static uint64_t masked_irq = 0;
static uint64_t unmasked_irq = 0;
static uint64_t enabled_irq = 0;
static uint64_t mask_count = 0;
static uint64_t unmask_count = 0;
static uint64_t enable_count = 0;

Result(SubsystemInitStatus) Arch__init(KernelPtr kernel)
{
    return Success(SubsystemInitStatus, SUBSYSTEM_INIT_OK);
}

void Arch__mask_irq(InterruptControllerInfoPtr interrupt_controller, uint64_t irq)
{
    masked_irq = irq;
    mask_count++;
}

Result(SubsystemInitStatus) Arch__enable_irq(InterruptControllerInfoPtr interrupt_controller, uint64_t irq)
{
    enabled_irq = irq;
    enable_count++;
    return Success(SubsystemInitStatus, SUBSYSTEM_INIT_OK);
}

void Arch__unmask_irq(InterruptControllerInfoPtr interrupt_controller, uint64_t irq)
{
    unmasked_irq = irq;
    unmask_count++;
}

void Arch__reset_irq_tracking()
{
    masked_irq = 0;
    unmasked_irq = 0;
    enabled_irq = 0;
    mask_count = 0;
    unmask_count = 0;
    enable_count = 0;
}

uint64_t Arch__masked_irq()
{
    return masked_irq;
}

uint64_t Arch__unmasked_irq()
{
    return unmasked_irq;
}

uint64_t Arch__enabled_irq()
{
    return enabled_irq;
}

uint64_t Arch__mask_count()
{
    return mask_count;
}

uint64_t Arch__unmask_count()
{
    return unmask_count;
}

uint64_t Arch__enable_count()
{
    return enable_count;
}
