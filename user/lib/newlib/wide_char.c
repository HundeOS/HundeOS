#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

enum
{
    WCTYPE_ALNUM = 1u << 0,
    WCTYPE_ALPHA = 1u << 1,
    WCTYPE_BLANK = 1u << 2,
    WCTYPE_CNTRL = 1u << 3,
    WCTYPE_DIGIT = 1u << 4,
    WCTYPE_GRAPH = 1u << 5,
    WCTYPE_LOWER = 1u << 6,
    WCTYPE_PRINT = 1u << 7,
    WCTYPE_PUNCT = 1u << 8,
    WCTYPE_SPACE = 1u << 9,
    WCTYPE_UPPER = 1u << 10,
    WCTYPE_XDIGIT = 1u << 11
};

typedef struct Utf8State
{
    uint32_t codepoint;
    uint8_t expected;
    uint8_t seen;
} Utf8State;

_Static_assert(sizeof(mbstate_t) >= sizeof(Utf8State), "mbstate_t must hold UTF-8 conversion state");

static Utf8State read_state(const mbstate_t * state)
{
    Utf8State value = {0};
    memcpy(&value, state, sizeof(value));
    return value;
}

static void write_state(mbstate_t * state, Utf8State value)
{
    memset(state, 0, sizeof(*state));
    memcpy(state, &value, sizeof(value));
}

static void reset_state(mbstate_t * state)
{
    memset(state, 0, sizeof(*state));
}

int mbsinit(const mbstate_t * state)
{
    return state == NULL || read_state(state).expected == 0;
}

static int invalid_codepoint(uint32_t codepoint)
{
    return codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff);
}

static size_t invalid_sequence(mbstate_t * state)
{
    reset_state(state);
    errno = EILSEQ;
    return (size_t)-1;
}

size_t mbrtowc(wchar_t * character, const char * text, size_t size, mbstate_t * state)
{
    static mbstate_t internal_state;
    mbstate_t * conversion = state == NULL ? &internal_state : state;
    Utf8State conversion_state = read_state(conversion);

    if (text == NULL)
    {
        reset_state(conversion);
        return 0;
    }
    if (size == 0)
    {
        return (size_t)-2;
    }

    size_t consumed = 0;
    if (conversion_state.expected == 0)
    {
        uint8_t first = (uint8_t)text[consumed++];
        if (first < 0x80)
        {
            if (character != NULL)
            {
                *character = first;
            }
            return first == 0 ? 0 : 1;
        }
        if (!__locale_is_utf8())
        {
            return invalid_sequence(conversion);
        }
        if (first >= 0xc2 && first <= 0xdf)
        {
            conversion_state.codepoint = first & 0x1f;
            conversion_state.expected = 1;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            conversion_state.codepoint = first & 0x0f;
            conversion_state.expected = 2;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            conversion_state.codepoint = first & 0x07;
            conversion_state.expected = 3;
        }
        else
        {
            return invalid_sequence(conversion);
        }
    }

    while (conversion_state.seen < conversion_state.expected && consumed < size)
    {
        uint8_t next = (uint8_t)text[consumed++];
        if ((next & 0xc0) != 0x80)
        {
            return invalid_sequence(conversion);
        }
        conversion_state.codepoint = (conversion_state.codepoint << 6) | (next & 0x3f);
        conversion_state.seen++;
    }
    if (conversion_state.seen < conversion_state.expected)
    {
        write_state(conversion, conversion_state);
        return (size_t)-2;
    }

    uint32_t codepoint = conversion_state.codepoint;
    uint32_t minimum = conversion_state.expected == 1 ? 0x80 : conversion_state.expected == 2 ? 0x800 : 0x10000;
    if (codepoint < minimum || invalid_codepoint(codepoint))
    {
        return invalid_sequence(conversion);
    }
    reset_state(conversion);
    if (character != NULL)
    {
        *character = (wchar_t)codepoint;
    }
    return consumed;
}

size_t mbrlen(const char * text, size_t size, mbstate_t * state)
{
    return mbrtowc(NULL, text, size, state);
}

int mblen(const char * text, size_t size)
{
    mbstate_t state = {0};
    size_t result = mbrlen(text, size, &state);
    return result == (size_t)-1 || result == (size_t)-2 ? -1 : (int)result;
}

int mbtowc(wchar_t * character, const char * text, size_t size)
{
    mbstate_t state = {0};
    size_t result = mbrtowc(character, text, size, &state);
    return result == (size_t)-1 || result == (size_t)-2 ? -1 : (int)result;
}

