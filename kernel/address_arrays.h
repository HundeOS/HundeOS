#ifndef __KERNEL_ADDRESS_ARRAYS_H
#define __KERNEL_ADDRESS_ARRAYS_H
#include "types/fixed_array.h"
#include "address.h"
#include "types/optional.h"
#include "types/comparison.h"

OptionalDef(UserAddress);

bool Ptr__equals(void *, void *);

#endif
