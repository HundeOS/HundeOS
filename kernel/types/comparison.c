#include "comparison.h"
#include <stdint.h>

#include "bstring.h"
#include "types.h"

SimpleEqualsBody(int) SimpleEqualsBody(bool) SimpleEqualsBody(uint64_t) SimpleEqualsBody(uint32_t)
    SimpleEqualsBody(uint16_t) SimpleEqualsBody(uint8_t) SimpleEqualsBody(int64_t) SimpleEqualsBody(int32_t)
        SimpleEqualsBody(int16_t) SimpleEqualsBody(int8_t) SimpleEqualsBody(char) SimpleEqualsBody(Char)

    // wil be a pointer to an element which is a pointer
    bool Ptr__equals(void * a, void * b)
{
    return *(void **)a == *(void **)b;
}

bool never__equals(void * a, void * b)
{
    return false;
}

HashCode hash__uint64(uint64_t * key)
{
    return *key;
}
