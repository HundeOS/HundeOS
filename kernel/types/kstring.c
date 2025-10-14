#include "kstring.h"

KString KString__new(const char * str)
{
    KString res = (KString){
        .len = 0,
        .max = MAX_SIZE,
        .data[0] = 0
    };
    int max = res.max - 1;
    for (int i = 0; i < max && *str != 0; i++, str++)
    {
        res.data[i] = *str;
    }
    res.data[max] = 0;
    return res;
}

KString kstring(const char * str)
{
    return KString__new(str);
}