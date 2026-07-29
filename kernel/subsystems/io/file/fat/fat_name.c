#include "fat_name.h"

bool FatName__is_valid_83_char(char ch)
{
    return (ch >= 'A' && ch <= 'Z')
        || (ch >= '0' && ch <= '9')
        || ch == '_'
        || ch == '-';
}

bool FatName__encode_83(ConstKStringPtr name, uint8_t target[FAT_NAME_83_SIZE])
{
    if (KString__length(name) == 0)
    {
        return false;
    }

    for (size_t i = 0; i < FAT_NAME_83_SIZE; i++)
    {
        target[i] = ' ';
    }

    KStringArray parts = KString__split(name, '.');
    size_t part_count = KStringArray__len(&parts);
    if (part_count == 0 || part_count > 2)
    {
        return false;
    }

    KString base = Unwrap(KString, KStringArray__at(&parts, 0), EMPTY_KSTRING);
    if (KString__length(&base) == 0)
    {
        return false;
    }

    KString uppercase_base = KString__uppercase(&base);

    size_t target_index = 0;
    for (StringPos i = 0; i < KString__length(&uppercase_base); i++)
    {
        char ch = Unwrap(char, KString__char_at(&uppercase_base, i), '\0');

        if (!FatName__is_valid_83_char(ch) || target_index >= 8)
        {
            return false;
        }

        target[target_index] = (uint8_t) ch;
        target_index++;
    }

    if (part_count == 2)
    {
        KString extension = Unwrap(KString, KStringArray__at(&parts, 1), EMPTY_KSTRING);
        if (KString__length(&extension) == 0)
        {
            return false;
        }

        KString uppercase_extension = KString__uppercase(&extension);

        target_index = 8;
        for (StringPos i = 0; i < KString__length(&uppercase_extension); i++)
        {
            char ch = Unwrap(char, KString__char_at(&uppercase_extension, i), '\0');

            if (!FatName__is_valid_83_char(ch) || target_index >= FAT_NAME_83_SIZE)
            {
                return false;
            }

            target[target_index] = (uint8_t) ch;
            target_index++;
        }
    }

    return true;
}