size_t wcrtomb(char * destination, wchar_t character, mbstate_t * state)
{
    static mbstate_t internal_state;
    mbstate_t * conversion = state == NULL ? &internal_state : state;
    uint32_t codepoint = (uint32_t)character;

    reset_state(conversion);
    if (destination == NULL)
    {
        return 1;
    }
    if (invalid_codepoint(codepoint) || (!__locale_is_utf8() && codepoint > 0x7f))
    {
        errno = EILSEQ;
        return (size_t)-1;
    }
    if (codepoint <= 0x7f)
    {
        destination[0] = (char)codepoint;
        return 1;
    }
    if (codepoint <= 0x7ff)
    {
        destination[0] = (char)(0xc0 | (codepoint >> 6));
        destination[1] = (char)(0x80 | (codepoint & 0x3f));
        return 2;
    }
    if (codepoint <= 0xffff)
    {
        destination[0] = (char)(0xe0 | (codepoint >> 12));
        destination[1] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
        destination[2] = (char)(0x80 | (codepoint & 0x3f));
        return 3;
    }
    destination[0] = (char)(0xf0 | (codepoint >> 18));
    destination[1] = (char)(0x80 | ((codepoint >> 12) & 0x3f));
    destination[2] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
    destination[3] = (char)(0x80 | (codepoint & 0x3f));
    return 4;
}

int wctomb(char * destination, wchar_t character)
{
    mbstate_t state = {0};
    size_t result = wcrtomb(destination, character, &state);
    return result == (size_t)-1 ? -1 : (int)result;
}

wint_t btowc(int character)
{
    if (character == EOF || (unsigned int)character > 0x7f)
    {
        return WEOF;
    }
    return (wint_t)character;
}

int wctob(wint_t character)
{
    return character <= 0x7f ? (int)character : EOF;
}

size_t mbsrtowcs(wchar_t * destination, const char ** source, size_t size, mbstate_t * state)
{
    const char * current = *source;
    size_t written = 0;

    while (*current != '\0')
    {
        wchar_t character;
        size_t result = mbrtowc(&character, current, __locale_mb_cur_max(), state);
        if (result == (size_t)-1 || result == (size_t)-2)
        {
            if (destination != NULL)
            {
                *source = current;
            }
            return (size_t)-1;
        }
        if (destination != NULL && written == size)
        {
            *source = current;
            return written;
        }
        if (destination != NULL)
        {
            destination[written] = character;
        }
        written++;
        current += result;
    }
    if (destination != NULL && written < size)
    {
        destination[written] = L'\0';
        *source = NULL;
    }
    else if (destination != NULL)
    {
        *source = current;
    }
    return written;
}

size_t mbstowcs(wchar_t * destination, const char * source, size_t size)
{
    mbstate_t state = {0};
    return mbsrtowcs(destination, &source, size, &state);
}

size_t wcsrtombs(char * destination, const wchar_t ** source, size_t size, mbstate_t * state)
{
    const wchar_t * current = *source;
    size_t written = 0;

    while (*current != L'\0')
    {
        char bytes[4];
        size_t count = wcrtomb(bytes, *current, state);
        if (count == (size_t)-1)
        {
            return (size_t)-1;
        }
        if (destination != NULL && count > size - written)
        {
            *source = current;
            return written;
        }
        if (destination != NULL)
        {
            memcpy(destination + written, bytes, count);
        }
        written += count;
        current++;
    }
    if (destination != NULL && written < size)
    {
        destination[written] = '\0';
        *source = NULL;
    }
    else if (destination != NULL)
    {
        *source = current;
    }
    return written;
}

size_t wcstombs(char * destination, const wchar_t * source, size_t size)
{
    mbstate_t state = {0};
    return wcsrtombs(destination, &source, size, &state);
}

size_t wcslen(const wchar_t * text)
{
    const wchar_t * end = text;
    while (*end != L'\0')
    {
        end++;
    }
    return (size_t)(end - text);
}

wchar_t * wcschr(const wchar_t * text, wchar_t character)
{
    while (*text != character)
    {
        if (*text == L'\0')
        {
            return NULL;
        }
        text++;
    }
    return (wchar_t *)text;
}

wchar_t * wmemchr(const wchar_t * text, wchar_t character, size_t size)
{
    for (size_t index = 0; index < size; index++)
    {
        if (text[index] == character)
        {
            return (wchar_t *)&text[index];
        }
    }
    return NULL;
}

