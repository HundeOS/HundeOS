#define FIXED_ARRAY_ALLOC(size) ((void *)0)
#include "bstring_additional_types.h"

FixedArrayBodyDefine(BStringArray);
#undef FIXED_ARRAY_ALLOC

bool BString__contains_char(ConstBStringPtr self, char ch)
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
        return (char)('0' + value);
    }

    return (char)('a' + value - 10);
}

static uint64_t BString__parse_digit_value(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return (uint64_t)(ch - '0');
    }
    if (ch >= 'a' && ch <= 'z')
    {
        return (uint64_t)(10 + ch - 'a');
    }
    if (ch >= 'A' && ch <= 'Z')
    {
        return (uint64_t)(10 + ch - 'A');
    }

    return 36;
}

static void BString__append_cstr_mut(BStringPtr self, const char * text)
{
    for (StringPos i = 0; text[i] != '\0'; i++)
    {
        if (!BString__push_char(self, text[i]))
        {
            return;
        }
    }
}

static void BString__append_bstring_mut(BStringPtr self, ConstBStringPtr text)
{
    if (text == NULL)
    {
        BString__append_cstr_mut(self, "(null)");
        return;
    }

    for (StringPos i = 0; i < BString__length(text); i++)
    {
        if (!BString__push_char(self, BString__char_at_or(text, i, '\0')))
        {
            return;
        }
    }
}

static void BString__append_cstr_limited_mut(BStringPtr self, const char * text, int precision)
{
    if (text == NULL)
    {
        text = "(null)";
    }

    for (StringPos i = 0; text[i] != '\0' && (precision < 0 || i < precision); i++)
    {
        if (!BString__push_char(self, text[i]))
        {
            return;
        }
    }
}

static void BString__append_bstring_limited_mut(BStringPtr self, ConstBStringPtr text, int precision)
{
    if (text == NULL)
    {
        BString__append_cstr_limited_mut(self, "(null)", precision);
        return;
    }

    for (StringPos i = 0; i < BString__length(text) && (precision < 0 || i < precision); i++)
    {
        if (!BString__push_char(self, BString__char_at_or(text, i, '\0')))
        {
            return;
        }
    }
}

static void BString__append_padding_mut(BStringPtr self, StringPos count, char ch)
{
    for (StringPos i = 0; i < count; i++)
    {
        if (!BString__push_char(self, ch))
        {
            return;
        }
    }
}

static void BString__append_formatted_field_mut(BStringPtr self, ConstBStringPtr field, int width, bool left_align,
                                                bool zero_pad)
{
    StringPos padding = 0;
    if (width > BString__length(field))
    {
        padding = width - BString__length(field);
    }

    if (!left_align)
    {
        BString__append_padding_mut(self, padding, zero_pad ? '0' : ' ');
    }

    BString__append_bstring_mut(self, field);

    if (left_align)
    {
        BString__append_padding_mut(self, padding, ' ');
    }
}

static void BString__append_reversed_mut(BStringPtr self, ConstBStringPtr reversed)
{
    BString copy = *reversed;
    while (!BString__is_empty(&copy))
    {
        StringPos last_index = BString__length(&copy) - 1;
        match_optional(
            BString__char_at(&copy, last_index),
            some(ch)
            {
                BString__push_char(self, ch);
                ;
            },
            none()
            {
                return;
                ;
            }
        );

        BString__remove_at(&copy, last_index);
    }
}

static void BString__append_uint64_dec_mut(BStringPtr self, uint64_t value)
{
    BString reversed = empty_bstr();

    if (value == 0)
    {
        BString__push_char(&reversed, '0');
    }

    while (value > 0)
    {
        BString__push_char(&reversed, (char)('0' + (value % 10)));
        value /= 10;
    }

    BString__append_reversed_mut(self, &reversed);
}

static void BString__append_int64_dec_mut(BStringPtr self, int64_t value)
{
    uint64_t magnitude = (uint64_t)value;

    if (value < 0)
    {
        BString__push_char(self, '-');
        magnitude = (uint64_t)(-(value + 1)) + 1;
    }

    BString__append_uint64_dec_mut(self, magnitude);
}

static void BString__append_uint64_hex_mut(BStringPtr self, uint64_t value)
{
    BString reversed = empty_bstr();

    if (value == 0)
    {
        BString__push_char(&reversed, '0');
    }

    while (value > 0)
    {
        BString__push_char(&reversed, BString__hex_digit((unsigned int)(value % 16)));
        value /= 16;
    }

    BString__append_reversed_mut(self, &reversed);
}

