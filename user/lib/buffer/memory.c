#include "client.h"
#include "sys/syscall.h"

int64_t BufferMemory__operation(MemOperation operation, uint64_t first, uint64_t second, uint64_t third,
                                uint64_t fourth)
{
    return (int64_t)syscall(MEM_CALL, operation, first, second, third, fourth);
}
