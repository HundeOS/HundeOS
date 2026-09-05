#include "../../../kernel/process.h"

Result(UserAddress) Process__map_shared_user_pages(
    ProcessPtr self,
    ProcessPtr source_process,
    Address source_virtual_address,
    size_t size
)
{
    return Success(UserAddress, UserAddress__new(
        source_virtual_address,
        KERNEL_ADDR(source_virtual_address, source_virtual_address),
        true,
        true,
        false,
        true
    ));
}

Result(SharedUserPages) Process__map_shared_user_pages_with_allocation(
    ProcessPtr self,
    ProcessPtr source_process,
    Address source_virtual_address,
    size_t size
)
{
    UserAddress mapping = UserAddress__new(
        source_virtual_address,
        KERNEL_ADDR(source_virtual_address, source_virtual_address),
        true,
        true,
        false,
        true
    );
    return Success(SharedUserPages, SharedUserPages__new(mapping, mapping));
}

Result(ProcessMemoryCode) Process__free_user_pages(ProcessPtr self, Address virtual_address)
{
    return Success(ProcessMemoryCode, PROCESS_MEMORY_OK);
}
