#include "clock.h"

#include "../../common/ipc/doorbell.h"
#include "../../common_includes/syscalls/mem.h"
#include "../all_defs.h"
#include "../arch/page_table.h"
#include "../kalloc.h"
#include "../process.h"

_Static_assert(CLOCK_PAGE_SIZE == PAGE_SIZE, "Clock page must occupy one page");
_Static_assert(CLOCK_PAGE_VADDR >= MEM_SHARED_PAGE_BASE + MEM_SHARED_PAGE_COUNT * MEM_SHARED_PAGE_SIZE,
               "Clock page overlaps managed shared pages");
_Static_assert(CLOCK_PAGE_VADDR + CLOCK_PAGE_SIZE <= IPC_DOORBELL_ADDRESS, "Clock page overlaps the IPC doorbell");

static BString clock_page_not_initialized = bstr("Clock page not initialized");

Result(ServiceCode)
    KernelClock__init(KernelClockPtr self, uint64_t timer_frequency, ConstOptionalRealtimeClockSamplePtr realtime_clock)
{
    match_result(
        Kernel__try_alloc_user(CLOCK_PAGE_VADDR, CLOCK_PAGE_SIZE, true, false, false),
        success(mapping)
        {
            Kernel__memset(mapping.kernel_addr.virtual_addr, 0, CLOCK_PAGE_SIZE);
            ClockPage__publish(k_v_ptr(mapping, ClockPage), timer_frequency, realtime_clock);
            self->mapping = mapping;
            return Success(ServiceCode, SERVICE_OK);
        },
        error(code, message)
        {
            return Error(ServiceCode, message, SERVICE_MAP_FAILED);
        }
    );

    return Error(ServiceCode, clock_page_not_initialized, SERVICE_MAP_FAILED);
}

Result(ServiceCode) KernelClock__map_process(KernelClockPtr self, ProcessPtr process)
{
    if (!self->mapping.valid)
    {
        return Error(ServiceCode, clock_page_not_initialized, SERVICE_MAP_FAILED);
    }

    PageTable__map_shared(&process->page_table, process->kernel, self->mapping, CLOCK_PAGE_SIZE);
    return Success(ServiceCode, SERVICE_OK);
}
