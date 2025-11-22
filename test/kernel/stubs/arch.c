#include "../../../kernel/arch/init.h"

Result(SubsystemInitStatus) Arch__init()
{
    return Success(SubsystemInitStatus, SUBSYSTEM_INIT_OK);
}