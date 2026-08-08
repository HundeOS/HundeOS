#include "../../kernel/arch/system.h"

#include "../../kernel/arch/cpu.h"
#include "asm_helpers.h"
#include "io/psci.h"

void System__shutdown()
{
    dsb_sy();
    isb();

    if (smccc_hvc(PSCI_0_2_FN64_SYSTEM_OFF, 0, 0, 0).x0 != PSCI_RET_SUCCESS)
    {
        CPU__halt_all();
    }
}
