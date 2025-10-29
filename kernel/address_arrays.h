#ifndef __KERNEL_ADDRESS_ARRAYS_H
#define __KERNEL_ADDRESS_ARRAYS_H
#include "types/fixed_array.h"
#include "address.h"
#include "types/optional.h"
#include "types/comparison.h"

OptionalDef(UserAddress);
SimpleEqualsWrapperDef(UserAddress__equals)

FixedArrayDefine(UserAddress, 1024, UserAddressArray, SimpleEqualsWrapper(UserAddress__equals));

bool Ptr__equals(void *, void *);

#endif