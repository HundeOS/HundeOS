#include <stdlib.h>

#include "../../../kernel/process.h"
#include "../../../kernel/subsystems/io/types.h"

Result(UserAddress) Process__alloc_user_pages(ProcessPtr self, size_t size)
{
    DataPtr data = (DataPtr) malloc(size);
    if (data == NULL) // hundelint: ignore-no-null
    {
        return Error(UserAddress, BString__new("test allocation failed"), PROCESS_MEMORY_FULL);
    }

    return Success(UserAddress, ((UserAddress) {
        .virtual_addr = addr(data),
        .kernel_addr = KERNEL_ADDR(addr(data), addr(data)),
        .read = true,
        .write = true,
        .execute = false,
        .valid = true,
    }));
}

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
    free(void_ptr(virtual_address));
    return Success(ProcessMemoryCode, PROCESS_MEMORY_OK);
}
