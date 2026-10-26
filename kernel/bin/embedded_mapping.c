#include "embedded_mapping.h"
#include "../kernel_core.h"
#include "../arch/page_table.h"

Result(ProcessSetupStatus) EmbeddedArchive__map_process(ProcessPtr self)
{
    if (!BString__equals(&self->name, &bstr(EMBEDDED_FS_HOST)))
    {
        return Success(ProcessSetupStatus, PROCESS_SETUP);
    }
    match_optional(
        self->kernel->system_info,
        some(info)
        {
            MemoryRegion region = info.embedded_binaries;
            if (region.physical_end <= region.physical_start || region.physical_start % 4096 != 0 ||
                region.physical_end % 4096 != 0 || region.virtual_start % 4096 != 0)
            {
                return Error(ProcessSetupStatus, bstr("Unaligned embedded archive"), PROCESS_FAILED_TO_SETUP);
            }
            size_t size = region.physical_end - region.physical_start;
            EmbeddedFs archive = EmbeddedFs__new(region.virtual_start, region.virtual_start + size);
            if (size > EMBEDDED_FS_MAX_SIZE || !EmbeddedFs__is_valid(&archive))
            {
                return Error(ProcessSetupStatus, bstr("Invalid embedded archive"), PROCESS_FAILED_TO_SETUP);
            }
            KernelAddress backing = KernelAddress__new(region.virtual_start, region.physical_start, true);
            UserAddress mapping = UserAddress__new(EMBEDDED_FS_VADDR, backing, true, false, false, true);
            PageTable__map_shared(&self->page_table, self->kernel, mapping, size);
            return Success(ProcessSetupStatus, PROCESS_SETUP);
        },
        none()
        {
            return Error(ProcessSetupStatus, bstr("Missing system information"), PROCESS_FAILED_TO_SETUP);
        }
    );
}
