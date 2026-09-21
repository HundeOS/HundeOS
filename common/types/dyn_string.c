#include "dyn_string.h"

static BString allocation_failed = bstr("DynString allocation failed");
static BString invalid_capacity = bstr("DynString invalid capacity");

LinkedListBodyDefine(DynStringLines);

bool dyn_string_equals(void * a, void * b)
{
    DynStringPtr left = (DynStringPtr)a;
    DynStringPtr right = (DynStringPtr)b;
    if (DynString__length(left) != DynString__length(right))
    {
        return false;
    }

    for (StringPos i = 0; i < DynString__length(left); i++)
    {
        char left_ch = DynString__char_at_or(left, i, '\0');
        char right_ch = DynString__char_at_or(right, i, '\0');
        if (left_ch != right_ch)
        {
            return false;
        }
    }

    return true;
}

void DynStringLines__dispose_all(DynStringLinesPtr self)
{
    for_each_ll(
        DynStringLines,
        *self,
        line,
        {
            DynString__dispose(&line);
        }
    );

    match_result(DynStringLines__dispose(self), // hundelint: ignore-trivial-error-branch
        success_do_nothing(),
        error(code, message)
        {
            return;
        }
    );
}

static StringPos DynString__next_capacity(StringPos current, StringPos required)
{
    StringPos next = current > 0 ? current : 16;
    while (next < required)
    {
        next *= 2;
    }
    return next;
}

static void DynString__copy(char * target, const char * source, StringPos len)
{
    for (StringPos i = 0; i < len; i++)
    {
        target[i] = source[i];
    }
}

Result(DynStringCode) DynString__init(DynStringPtr self, StringPos capacity)
{
    if (capacity <= 0)
    {
        return Error(DynStringCode, invalid_capacity, DYN_STRING_INVALID_CAPACITY);
    }

    char * data = DYN_STRING_ALLOC((size_t)capacity);
    if (data == NULL)
    {
        return Error(DynStringCode, allocation_failed, DYN_STRING_ALLOCATION_FAILED);
    }

    self->len = 0;
    self->capacity = capacity;
    self->data = data;
    self->data[0] = '\0';
    return Success(DynStringCode, DYN_STRING_OK);
}

Result(DynStringCode) DynString__reserve(DynStringPtr self, StringPos capacity)
{
    if (capacity <= self->capacity)
    {
        return Success(DynStringCode, DYN_STRING_OK);
    }

    char * data = DYN_STRING_ALLOC((size_t)capacity);
    if (data == NULL)
    {
        return Error(DynStringCode, allocation_failed, DYN_STRING_ALLOCATION_FAILED);
    }

    DynString__copy(data, self->data, self->len);
    data[self->len] = '\0';
    DYN_STRING_FREE(self->data);
    self->data = data;
    self->capacity = capacity;
    return Success(DynStringCode, DYN_STRING_OK);
}

Result(DynStringCode) DynString__push_char(DynStringPtr self, char ch)
{
    StringPos required = self->len + 2;
    if (required > self->capacity)
    {
        match_result(
            DynString__reserve(self, DynString__next_capacity(self->capacity, required)),
            success_do_nothing(),
            error(code, message)
            {
                return Error(DynStringCode, message, code);
            }
        );
    }

    self->data[self->len] = ch;
    self->len++;
    self->data[self->len] = '\0';
    return Success(DynStringCode, DYN_STRING_OK);
}

Result(DynStringCode) DynString__append_cstr(DynStringPtr self, const char * text)
{
    for (StringPos i = 0; text[i] != '\0'; i++)
    {
        match_result(
            DynString__push_char(self, text[i]),
            success_do_nothing(),
            error(code, message)
            {
                return Error(DynStringCode, message, code);
            }
        );
    }

    return Success(DynStringCode, DYN_STRING_OK);
}

Result(DynStringCode) DynString__append_bstring(DynStringPtr self, ConstBStringPtr text)
{
    for_each_bstring(
        text,
        i,
        ch,
        {
            match_result(
                DynString__push_char(self, ch),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(DynStringCode, message, code);
                }
            );
        }
    );

    return Success(DynStringCode, DYN_STRING_OK);
}

void DynString__clear(DynStringPtr self)
{
    self->len = 0;
    self->data[0] = '\0';
}

void DynString__dispose(DynStringPtr self)
{
    if (self->capacity > 0)
    {
        DYN_STRING_FREE(self->data);
    }
    self->len = 0;
    self->capacity = 0;
}

const char * DynString__cstr(ConstDynStringPtr self)
{
    return self->data;
}

StringPos DynString__length(ConstDynStringPtr self)
{
    return self->len;
}

StringPos DynString__capacity(ConstDynStringPtr self)
{
    return self->capacity;
}

Optional(char) DynString__char_at(ConstDynStringPtr self, StringPos index)
{
    if (index < 0 || index >= self->len)
    {
        return None(char);
    }

    return Some(char, self->data[index]);
}

char DynString__char_at_or(ConstDynStringPtr self, StringPos index, char alternate)
{
    match_optional(DynString__char_at(self, index),
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

Optional(BString) DynString__bstring(ConstDynStringPtr self)
{
    BString text = empty_bstr();
    for (StringPos i = 0; i < DynString__length(self); i++)
    {
        char ch = DynString__char_at_or(self, i, '\0');
        if (!BString__push_char(&text, ch))
        {
            return None(BString);
        }
    }

    return Some(BString, text);
}

static bool DynString__push_line(DynStringLinesPtr lines, ConstDynStringPtr line)
{
    match_result(
        DynStringLines__push(lines, *line),
        success(index)
        {
            return true;
        },
        error(code, message)
        {
            return false;
        }
    );

    return false;
}

Optional(DynStringLines) DynString__lines(ConstDynStringPtr self)
{
    DynStringLines lines = DynStringLines__new();
    DynString line;
    match_result(
        DynString__init(&line, 16),
        success_do_nothing(),
        error(code, message)
        {
            return None(DynStringLines);
        }
    );

    for (StringPos i = 0; i < DynString__length(self); i++)
    {
        char ch = DynString__char_at_or(self, i, '\0');
        if (ch == '\n')
        {
            if (!DynString__push_line(&lines, &line))
            {
                DynString__dispose(&line);
                DynStringLines__dispose_all(&lines);
                return None(DynStringLines);
            }

            match_result(
                DynString__init(&line, 16),
                success_do_nothing(),
                error(code, message)
                {
                    DynStringLines__dispose_all(&lines);
                    return None(DynStringLines);
                }
            );
            continue;
        }

        match_result(
            DynString__push_char(&line, ch),
            success_do_nothing(),
            error(code, message)
            {
                DynString__dispose(&line);
                DynStringLines__dispose_all(&lines);
                return None(DynStringLines);
            }
        );
    }

    if (DynString__length(&line) > 0)
    {
        if (!DynString__push_line(&lines, &line))
        {
            DynString__dispose(&line);
            DynStringLines__dispose_all(&lines);
            return None(DynStringLines);
        }
    }
    else
    {
        DynString__dispose(&line);
    }

    return Some(DynStringLines, lines);
}
