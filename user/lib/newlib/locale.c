#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

static int utf8_locale;

static int valid_category(int category)
{
    return category >= LC_ALL && category <= LC_MESSAGES;
}

static const char * environment_locale(void)
{
    const char * locale = getenv("LC_ALL");
    if (locale == NULL || locale[0] == '\0')
    {
        locale = getenv("LC_CTYPE");
    }
    if (locale == NULL || locale[0] == '\0')
    {
        locale = getenv("LANG");
    }
    return locale == NULL || locale[0] == '\0' ? "C.UTF-8" : locale;
}

char * setlocale(int category, const char * locale)
{
    static char c_locale[] = "C";
    static char utf8_name[] = "C.UTF-8";

    if (!valid_category(category))
    {
        errno = EINVAL;
        return NULL;
    }
    if (locale == NULL)
    {
        return utf8_locale ? utf8_name : c_locale;
    }
    if (locale[0] == '\0')
    {
        locale = environment_locale();
    }
    if (strcmp(locale, "C") == 0 || strcmp(locale, "POSIX") == 0)
    {
        utf8_locale = 0;
        return c_locale;
    }
    if (strcmp(locale, "C.UTF-8") == 0 || strcmp(locale, "C.utf8") == 0 || strcmp(locale, "UTF-8") == 0)
    {
        utf8_locale = 1;
        return utf8_name;
    }

    errno = EINVAL;
    return NULL;
}

size_t __locale_mb_cur_max(void)
{
    return utf8_locale ? 4 : 1;
}

int __locale_is_utf8(void)
{
    return utf8_locale;
}
