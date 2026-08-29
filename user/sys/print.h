#ifndef __USER_SYS_PRINT_H
#define __USER_SYS_PRINT_H

#include <stdarg.h>

#include "../../common/types/bstring.h"

void IO__print(ConstBStringPtr text);
int IO__vprintf(ConstBStringPtr format, va_list args);
int IO__printf(ConstBStringPtr format, ...);

#endif
