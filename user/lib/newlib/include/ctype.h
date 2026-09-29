#ifndef __USER_LIB_NEWLIB_CTYPE_H
#define __USER_LIB_NEWLIB_CTYPE_H

static inline int isascii(int character)
{
    return (unsigned int)character <= 0x7fU;
}

static inline int toascii(int character)
{
    return character & 0x7f;
}

static inline int iscntrl(int character)
{
    unsigned int value = (unsigned int)character;
    return value < 0x20U || value == 0x7fU;
}

static inline int isprint(int character)
{
    unsigned int value = (unsigned int)character;
    return value >= 0x20U && value < 0x7fU;
}

static inline int isdigit(int character)
{
    return character >= '0' && character <= '9';
}

static inline int islower(int character)
{
    return character >= 'a' && character <= 'z';
}

static inline int isupper(int character)
{
    return character >= 'A' && character <= 'Z';
}

static inline int isalpha(int character)
{
    return islower(character) || isupper(character);
}

static inline int isalnum(int character)
{
    return isalpha(character) || isdigit(character);
}

static inline int isspace(int character)
{
    return character == ' ' || character == '\t' || character == '\n' || character == '\r' || character == '\f' ||
           character == '\v';
}

static inline int isblank(int character)
{
    return character == ' ' || character == '\t';
}

static inline int ispunct(int character)
{
    return isprint(character) && !isalnum(character) && !isspace(character);
}

static inline int isxdigit(int character)
{
    return isdigit(character) || (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

static inline int tolower(int character)
{
    return isupper(character) ? character + ('a' - 'A') : character;
}

static inline int toupper(int character)
{
    return islower(character) ? character - ('a' - 'A') : character;
}

#endif
