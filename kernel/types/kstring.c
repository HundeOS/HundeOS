#include "kstring.h"

#include "../print.h"
#include "kstring_additional_types.h"

FixedArrayBodyDefine(KStringArray);


KStringArray KString__split(ConstKStringPtr self, char delimiter)
{
    KStringArray result = EmptyArray(KStringArray);
    char buffer[MAX_SIZE];

    int64_t start = 0;
    for (int64_t i = 0; i < self->len; i++)
    {
        if (self->data[i] != delimiter)
        {
            buffer[i] = self->data[i];
        }
        else
        {
            buffer[i] = '\0';
            if (start != i)
            {
                KString str = KString__new(&buffer[start]);
                KStringArray__push(&result, str);
            }
            start = i + 1;
        }
    }
    if (start != self->len)
    {
        buffer[self->len] = '\0';
        KString str = KString__new(&buffer[start]);
        KStringArray__push(&result, str);
    }

    return result;
}

KString KString__join(KStringArrayPtr array, char delimiter)
{
    KString result = EMPTY_KSTRING;
    char * delim_buffer = NULL_TERM(delimiter);

    KString delim = KString__new(delim_buffer);

    FAPtrIterate(KStringArray, array, i, segment)
        result = KString__append(&result, &segment);
        if (i < KStringArray__len(array) - 1)
        {
            result = KString__append(&result, &delim);
        }
    End
    return result;
}

int KString__length(ConstKStringPtr self)
{
    return self->len;
}

const char * KString__cstr(ConstKStringPtr self)
{
    return BString__cstr(self);
}

KString KString__new(const char * str)
{
    return BString__new(str);
}

KString kstring(const char * str)
{
    return bstring(str);
}

bool KString__equals(ConstKStringPtr self, ConstKStringPtr other)
{
    return BString__equals(self, other);
}

KString KString__append(ConstKStringPtr self, ConstKStringPtr other)
{
    return BString__append(self, other);
}

bool KString__starts_with(ConstKStringPtr self, ConstKStringPtr prefix)
{
    return BString__starts_with(self, prefix);
}

KString KString__sprintf(ConstKStringPtr self, ...)
{
    va_list args;
    va_start(args, self);
    KString result = EMPTY_KSTRING;
    int len = k_sprintf_impl(result.data, self->data, args);
    va_end(args);
    result.len = len;
    return result;
}
