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
    match_optional(DynString, read_result,
        some(fstab)
        {
            if (DynString__length(&fstab) <= 0)
            {
                DynString__dispose(&fstab);
                return None(DynString);
            }

            return Some(DynString, fstab);
        },
        none() {}
    )

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
    match_optional(BString, split.match,
        some(token)
        {
            *remaining = split.rest;
            return Some(BString, token);
        },
        none()
        {
            *remaining = EMPTY_BSTRING;
            return Some(BString, stripped);
        }
    )

    *remaining = EMPTY_BSTRING;
    return None(BString);
}

static BString Humpen__line_without_comment(ConstBStringPtr line)
{
    BStringSplit split = BString__split_first_any(line, &humpen_comment_delimiter);
    match_optional(BString, split.match,
        some(before_comment) { return before_comment; },
        none() {}
    )

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
    match_optional(BString, Humpen__next_token(&remaining),
        some(token) { command = token; },
        none() { return 0; }
    )

    if (!BString__equals(&command, &humpen_device_command))
    {
        printf("humpen: invalid fstab line\n");
        return 1;
    }

    BString device_path = EMPTY_BSTRING;
    match_optional(BString, Humpen__next_token(&remaining),
        some(token) { device_path = token; },
        none()
        {
            printf("humpen: invalid device path\n");
            return 1;
        }
    )

    BString filesystem_type = EMPTY_BSTRING;
    match_optional(BString, Humpen__next_token(&remaining),
        some(token) { filesystem_type = token; },
        none()
        {
            printf("humpen: invalid filesystem type\n");
            return 1;
        }
    )

    BString mountpoint = EMPTY_BSTRING;
    match_optional(BString, Humpen__next_token(&remaining),
        some(token) { mountpoint = token; },
        none()
        {
            printf("humpen: invalid mountpoint\n");
            return 1;
        }
    )

    match_optional(BString, Humpen__next_token(&remaining),
        some(extra)
        {
            printf("humpen: unexpected fstab token\n");
            return 1;
        },
        none() {}
    )

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

    match_optional(DynStringLines, DynString__lines(fstab),
        some(lines)
        {
            LLIterate(DynStringLines, lines, line)
                match_optional(BString, DynString__bstring(&line),
                    some(bstring_line) { failures += Humpen__mount_device_line(bstring_line); },
                    none()
                    {
                        printf("humpen: fstab line too long\n");
                        failures++;
                    }
                )

            End
            DynStringLines__dispose_all(&lines);
        },
        none()
        {
            printf("humpen: failed to split fstab lines\n");
            failures++;
        }
    )

    return failures;
}

int HUMPEN_ENTRYPOINT(void)
{
    int result = 1;
    match_optional(DynString, Humpen__load_fstab(),
        some(fstab)
        {
            result = Humpen__mount_fstab(&fstab) == 0 ? 0 : 1;
            DynString__dispose(&fstab);
        },
        none() { printf("humpen: no fstab found\n"); }
    )

    return result;
}
