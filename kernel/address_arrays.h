#ifndef __KERNEL_ADDRESS_ARRAYS_H
#define __KERNEL_ADDRESS_ARRAYS_H
#include "types/fixed_array.h"
#include "address.h"

OptionalDef(UserAddress);
FixedArrayDefine(UserAddress, 1024, UserAddressArray);

#endif