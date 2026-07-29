#include "bstring.h"

static bool BString__contains_char(ConstBStringPtr self, char ch)
{
    for (StringPos i = 0; i < self->len; i++)
    {
        if (self->data[i] == ch)
        {
            return true;
        }
    }

    return false;
}

BString BString__slice(ConstBStringPtr self, StringPos start, StringPos end)
{
    BString result = EMPTY_BSTRING;

    if (end == -1)
    {
        end = self->len;
    }

    if (start < 0)
    {
        start = 0;
    }

    if (end < 0)
    {
        end = 0;
    }

    if (start > self->len)
    {
        start = self->len;
    }

    if (end > self->len)
    {
        end = self->len;
    }

    if (start >= end)
    {
        return result;
    }

    for (StringPos i = start; i < end; i++)
    {
        if (!BString__push_char(&result, self->data[i]))
        {
            break;
        }
    }

    return result;
}

BString BString__new(const char * str)
{
    BString result = EMPTY_BSTRING;
    StringPos max_without_nul = result.max - 1;
    StringPos len = 0;

    for (StringPos i = 0; i < max_without_nul && str[i] != 0; i++)
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

void BString__clear(BStringPtr self)
{
    *self = EMPTY_BSTRING;
}

bool BString__is_empty(ConstBStringPtr self)
{
    return self->len == 0;
}

bool BString__is_whitespace(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\0';
}

bool BString__push_char(BStringPtr self, char ch)
{
    if (self->len + 1 >= self->max)
    {
        return false;
    }

    self->data[self->len] = ch;
    self->len++;
    self->data[self->len] = '\0';
    return true;
}

BString BString__strip(ConstBStringPtr self)
{
    StringPos start = 0;
    StringPos end = self->len;

    while (start < self->len && BString__is_whitespace(self->data[start]))
    {
        start++;
    }

    while (end > start && BString__is_whitespace(self->data[end - 1]))
    {
        end--;
    }

    return BString__slice(self, start, end);
}

BStringSplit BString__split_first_any(ConstBStringPtr self, ConstBStringPtr delimiters)
{
    BStringSplit result;
    result.match = None(BString);
    result.rest = EMPTY_BSTRING;

    if (self->len == 0)
    {
        return result;
    }

    for (StringPos i = 0; i < self->len; i++)
    {
        if (BString__contains_char(delimiters, self->data[i]))
        {
            result.match = Some(BString, BString__slice(self, 0, i));
            result.rest = BString__slice(self, i + 1, self->len);
            return result;
        }
    }

    result.match = Some(BString, *self);
    return result;
}

bool BString__equals(ConstBStringPtr self, ConstBStringPtr other)
{
    if (self->len != other->len)
    {
        return false;
    }

    for (StringPos i = 0; i < self->len; i++)
    {
        if (self->data[i] != other->data[i])
        {
            return false;
        }
    }

    return true;
}

bool bstring_equals(void * a, void * b)
{
    return BString__equals((ConstBStringPtr) a, (ConstBStringPtr) b);
}

BString BString__append(ConstBStringPtr self, ConstBStringPtr other)
{
    BString result = EMPTY_BSTRING;
    StringPos target = self->len + other->len;
    if (target >= result.max)
    {
        target = result.max - 1;
    }

    StringPos written = 0;
    for (StringPos i = 0; i < self->len && written < target; i++, written++)
    {
        result.data[written] = self->data[i];
    }
    for (StringPos i = 0; i < other->len && written < target; i++, written++)
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

    for (StringPos i = 0; i < prefix->len; i++)
    {
        if (self->data[i] != prefix->data[i])
        {
            return false;
        }
    }

    return true;
}

StringPos BString__copy_to(ConstBStringPtr self, char * target, StringPos target_size)
{
    if (target_size == 0)
    {
        return 0;
    }

    StringPos position = 0;
    while (position + 1 < target_size && position < self->len)
    {
        target[position] = self->data[position];
        position++;
    }
    target[position] = '\0';
    return position;
}

const char * BString__cstr(ConstBStringPtr self)
{
    return self->data;
}
