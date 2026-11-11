#ifndef HUNDE_SYS_THREAD_LOCAL_ARCH_H
#define HUNDE_SYS_THREAD_LOCAL_ARCH_H

#include <stddef.h>
#include "../../../common/types/address.h"

Address ThreadLocalArch__pointer(void);
void ThreadLocalArch__set_pointer(Address pointer);
size_t ThreadLocalArch__control_prefix_size(void);

#endif