wchar_t * wcscpy(wchar_t * destination, const wchar_t * source)
{
    wchar_t * result = destination;
    while ((*destination++ = *source++) != L'\0')
    {
    }
    return result;
}

int wcscmp(const wchar_t * left, const wchar_t * right)
{
    while (*left != L'\0' && *left == *right)
    {
        left++;
        right++;
    }
    return *left == *right ? 0 : *left < *right ? -1 : 1;
}

int wcscoll(const wchar_t * left, const wchar_t * right)
{
    return wcscmp(left, right);
}

int wcsncmp(const wchar_t * left, const wchar_t * right, size_t size)
{
    for (size_t index = 0; index < size; index++)
    {
        if (left[index] != right[index])
        {
            return left[index] < right[index] ? -1 : 1;
        }
        if (left[index] == L'\0')
        {
            return 0;
        }
    }
    return 0;
}

static int is_combining(wint_t character)
{
    return (character >= 0x0300 && character <= 0x036f) || (character >= 0x0483 && character <= 0x0489) ||
           (character >= 0x0591 && character <= 0x05bd) || character == 0x05bf ||
           (character >= 0x05c1 && character <= 0x05c2) || (character >= 0x0610 && character <= 0x061a) ||
           (character >= 0x064b && character <= 0x065f) || (character >= 0x06d6 && character <= 0x06ed) ||
           (character >= 0x0900 && character <= 0x0902) || character == 0x093c ||
           (character >= 0x0941 && character <= 0x0948) || character == 0x094d ||
           (character >= 0x1ab0 && character <= 0x1aff) || (character >= 0x1dc0 && character <= 0x1dff) ||
           (character >= 0x200b && character <= 0x200f) || character == 0x200d ||
           (character >= 0x202a && character <= 0x202e) || (character >= 0x2060 && character <= 0x206f) ||
           (character >= 0x20d0 && character <= 0x20ff) || (character >= 0xfe00 && character <= 0xfe0f) ||
           (character >= 0xfe20 && character <= 0xfe2f) || (character >= 0x1f3fb && character <= 0x1f3ff) ||
           (character >= 0xe0100 && character <= 0xe01ef);
}

static int is_wide(wint_t character)
{
    return (character >= 0x1100 && character <= 0x115f) || character == 0x2329 || character == 0x232a ||
           (character >= 0x2e80 && character <= 0xa4cf) || (character >= 0xac00 && character <= 0xd7a3) ||
           (character >= 0xf900 && character <= 0xfaff) || (character >= 0xfe10 && character <= 0xfe19) ||
           (character >= 0xfe30 && character <= 0xfe6f) || (character >= 0xff01 && character <= 0xff60) ||
           (character >= 0xffe0 && character <= 0xffe6) || (character >= 0x1f300 && character <= 0x1faff) ||
           (character >= 0x20000 && character <= 0x3fffd);
}

int wcwidth(wchar_t character)
{
    wint_t value = (wint_t)character;
    if (value == 0)
    {
        return 0;
    }
    if (invalid_codepoint(value) || value < 0x20 || (value >= 0x7f && value < 0xa0))
    {
        return -1;
    }
    if (is_combining(value))
    {
        return 0;
    }
    return is_wide(value) ? 2 : 1;
}

int wcswidth(const wchar_t * text, size_t size)
{
    int width = 0;
    for (size_t index = 0; index < size && text[index] != L'\0'; index++)
    {
        int character_width = wcwidth(text[index]);
        if (character_width < 0)
        {
            return -1;
        }
        width += character_width;
    }
    return width;
}

static int unicode_digit(wint_t character)
{
    return (character >= L'0' && character <= L'9') || (character >= 0x0660 && character <= 0x0669) ||
           (character >= 0x06f0 && character <= 0x06f9) || (character >= 0x0966 && character <= 0x096f) ||
           (character >= 0xff10 && character <= 0xff19);
}

static int unicode_alpha(wint_t character)
{
    return (character >= L'A' && character <= L'Z') || (character >= L'a' && character <= L'z') ||
           (character >= 0x00c0 && character <= 0x02af && character != 0x00d7 && character != 0x00f7) ||
           (character >= 0x0370 && character <= 0x052f) || (character >= 0x05d0 && character <= 0x05ea) ||
           (character >= 0x0620 && character <= 0x064a) || (character >= 0x0904 && character <= 0x0939) ||
           (character >= 0x3040 && character <= 0x30ff) || (character >= 0x3400 && character <= 0x9fff) ||
           (character >= 0xac00 && character <= 0xd7a3) || (character >= 0xf900 && character <= 0xfaff);
}

