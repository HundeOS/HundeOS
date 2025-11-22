#include "kstring.h"

#include "../debug.h"
#include "kstring_additional_types.h"

FixedArrayBodyDefine(KStringArray);


KStringArray KString__split(KStringPtr self, char delimiter)
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

int KString__length(KStringPtr self)
{
    return self->len;
}

const char * KString__cstr(KStringPtr self)
{
    return self->data;
}

KString KString__new(const char * str)
{
    KString res = EMPTY_KSTRING;

    int max_without_nul = res.max - 1;
    int len = 0;

    for (int i = 0; i < max_without_nul && *str != 0; i++, str++)
    {
        res.data[i] = *str;
        len++;
    }
    res.data[len + 1] = 0;
    res.len = len;

    return res;
}

KString kstring(const char * str)
{
    return KString__new(str);
}

bool KString__equals(const KStringPtr self, const KStringPtr other)
{
    __d_printf(DEBUG, "self len: %d, other len: %d \n", self->len, other->len);

    if (self->len != other->len)
    {
        return false;
    }

    for (uint64_t i = 0; i < self->len; i++)
    {
        __d_printf(DEBUG, "self: %c, other: %c \n", self->data[i], other->data[i]);
        if (self->data[i] != other->data[i])
        {
            __d_println(DEBUG, "char not equal");
            return false;
        }
    }

    return true;
}

KString KString__append(KStringPtr self, KStringPtr other)
{
    KString result = EMPTY_KSTRING;
    int len = self->len + other->len;
    result.len = len;

    for (int i = 0; i < self->len; i++)
    {
        result.data[i] = self->data[i];
    }
    for (int i = 0; i < other->len; i++)
    {
        result.data[i + self->len] = other->data[i];
    }

    return result;
}

bool KString__starts_with(KStringPtr self, KStringPtr prefix)
{
    if (self->len < prefix->len)
    {
        return false;
    }
    if (prefix->len == 0)
    {
        return false;
    }

    for (int i = 0; i < prefix->len; i++)
    {
        if (self->data[i] != prefix->data[i])
        {
            return false;
        }
    }
    return true;
}

KString KString__sprintf(KStringPtr self, ...)
{
    va_list args;
    va_start(args, self);
    KString result = EMPTY_KSTRING;
    int len = k_sprintf_impl(result.data, self->data, args);
    va_end(args);
    result.len = len;
    return result;
}