BString BString__slice(ConstBStringPtr self, StringPos start, StringPos end)
{
    BString result = empty_bstr();

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
    BString result = empty_bstr();
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
    *self = empty_bstr();
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

bool BString__set_char_at(BStringPtr self, StringPos index, char ch)
{
    if (index < 0 || index + 1 >= self->max)
    {
        return false;
    }

    self->data[index] = ch;
    if (index >= self->len)
    {
        self->len = index + 1;
        self->data[self->len] = '\0';
    }
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
    result.rest = empty_bstr();

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

BStringSplit BString__split_first_token(ConstBStringPtr self, ConstBStringPtr delimiters)
{
    BString stripped = BString__strip(self);
    return BString__split_first_any(&stripped, delimiters);
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

uint64_t BString__hash(ConstBStringPtr self)
{
    uint64_t hash = 14695981039346656037ULL;
    for_each_bstring(
        self,
        index,
        character,
        {
            hash ^= (uint8_t)character;
            hash *= 1099511628211ULL;
        }
    );
    return hash;
}

BString BString__from_chars(const char * chars, StringPos length)
{
    BString result = empty_bstr();
    for (StringPos i = 0; i < length; i++)
    {
        if (!BString__push_char(&result, chars[i]))
        {
            return result;
        }
    }

    return result;
}

bool bstring_equals(void * a, void * b)
{
    return BString__equals((ConstBStringPtr)a, (ConstBStringPtr)b);
}

BString BString__append(ConstBStringPtr self, ConstBStringPtr other)
{
    BString result = empty_bstr();
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
    BString result = empty_bstr();
    BString delim = empty_bstr();
    BString__push_char(&delim, delimiter);

    for_each_fa_ptr(
        BStringArray,
        array,
        index,
        segment,
        {
            result = BString__append(&result, &segment);
            if (index + 1 < BStringArray__len(array))
            {
                result = BString__append(&result, &delim);
            }
        }
    );

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

bool BString__is_valid(ConstBStringPtr self)
{
    return self->max > 0 && self->max <= BSTRING_MAX_SIZE && self->len >= 0 && self->len < self->max;
}

Optional(char) BString__char_at(ConstBStringPtr self, StringPos index)
{
    if (index < 0 || index >= BString__length(self))
    {
        return None(char);
    }

    return Some(char, self->data[index]);
}

char BString__char_at_or(ConstBStringPtr self, StringPos index, char alternate)
{
    match_optional(BString__char_at(self, index),
        some(ch)
        {
            return ch;
        },
        none()
        {
            return alternate;
        }
    );
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
    BString result = empty_bstr();
    for (StringPos i = 0; i < BString__length(self); i++)
    {
        char ch = BString__char_at_or(self, i, '\0');
        if (ch >= 'a' && ch <= 'z')
        {
            ch = (char)(ch - ('a' - 'A'));
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
    return BString__parse_uint64_base(self, 10);
}

Optional(uint64_t) BString__parse_uint64_base(ConstBStringPtr self, uint64_t base)
{
    if (self->len == 0)
    {
        return None(uint64_t);
    }

    BString stripped = BString__strip(self);
    StringPos position = 0;
    bool saw_digit = false;
    uint64_t result = 0;

    if (base == 0)
    {
        base = 10;
        if (stripped.len >= 2 && stripped.data[0] == '0' && (stripped.data[1] == 'x' || stripped.data[1] == 'X'))
        {
            base = 16;
            position = 2;
        }
        else if (stripped.len > 1 && stripped.data[0] == '0')
        {
            base = 8;
        }
    }

    else if (base == 16 && stripped.len >= 2 && stripped.data[0] == '0' &&
             (stripped.data[1] == 'x' || stripped.data[1] == 'X'))
    {
        position = 2;
    }

    if (base < 2 || base > 36)
    {
        return None(uint64_t);
    }

    for (StringPos i = position; i < stripped.len; i++)
    {
        uint64_t digit = BString__parse_digit_value(stripped.data[i]);
        if (digit >= base)
        {
            return None(uint64_t);
        }

        result = (result * base) + digit;
        saw_digit = true;
    }

    if (!saw_digit)
    {
        return None(uint64_t);
    }

    return Some(uint64_t, result);
}

BString BString__vsprintf(ConstBStringPtr self, va_list args)
{
    BString result = empty_bstr();

    if (self == NULL)
    {
        return result;
    }

    for (StringPos i = 0; i < BString__length(self); i++)
    {
        char format_char = BString__char_at_or(self, i, '\0');
        if (format_char != '%')
        {
            BString__push_char(&result, format_char);
            continue;
        }

        i++;
        format_char = BString__char_at_or(self, i, '\0');
        bool left_align = false;
        bool zero_pad = false;
        while (i < BString__length(self) && (format_char == '-' || format_char == '0'))
        {
            if (format_char == '-')
            {
                left_align = true;
            }
            if (format_char == '0')
            {
                zero_pad = true;
            }
            i++;
            format_char = BString__char_at_or(self, i, '\0');
        }

        if (i >= BString__length(self))
        {
            break;
        }

        int width = 0;
        if (format_char == '*')
        {
            width = va_arg(args, int);
            if (width < 0)
            {
                left_align = true;
                width = -width;
            }
            i++;
            format_char = BString__char_at_or(self, i, '\0');
        }
        else
        {
            while (i < BString__length(self) && format_char >= '0' && format_char <= '9')
            {
                width = (width * 10) + (format_char - '0');
                i++;
                format_char = BString__char_at_or(self, i, '\0');
            }
        }

        if (i >= BString__length(self))
        {
            break;
        }

        int precision = -1;
        if (format_char == '.')
        {
            precision = 0;
            i++;
            format_char = BString__char_at_or(self, i, '\0');
            if (format_char == '*')
            {
                precision = va_arg(args, int);
                if (precision < 0)
                {
                    precision = -1;
                }
                i++;
                format_char = BString__char_at_or(self, i, '\0');
            }
            else
            {
                while (i < BString__length(self) && format_char >= '0' && format_char <= '9')
                {
                    precision = (precision * 10) + (format_char - '0');
                    i++;
                    format_char = BString__char_at_or(self, i, '\0');
                }
            }
        }

        if (i >= BString__length(self))
        {
            break;
        }

        int long_count = 0;
        while (i < BString__length(self) && format_char == 'l' && long_count < 2)
        {
            long_count++;
            i++;
            format_char = BString__char_at_or(self, i, '\0');
        }

        if (i >= BString__length(self))
        {
            break;
        }

        switch (format_char)
        {
        case 'd':
        case 'i':
        {
            BString field = empty_bstr();
            int64_t value;
            if (long_count == 0)
            {
                value = va_arg(args, int);
            }
            else if (long_count == 1)
            {
                value = va_arg(args, long);
            }
            else
            {
                value = va_arg(args, long long);
            }

            BString__append_int64_dec_mut(&field, value);
            BString__append_formatted_field_mut(&result, &field, width, left_align, zero_pad);
            break;
        }

        case 'u':
        {
            BString field = empty_bstr();
            uint64_t value;
            if (long_count == 0)
            {
                value = va_arg(args, unsigned int);
            }
            else if (long_count == 1)
            {
                value = va_arg(args, unsigned long);
            }
            else
            {
                value = va_arg(args, unsigned long long);
            }

            BString__append_uint64_dec_mut(&field, value);
            BString__append_formatted_field_mut(&result, &field, width, left_align, zero_pad);
            break;
        }

        case 's':
        {
            BString field = empty_bstr();
            char * str = va_arg(args, char *);
            BString__append_cstr_limited_mut(&field, str, precision);
            BString__append_formatted_field_mut(&result, &field, width, left_align, zero_pad);
            break;
        }

        case 'B':
        {
            BString field = empty_bstr();
            ConstBStringPtr str = va_arg(args, ConstBStringPtr);
            BString__append_bstring_limited_mut(&field, str, precision);
            BString__append_formatted_field_mut(&result, &field, width, left_align, zero_pad);
            break;
        }

        case 'c':
        {
            BString field = empty_bstr();
            int ch = va_arg(args, int);
            BString__push_char(&field, (char)ch);
            BString__append_formatted_field_mut(&result, &field, width, left_align, zero_pad);
            break;
        }

        case 'p':
        case 'x':
        {
            BString field = empty_bstr();
            uint64_t value;
            if (format_char == 'p')
            {
                value = va_arg(args, uintptr_t);
            }
            else if (long_count == 0)
            {
                value = va_arg(args, unsigned int);
            }
            else if (long_count == 1)
            {
                value = va_arg(args, unsigned long);
            }
            else
            {
                value = va_arg(args, unsigned long long);
            }

            if (format_char == 'p')
            {
                BString__append_cstr_mut(&field, "0x");
            }

            BString__append_uint64_hex_mut(&field, value);
            BString__append_formatted_field_mut(&result, &field, width, left_align, zero_pad);
            break;
        }

        case '%':
        {
            BString__append_formatted_field_mut(&result, &bstr("%"), width, left_align, zero_pad);
            break;
        }

        default:
            break;
        }
    }

    return result;
}

BString BString__sprintf(ConstBStringPtr self, ...)
{
    va_list args;
    va_start(args, self);
    BString result = BString__vsprintf(self, args);
    va_end(args);
    return result;
}
