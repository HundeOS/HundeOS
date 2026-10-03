#ifndef __USER_LIB_NEWLIB_LOCALE_H
#define __USER_LIB_NEWLIB_LOCALE_H

#include <stddef.h>

#define LC_ALL 0
#define LC_COLLATE 1
#define LC_CTYPE 2
#define LC_MONETARY 3
#define LC_NUMERIC 4
#define LC_TIME 5
#define LC_MESSAGES 6

char * setlocale(int category, const char * locale);
size_t __locale_mb_cur_max(void);
int __locale_is_utf8(void);

#endif
