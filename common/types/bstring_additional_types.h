#ifndef __COMMON_TYPES_BSTRING_ADDITIONAL_TYPES_H
#define __COMMON_TYPES_BSTRING_ADDITIONAL_TYPES_H

#include "bstring.h"
#include "optional.h"
#include "result.h"

#ifndef FIXED_ARRAY_ALLOC
#define FIXED_ARRAY_ALLOC(size) ((void *) 0)
#define __COMMON_TYPES_BSTRING_FIXED_ARRAY_ALLOC_DEFINED
#endif

#include "fixed_array.h"

#ifdef __COMMON_TYPES_BSTRING_FIXED_ARRAY_ALLOC_DEFINED
#undef FIXED_ARRAY_ALLOC
#undef __COMMON_TYPES_BSTRING_FIXED_ARRAY_ALLOC_DEFINED
#endif

OptionalDef(BStringPtr);
ResultDef(BString);
FixedArrayDefine(BString, 1024, BStringArray, bstring_equals);

#endif
