#include "../../../common/types/dyn_string.h"
#include "../../sys/io.h"
#include "../../user.h"

#ifndef HUMPEN_ENTRYPOINT
#define HUMPEN_ENTRYPOINT main
#endif

#define HUMPEN_FSTAB_PATH "/boot/config/fstab"
#define HUMPEN_FSTAB_MAX 2048U

static BString humpen_space_delimiters = BSTRING_NEW(" \t\r\n");
static BString humpen_comment_delimiter = BSTRING_NEW("#");
static BString humpen_device_command = BSTRING_NEW("device");

static Optional(DynString) Humpen__load_fstab(void)
{
    IOHandle handle = open(HUMPEN_FSTAB_PATH, IO_OPEN_READ);
    if (handle < 0)
    {
        return None(DynString);
    }

    Optional(DynString) read_result = IO__read_dyn_string(handle, HUMPEN_FSTAB_MAX);

    close(handle);
    Match(DynString, read_result)
        IsSome(fstab,
        {
            if (DynString__length(&fstab) <= 0)
            {
                DynString__dispose(&fstab);
                return None(DynString);
            }

            return Some(DynString, fstab);
        })
    End

    return None(DynString);
}

static Optional(BString) Humpen__next_token(BStringPtr remaining)
{
    BString stripped = BString__strip(remaining);
    if (BString__is_empty(&stripped))
    {
        *remaining = EMPTY_BSTRING;
        return None(BString);
    }

    BStringSplit split = BString__split_first_any(&stripped, &humpen_space_delimiters);
    Match(BString, split.match)
        IsSome(token,
        {
            *remaining = split.rest;
            return Some(BString, token);
        })
        IsNone(
        {
            *remaining = EMPTY_BSTRING;
            return Some(BString, stripped);
        })
    End

    *remaining = EMPTY_BSTRING;
    return None(BString);
}

static BString Humpen__line_without_comment(ConstBStringPtr line)
{
    BStringSplit split = BString__split_first_any(line, &humpen_comment_delimiter);
    Match(BString, split.match)
        IsSome(before_comment, return before_comment)
    End

    return BString__slice(line, 0, BString__length(line));
}

static int Humpen__mount_device_line(BString line)
{
    BString command_line = Humpen__line_without_comment(&line);
    BString remaining = BString__strip(&command_line);
    if (BString__is_empty(&remaining))
    {
        return 0;
    }

    BString command = EMPTY_BSTRING;
    Match(BString, Humpen__next_token(&remaining))
        IsSome(token, command = token)
        IsNone(return 0)
    End

    if (!BString__equals(&command, &humpen_device_command))
    {
        printf("humpen: invalid fstab line\n");
        return 1;
    }

    BString device_path = EMPTY_BSTRING;
    Match(BString, Humpen__next_token(&remaining))
        IsSome(token, device_path = token)
        IsNone(
        {
            printf("humpen: invalid device path\n");
            return 1;
        })
    End

    BString filesystem_type = EMPTY_BSTRING;
    Match(BString, Humpen__next_token(&remaining))
        IsSome(token, filesystem_type = token)
        IsNone(
        {
            printf("humpen: invalid filesystem type\n");
            return 1;
        })
    End

    BString mountpoint = EMPTY_BSTRING;
    Match(BString, Humpen__next_token(&remaining))
        IsSome(token, mountpoint = token)
        IsNone(
        {
            printf("humpen: invalid mountpoint\n");
            return 1;
        })
    End

    Match(BString, Humpen__next_token(&remaining))
        IsSome(extra,
        {
            printf("humpen: unexpected fstab token\n");
            return 1;
        })
    End

    IOStatus status = IO__mount_device(&device_path, &filesystem_type, &mountpoint);
    if (status != IO_OK)
    {
        printf(
            "humpen: failed to mount %s on %s: %i\n",
            BString__cstr(&device_path),
            BString__cstr(&mountpoint),
            (int) status
        );
        return 1;
    }

    return 0;
}

static int Humpen__mount_fstab(ConstDynStringPtr fstab)
{
    int failures = 0;

    Match(DynStringLines, DynString__lines(fstab))
        IsSome(lines,
        {
            LLIterate(DynStringLines, lines, line)
                Match(BString, DynString__bstring(&line))
                    IsSome(bstring_line, failures += Humpen__mount_device_line(bstring_line))
                    IsNone(
                    {
                        printf("humpen: fstab line too long\n");
                        failures++;
                    })
                End
            End

            DynStringLines__dispose_all(&lines);
        })
        IsNone(
        {
            printf("humpen: failed to split fstab lines\n");
            failures++;
        })
    End

    return failures;
}

int HUMPEN_ENTRYPOINT(void)
{
    int result = 1;
    Match(DynString, Humpen__load_fstab())
        IsSome(fstab,
        {
            result = Humpen__mount_fstab(&fstab) == 0 ? 0 : 1;
            DynString__dispose(&fstab);
        })
        IsNone(printf("humpen: no fstab found\n"))
    End

    return result;
}
