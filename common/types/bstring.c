#define FIXED_ARRAY_ALLOC(size) ((void *) 0)
#include "bstring_additional_types.h"

FixedArrayBodyDefine(BStringArray);
#undef FIXED_ARRAY_ALLOC

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

static char BString__hex_digit(unsigned int value)
{
    if (value < 10)
    {
        return (char) ('0' + value);
    }

    return (char) ('A' + value - 10);
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

bool BString__insert_char(BStringPtr self, StringPos index, char ch)
{
    if (index < 0 || index > self->len || self->len + 1 >= self->max)
    {
        return false;
    }

    for (StringPos i = self->len; i > index; i--)
    {
        self->data[i] = self->data[i - 1];
    }

    self->data[index] = ch;
    self->len++;
    self->data[self->len] = '\0';
    return true;
}

bool BString__remove_at(BStringPtr self, StringPos index)
{
    if (index < 0 || index >= self->len)
    {
        return false;
    }

    for (StringPos i = index; i + 1 < self->len; i++)
    {
        self->data[i] = self->data[i + 1];
    }

    self->len--;
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

BStringArray BString__split(ConstBStringPtr self, char delimiter)
{
    BStringArray result = BStringArray__new();
    char buffer[BSTRING_MAX_SIZE];
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
                BString segment = BString__new(&buffer[start]);
                BStringArray__push(&result, segment);
            }
            start = i + 1;
        }
    }

    if (start != self->len)
    {
        buffer[self->len] = '\0';
        BString segment = BString__new(&buffer[start]);
        BStringArray__push(&result, segment);
    }

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

BString BString__join(BStringArrayPtr array, char delimiter)
{
    BString result = EMPTY_BSTRING;
    BString delim = EMPTY_BSTRING;
    BString__push_char(&delim, delimiter);

    FAPtrIterate(BStringArray, array, index, segment)
        result = BString__append(&result, &segment);
        if (index + 1 < BStringArray__len(array))
        {
            result = BString__append(&result, &delim);
        }
    End

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

StringPos BString__length(ConstBStringPtr self)
{
    return self->len;
}

Optional(char) BString__char_at(ConstBStringPtr self, StringPos index)
{
    if (index < 0 || index >= BString__length(self))
    {
        return None(char);
    }

    return Some(char, self->data[index]);
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

BString BString__uppercase(ConstBStringPtr self)
{
    BString result = EMPTY_BSTRING;
    for (StringPos i = 0; i < BString__length(self); i++)
    {
        char ch = Unwrap(char, BString__char_at(self, i), '\0');
        if (ch >= 'a' && ch <= 'z')
        {
            ch = (char) (ch - ('a' - 'A'));
        }

        if (!BString__push_char(&result, ch))
        {
            break;
        }
    }

    return result;
}

Optional(uint64_t) BString__parse_uint64(ConstBStringPtr self)
{
    if (self->len == 0)
    {
        return None(uint64_t);
    }

    uint64_t result = 0;
    for (StringPos i = 0; i < self->len; i++)
    {
        char ch = self->data[i];
        if (ch < '0' || ch > '9')
        {
            return None(uint64_t);
        }

        result = result * 10 + (uint64_t) (ch - '0');
    }

    return Some(uint64_t, result);
}

BString BString__vsprintf(ConstBStringPtr format, va_list args)
{
    BString result = EMPTY_BSTRING;

    if (format == NULL)
    {
        return result;
    }

    int str_idx = 0;

    for (StringPos i = 0; i < format->len && str_idx + 1 < result.max; i++)
    {
        if (format->data[i] != '%')
        {
            result.data[str_idx++] = format->data[i];
            continue;
        }

        i++;
        while (i < format->len && format->data[i] >= '0' && format->data[i] <= '9')
        {
            i++;
        }

        if (i >= format->len)
        {
            break;
        }

        switch (format->data[i])
        {
            case 'd':
            case 'i':
            {
                int value = va_arg(args, int);
                char temp[20];
                int temp_idx = 0;
                bool is_negative = false;

                if (value < 0)
                {
                    is_negative = true;
                    value = -value;
                }

                if (value == 0)
                {
                    temp[temp_idx++] = '0';
                }
                else
                {
                    int start = temp_idx;
                    while (value > 0)
                    {
                        temp[temp_idx++] = (char) ('0' + (value % 10));
                        value /= 10;
                    }
                    for (int j = 0; j < (temp_idx - start) / 2; j++)
                    {
                        char swap = temp[start + j];
                        temp[start + j] = temp[temp_idx - 1 - j];
                        temp[temp_idx - 1 - j] = swap;
                    }
                }

                if (is_negative && str_idx + 1 < result.max)
                {
                    result.data[str_idx++] = '-';
                }

                for (int j = 0; j < temp_idx && str_idx + 1 < result.max; j++)
                {
                    result.data[str_idx++] = temp[j];
                }
                break;
            }

            case 's':
            {
                char * str = va_arg(args, char *);
                if (str == NULL)
                {
                    str = "(null)";
                }
                while (*str != '\0' && str_idx + 1 < result.max)
                {
                    result.data[str_idx++] = *str++;
                }
                break;
            }

            case 'c':
            {
                int ch = va_arg(args, int);
                result.data[str_idx++] = (char) ch;
                break;
            }

            case 'p':
            case 'x':
            {
                unsigned int value = va_arg(args, unsigned int);
                char temp[20];
                int temp_idx = 0;

                if (value == 0)
                {
                    temp[temp_idx++] = '0';
                }
                else
                {
                    int start = temp_idx;
                    while (value > 0)
                    {
                        temp[temp_idx++] = BString__hex_digit(value % 16);
                        value /= 16;
                    }
                    for (int j = 0; j < (temp_idx - start) / 2; j++)
                    {
                        char swap = temp[start + j];
                        temp[start + j] = temp[temp_idx - 1 - j];
                        temp[temp_idx - 1 - j] = swap;
                    }
                }

                if (format->data[i] == 'p' && str_idx + 2 < result.max)
                {
                    result.data[str_idx++] = '0';
                    result.data[str_idx++] = 'x';
                }

                for (int j = 0; j < temp_idx && str_idx + 1 < result.max; j++)
                {
                    result.data[str_idx++] = temp[j];
                }
                break;
            }

            case '%':
            {
                result.data[str_idx++] = '%';
                break;
            }

            default:
                break;
        }
    }

    result.data[str_idx] = '\0';
    result.len = str_idx;
    return result;
}

BString BString__sprintf(ConstBStringPtr format, ...)
{
    va_list args;
    va_start(args, format);
    BString result = BString__vsprintf(format, args);
    va_end(args);
    return result;
}
