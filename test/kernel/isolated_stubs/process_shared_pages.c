#include "../../../kernel/process.h"

Result(UserAddress) Process__map_shared_user_pages(
    ProcessPtr self,
    ProcessPtr source_process,
    Address source_virtual_address,
    size_t size
)
{
    return Success(UserAddress, ((UserAddress) {
        .virtual_addr = source_virtual_address,
        .kernel_addr = KERNEL_ADDR(source_virtual_address, source_virtual_address),
        .read = true,
        .write = true,
        .execute = false,
        .valid = true,
    }));
}

Result(ProcessMemoryCode) Process__free_user_pages(ProcessPtr self, Address virtual_address)
{
    return Success(ProcessMemoryCode, PROCESS_MEMORY_OK);
}
