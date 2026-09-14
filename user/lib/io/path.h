#ifndef HUND_IO_PATH_H
#define HUND_IO_PATH_H
#include "types/bstring.h"

static inline bool IOPath__is_bootstrap(ConstBStringPtr path)
{
    return BString__starts_with(path, &bstr(":")) || BString__equals(path, &bstr("/boot/config")) ||
           BString__starts_with(path, &bstr("/boot/config/")) || BString__equals(path, &bstr("/boot/sysinfo"));
}
#endif
