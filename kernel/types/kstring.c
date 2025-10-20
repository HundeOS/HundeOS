#include "kstring.h"

#include "../debug.h"

const char * KString__cstr(KString * self)
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

bool KString__equals(KStringPtr self, KStringPtr other)
{
    __d_printf(DEBUG, "self len: %d, other len: %d \n", self->len, other->len);
    if (self->len != other->len) return false;
    bool result = true;
    for (int i = 0; i < self->len; i++)
    {
        __d_printf(DEBUG, "self: %c, other: %c \n", self->data[i], other->data[i]);
        if (self->data[i] != other->data[i])
        {
            __d_println(DEBUG, "char not equal");
            result = false;
            break;
        }
    }
    return result;
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