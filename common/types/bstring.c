#include "bstring.h"

BString BString__new(const char * str)
{
    BString result = EMPTY_BSTRING;
    uint64_t max_without_nul = result.max - 1;
    uint64_t len = 0;

    for (uint64_t i = 0; i < max_without_nul && str[i] != 0; i++)
    {
        result.data[i] = str[i];
        len++;
    }

    result.data[len] = 0;
    result.len = len;
    return result;
}

BString bstring(const char * str)
{
    return BString__new(str);
}

bool BString__equals(ConstBStringPtr self, ConstBStringPtr other)
{
    if (self->len != other->len)
    {
        return false;
    }

    for (uint64_t i = 0; i < self->len; i++)
    {
        if (self->data[i] != other->data[i])
        {
            return false;
        }
    }

    return true;
}

BString BString__append(ConstBStringPtr self, ConstBStringPtr other)
{
    BString result = EMPTY_BSTRING;
    uint64_t target = self->len + other->len;
    if (target >= result.max)
    {
        target = result.max - 1;
    }

    uint64_t written = 0;
    for (uint64_t i = 0; i < self->len && written < target; i++, written++)
    {
        result.data[written] = self->data[i];
    }
    for (uint64_t i = 0; i < other->len && written < target; i++, written++)
    {
        result.data[written] = other->data[i];
    }

    result.data[written] = 0;
    result.len = written;
    return result;
}

bool BString__starts_with(ConstBStringPtr self, ConstBStringPtr prefix)
{
    if (self->len < prefix->len || prefix->len == 0)
    {
        return false;
    }

    for (uint64_t i = 0; i < prefix->len; i++)
    {
        if (self->data[i] != prefix->data[i])
        {
            return false;
        }
    }

    return true;
}

const char * BString__cstr(ConstBStringPtr self)
{
    return self->data;
}
