#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "sys/print.h"

static int LibcProbe__compare_ints(const void * left, const void * right)
{
    int left_value = *(const int *)left;
    int right_value = *(const int *)right;
    return (left_value > right_value) - (left_value < right_value);
}

static int LibcProbe__check_memory_and_strings(void)
{
    const char * source = "abcdef";
    void * destination = malloc(16);
    void * padded = malloc(6);
    void * bounded = malloc(4);
    if (!destination || !padded || !bounded)
    {
        free(destination);
        free(padded);
        free(bounded);
        return 1;
    }

    int failed = 0;
    memset(destination, 'x', 16);
    memcpy(destination, source, strlen(source) + 1);
    memmove((uint8_t *)destination + 2, destination, 4);
    if (memcmp(destination, "ababcd", 6) != 0 || memchr(destination, 'c', 6) != (uint8_t *)destination + 4)
    {
        failed = 1;
    }

    bcopy("cat", destination, 4);
    if (strcmp(destination, "cat") != 0 || strlen(destination) != 3)
    {
        failed = 1;
    }

    strcpy(destination, "one");
    strcat(destination, " two");
    strncat(destination, " three!", 6);
    if (strcmp(destination, "one two three") != 0 || strncmp(destination, "one", 3) != 0)
    {
        failed = 1;
    }

    memset(padded, 'x', 6);
    strncpy(padded, "cat", 6);
    if (memcmp(padded, "cat\0\0\0", 6) != 0)
    {
        failed = 1;
    }

    if (strchr(destination, 't') != (char *)destination + 4 || strrchr(destination, 'e') != (char *)destination + 12 ||
        strstr(destination, "two") != (char *)destination + 4 || strspn("abc123", "abc") != 3 ||
        strcspn("abc123", "0123456789") != 3 || *strpbrk("shell", "aeiou") != 'e')
    {
        failed = 1;
    }

    if (strlcpy(bounded, "hello", 4) != 5 || memcmp(bounded, "hel\0", 4) != 0)
    {
        failed = 1;
    }

    free(destination);
    free(padded);
    free(bounded);
    return failed;
}

static int LibcProbe__check_qsort(void)
{
    int values[] = {7, -2, 7, 0, 3, -9, 3};
    const int expected[] = {-9, -2, 0, 3, 3, 7, 7};

    qsort(values, sizeof(values) / sizeof(values[0]), sizeof(values[0]), LibcProbe__compare_ints);
    return memcmp(values, expected, sizeof(values)) != 0;
}

static int LibcProbe__check_integer_conversions(void)
{
    void * end;

    errno = 0;
    if (strtol(" \t-0x2a!", (char **)&end, 0) != -42 || *(const char *)end != '!' || errno != 0)
    {
        return 1;
    }

    errno = 0;
    if (strtol("9223372036854775808", (char **)&end, 10) != LONG_MAX || *(const char *)end != '\0' || errno != ERANGE)
    {
        return 1;
    }

    errno = 0;
    if (strtoul("18446744073709551616", (char **)&end, 10) != ULONG_MAX || *(const char *)end != '\0' ||
        errno != ERANGE)
    {
        return 1;
    }

    errno = 0;
    if (strtoll("-9223372036854775808", (char **)&end, 10) != LLONG_MIN || *(const char *)end != '\0' || errno != 0)
    {
        return 1;
    }

    errno = 0;
    if (strtoull("18446744073709551615", (char **)&end, 10) != ULLONG_MAX || *(const char *)end != '\0' || errno != 0)
    {
        return 1;
    }

    errno = 0;
    return strtol("1", (char **)&end, 1) != 0 || errno != EINVAL;
}

int main(void)
{
    if (LibcProbe__check_memory_and_strings() != 0 || LibcProbe__check_qsort() != 0 ||
        LibcProbe__check_integer_conversions() != 0)
    {
        IO__print(&bstr("libc_probe: failed\n"));
        return 1;
    }

    IO__print(&bstr("libc_probe: ok\n"));
    return 0;
}
