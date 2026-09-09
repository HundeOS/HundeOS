#include "fat_name.h"

bool FatName__is_valid_83_char(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
}

bool FatName__encode_83(ConstBStringPtr name, uint8_t target[FAT_NAME_83_SIZE])
{
    if (BString__length(name) == 0)
    {
        return false;
    }

    for (size_t i = 0; i < FAT_NAME_83_SIZE; i++)
    {
        target[i] = ' ';
    }

    BStringArray parts = BString__split(name, '.');
    size_t part_count = BStringArray__len(&parts);
    if (part_count == 0 || part_count > 2)
    {
        return false;
    }

    BString base = empty_bstr();
    match_optional(BStringArray__at(&parts, 0),
        some(segment) { base = segment; },
        none() { return false; }
    );
    if (BString__length(&base) == 0)
    {
        return false;
    }

    BString uppercase_base = BString__uppercase(&base);

    size_t target_index = 0;
    for (StringPos i = 0; i < BString__length(&uppercase_base); i++)
    {
        char ch = BString__char_at_or(&uppercase_base, i, '\0');

        if (!FatName__is_valid_83_char(ch) || target_index >= 8)
        {
            return false;
        }

        target[target_index] = (uint8_t)ch;
        target_index++;
    }

    if (part_count == 2)
    {
        BString extension = empty_bstr();
        match_optional(BStringArray__at(&parts, 1),
            some(segment) { extension = segment; },
            none() { return false; }
        );
        if (BString__length(&extension) == 0)
        {
            return false;
        }

        BString uppercase_extension = BString__uppercase(&extension);

        target_index = 8;
        for (StringPos i = 0; i < BString__length(&uppercase_extension); i++)
        {
            char ch = BString__char_at_or(&uppercase_extension, i, '\0');

            if (!FatName__is_valid_83_char(ch) || target_index >= FAT_NAME_83_SIZE)
            {
                return false;
            }

            target[target_index] = (uint8_t)ch;
            target_index++;
        }
    }

    return true;
}
