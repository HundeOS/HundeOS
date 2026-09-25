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

#endif
