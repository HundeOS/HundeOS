#ifndef HUND_IO_PATH_H
#define HUND_IO_PATH_H
#include "types/bstring.h"

static inline bool IOPath__is_terminal_node(ConstBStringPtr path)
{
    StringPos prefix_length;
    if (BString__starts_with(path, &bstr("/dev/vtty")))
    {
        prefix_length = 9;
    }
    else if (BString__starts_with(path, &bstr("/dev/tty")))
    {
        prefix_length = 8;
    }
    else
    {
        return false;
    }
    BString number = BString__slice(path, prefix_length, BString__length(path));
    BString stripped = BString__strip(&number);
    if (!BString__equals(&number, &stripped))
    {
        return false;
    }
    match_optional(BString__parse_uint64(&number),
        some(index)
        {
            return true;
        },
        none()
        {
            return false;
        }
    );
    return false;
}

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
