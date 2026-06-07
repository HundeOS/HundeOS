#ifndef __KERNEL_TYPES_KSTRING_OPTIONALS_H
#define __KERNEL_TYPES_KSTRING_OPTIONALS_H

#include "kstring.h"
#include "optional.h"
#include "fixed_array.h"
#include "comparison.h"

OptionalDef(KString);
OptionalDef(KStringPtr);

FixedArrayDefine(KString, 1024, KStringArray, kstring_equals);

#endif