#include "dyn_string.h"

#ifdef DYN_STRING_USE_KERNEL_ALLOC
#include "../../kernel/kalloc.h"
#define DYN_STRING_ALLOC(size) ((void *) Kernel__alloc(size).virtual_addr)
#define DYN_STRING_FREE(ptr) \
    { \
        Result(KAllocCode) free_result = Kernel__free(addr(ptr)); \
        ResultMatch(KAllocCode, free_result) \
            IsError(code, message, {}) \
        End \
    }
#else
#ifdef __APPLE__
#include <stdlib.h>
#else
#include <malloc.h>
#endif
#define DYN_STRING_ALLOC(size) malloc(size)
#define DYN_STRING_FREE(ptr) free(ptr)
#endif

static BString allocation_failed = BSTRING_NEW("DynString allocation failed");
static BString invalid_capacity = BSTRING_NEW("DynString invalid capacity");

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

    char * data = DYN_STRING_ALLOC((size_t) capacity);
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

    char * data = DYN_STRING_ALLOC((size_t) capacity);
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
        ResultMatch(DynStringCode, DynString__reserve(self, DynString__next_capacity(self->capacity, required)))
            IsSuccess(code, {})
            IsError(code, message, return Error(DynStringCode, message, code))
        End
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
        ResultMatch(DynStringCode, DynString__push_char(self, text[i]))
            IsSuccess(code, {})
            IsError(code, message, return Error(DynStringCode, message, code))
        End
    }

    return Success(DynStringCode, DYN_STRING_OK);
}

Result(DynStringCode) DynString__append_bstring(DynStringPtr self, ConstBStringPtr text)
{
    BStringIterate(text, i, ch)
        ResultMatch(DynStringCode, DynString__push_char(self, ch))
            IsSuccess(code, {})
            IsError(code, message, return Error(DynStringCode, message, code))
        End
    }

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
