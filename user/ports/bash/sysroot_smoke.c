#include <errno.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

_Static_assert(CHAR_MIN == 0 && CHAR_MAX == UCHAR_MAX, "plain char must match the target's unsigned ABI");
_Static_assert(INT_MIN == (-INT_MAX - 1), "int limits must describe two's-complement int");
_Static_assert(LONG_MIN == (-LONG_MAX - 1L), "long limits must describe the LP64 ABI");
_Static_assert(LLONG_MIN == (-LLONG_MAX - 1LL), "long long limits must be complete");

static int exercise_longjmp(void)
{
    jmp_buf environment;
    int result = setjmp(environment);
    if (result == 0)
    {
        longjmp(environment, 7);
    }
    return result == 7 ? 0 : 1;
}

int main(int argc, char ** argv)
{
    volatile double base = 10.0;
    double power = pow(base, 3.0);
    void * end;
    errno = 0;
    long parsed = strtol("42", (char **)&end, 10);
    return argc > 0 && strlen(argv[0]) > 0 && PATH_MAX > 0 && power == 1000.0 && parsed == 42 &&
                   *(const char *)end == '\0' && errno == 0 && exercise_longjmp() == 0
               ? 0
               : 1;
}
