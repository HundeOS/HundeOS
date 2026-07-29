#include "kstring.h"

#include "../print.h"
#include "kstring_additional_types.h"

FixedArrayBodyDefine(KStringArray);


KStringArray KString__split(ConstKStringPtr self, char delimiter)
{
    KStringArray result = EmptyArray(KStringArray);
    char buffer[MAX_SIZE];

    StringPos start = 0;
    for (StringPos i = 0; i < self->len; i++)
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

StringPos KString__length(ConstKStringPtr self)
{
    return self->len;
}

Optional(char) KString__char_at(ConstKStringPtr self, StringPos index)
{
    if (index < 0 || index >= KString__length(self))
    {
        return None(char);
    }

    return Some(char, self->data[index]);
}

KString KString__uppercase(ConstKStringPtr self)
{
    KString result = EMPTY_KSTRING;
    result.len = KString__length(self);

    for (StringPos i = 0; i < KString__length(self); i++)
    {
        char ch = Unwrap(char, KString__char_at(self, i), '\0');
        if (ch >= 'a' && ch <= 'z')
        {
            result.data[i] = (char) (ch - ('a' - 'A'));
        }
        else
        {
            result.data[i] = ch;
        }
    }

    result.data[result.len] = '\0';
    return result;
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

StringPos KString__copy_to(ConstKStringPtr self, char * target, StringPos target_size)
{
    return BString__copy_to(self, target, target_size);
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