int iswcntrl(wint_t character)
{
    return character < 0x20 || (character >= 0x7f && character < 0xa0);
}

int iswspace(wint_t character)
{
    return (character >= 0x09 && character <= 0x0d) || character == 0x20 || character == 0x85 ||
           character == 0xa0 || character == 0x1680 || (character >= 0x2000 && character <= 0x200a) ||
           character == 0x2028 || character == 0x2029 || character == 0x202f || character == 0x205f ||
           character == 0x3000;
}

int iswblank(wint_t character)
{
    return character == L' ' || character == L'\t' || character == 0xa0 || character == 0x3000;
}

int iswdigit(wint_t character)
{
    return unicode_digit(character);
}

int iswalpha(wint_t character)
{
    return unicode_alpha(character);
}

int iswalnum(wint_t character)
{
    return iswalpha(character) || iswdigit(character);
}

int iswupper(wint_t character)
{
    return (character >= L'A' && character <= L'Z') || (character >= 0x00c0 && character <= 0x00d6) ||
           (character >= 0x00d8 && character <= 0x00de) || (character >= 0x0391 && character <= 0x03ab) ||
           (character >= 0x0410 && character <= 0x042f);
}

int iswlower(wint_t character)
{
    return (character >= L'a' && character <= L'z') || (character >= 0x00e0 && character <= 0x00f6) ||
           (character >= 0x00f8 && character <= 0x00fe) || (character >= 0x03b1 && character <= 0x03cb) ||
           (character >= 0x0430 && character <= 0x044f);
}

int iswprint(wint_t character)
{
    return !invalid_codepoint(character) && !iswcntrl(character) &&
           (character < 0xfdd0 || character > 0xfdef) && (character & 0xffff) != 0xfffe &&
           (character & 0xffff) != 0xffff;
}

int iswgraph(wint_t character)
{
    return iswprint(character) && !iswspace(character);
}

int iswpunct(wint_t character)
{
    return iswgraph(character) && !iswalnum(character);
}

int iswxdigit(wint_t character)
{
    return (character >= L'0' && character <= L'9') || (character >= L'A' && character <= L'F') ||
           (character >= L'a' && character <= L'f');
}

wint_t towlower(wint_t character)
{
    if (character >= L'A' && character <= L'Z')
    {
        return character + (L'a' - L'A');
    }
    if ((character >= 0x00c0 && character <= 0x00d6) || (character >= 0x00d8 && character <= 0x00de) ||
        (character >= 0x0391 && character <= 0x03ab) || (character >= 0x0410 && character <= 0x042f))
    {
        return character + 0x20;
    }
    return character;
}

wint_t towupper(wint_t character)
{
    if (character >= L'a' && character <= L'z')
    {
        return character - (L'a' - L'A');
    }
    if ((character >= 0x00e0 && character <= 0x00f6) || (character >= 0x00f8 && character <= 0x00fe) ||
        (character >= 0x03b1 && character <= 0x03cb) || (character >= 0x0430 && character <= 0x044f))
    {
        return character - 0x20;
    }
    return character;
}

wctype_t wctype(const char * name)
{
    static const char * names[] = {"alnum", "alpha", "blank", "cntrl", "digit", "graph",
                                   "lower", "print", "punct", "space", "upper", "xdigit"};
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++)
    {
        if (strcmp(name, names[index]) == 0)
        {
            return (wctype_t)(1u << index);
        }
    }
    return 0;
}

int iswctype(wint_t character, wctype_t type)
{
    return ((type & WCTYPE_ALNUM) != 0 && iswalnum(character)) ||
           ((type & WCTYPE_ALPHA) != 0 && iswalpha(character)) ||
           ((type & WCTYPE_BLANK) != 0 && iswblank(character)) ||
           ((type & WCTYPE_CNTRL) != 0 && iswcntrl(character)) ||
           ((type & WCTYPE_DIGIT) != 0 && iswdigit(character)) ||
           ((type & WCTYPE_GRAPH) != 0 && iswgraph(character)) ||
           ((type & WCTYPE_LOWER) != 0 && iswlower(character)) ||
           ((type & WCTYPE_PRINT) != 0 && iswprint(character)) ||
           ((type & WCTYPE_PUNCT) != 0 && iswpunct(character)) ||
           ((type & WCTYPE_SPACE) != 0 && iswspace(character)) ||
           ((type & WCTYPE_UPPER) != 0 && iswupper(character)) ||
           ((type & WCTYPE_XDIGIT) != 0 && iswxdigit(character));
}
