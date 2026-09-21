#ifndef HUND_IO_PATH_H
#define HUND_IO_PATH_H
#include "types/bstring.h"

static inline bool IOPath__is_stream(ConstBStringPtr path)
{
    StringPos length = BString__length(path);
    if (length < 3 || BString__char_at_or(path, 0, '\0') != ':' || BString__char_at_or(path, length - 1, '\0') != ':')
    {
        return false;
    }
    BString name = BString__slice(path, 1, length - 1);
    return !BString__contains_char(&name, ':') && !BString__contains_char(&name, '/') &&
           !BString__contains_char(&name, '\0') && !BString__contains_char(&name, ' ') &&
           !BString__contains_char(&name, '\t') && !BString__contains_char(&name, '\r') &&
           !BString__contains_char(&name, '\n');
}

#endif
