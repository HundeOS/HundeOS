#include <stdint.h>
#include "../../../common/types/result.h"
#include "../../../common/types/dyn_string.h"
#include "sys/io.h"
#include "sys/spawn.h"
#include "../../user.h"

#ifndef HUMPEN_ENTRYPOINT
#define HUMPEN_ENTRYPOINT main
#endif

#define HUMPEN_FSTAB_PATH "/boot/config/fstab"
#define HUMPEN_FSTAB_MAX 2048U
#define HUMPEN_WACHHUND_PATH "/sbin/services/humpen-wachhund"

static BString humpen_space_delimiters = bstr(" \t\r\n");
static BString humpen_comment_delimiter = bstr("#");
static BString humpen_device_command = bstr("device");
static BString humpen_wachhund_path = bstr(HUMPEN_WACHHUND_PATH);
static BString humpen_invalid_fstab_line = bstr("invalid fstab line");
static BString humpen_invalid_device_path = bstr("invalid device path");
static BString humpen_invalid_filesystem_type = bstr("invalid filesystem type");
static BString humpen_invalid_mountpoint = bstr("invalid mountpoint");
static BString humpen_unexpected_fstab_token = bstr("unexpected fstab token");


typedef uint64_t MountResult;
ResultDef(MountResult);

const MountResult HUMPEN_PARSE_ERROR = 1;
const MountResult HUMPEN_FSTAB_ERROR = 2;


typedef struct HumpenMountCommand
{
    ConstBStringPtr driver_path;
    BString device_path;
    BString filesystem_type;
    BString mountpoint;
    Optional(BString) arg0;
    Optional(BString) arg1;
    Optional(BString) arg2;
}
HumpenMountCommand;

ResultDef(HumpenMountCommand);

static HumpenMountCommand HumpenMountCommand__new(
    ConstBStringPtr device_path,
    ConstBStringPtr filesystem_type,
    ConstBStringPtr mountpoint
)
{
    HumpenMountCommand command = {
        .driver_path = &humpen_wachhund_path,
        .device_path = *device_path,
        .filesystem_type = *filesystem_type,
        .mountpoint = *mountpoint,
        .arg0 = Some(BString, *device_path),
        .arg1 = Some(BString, *filesystem_type),
        .arg2 = Some(BString, *mountpoint),
    };

    return command;
}

static Optional(DynString) Humpen__load_fstab(void)
{
    IOHandle handle = open(&bstr(HUMPEN_FSTAB_PATH), IO_OPEN_READ);
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
        none_do_nothing()
    );

    return None(DynString);
}

static BString Humpen__line_without_comment(ConstBStringPtr line)
{
    BStringSplit split = BString__split_first_any(line, &humpen_comment_delimiter);
    match_optional(BString, split.match,
        some(before_comment) { return before_comment; },
        none_do_nothing()
    );

    return BString__slice(line, 0, BString__length(line));
}

static Result(HumpenMountCommand) Humpen__parse_mount_command(ConstBStringPtr input)
{
    BString remaining = *input;
    BString device_path = empty_bstr();
    BString filesystem_type = empty_bstr();
    BString mountpoint = empty_bstr();

    BStringSplit command_split = BString__split_first_token(&remaining, &humpen_space_delimiters);
    match_optional(BString, command_split.match,
        some(token)
        {
            if (!BString__equals(&token, &humpen_device_command))
            {
                return Error(HumpenMountCommand, humpen_invalid_fstab_line, HUMPEN_PARSE_ERROR);
            }

            remaining = command_split.rest;
        },
        none()
        {
            return Error(HumpenMountCommand, humpen_invalid_fstab_line, HUMPEN_PARSE_ERROR);
        }
    );

    BStringSplit device_path_split = BString__split_first_token(&remaining, &humpen_space_delimiters);
    match_optional(BString, device_path_split.match,
        some(token)
        {
            device_path = token;
            remaining = device_path_split.rest;
        },
        none()
        {
            return Error(HumpenMountCommand, humpen_invalid_device_path, HUMPEN_PARSE_ERROR);
        }
    );

    BStringSplit filesystem_type_split = BString__split_first_token(&remaining, &humpen_space_delimiters);
    match_optional(BString, filesystem_type_split.match,
        some(token)
        {
            filesystem_type = token;
            remaining = filesystem_type_split.rest;
        },
        none()
        {
            return Error(HumpenMountCommand, humpen_invalid_filesystem_type, HUMPEN_PARSE_ERROR);
        }
    );

    BStringSplit mountpoint_split = BString__split_first_token(&remaining, &humpen_space_delimiters);
    match_optional(BString, mountpoint_split.match,
        some(token)
        {
            mountpoint = token;
            remaining = mountpoint_split.rest;
        },
        none()
        {
            return Error(HumpenMountCommand, humpen_invalid_mountpoint, HUMPEN_PARSE_ERROR);
        }
    );

    BStringSplit extra_split = BString__split_first_token(&remaining, &humpen_space_delimiters);
    match_optional(BString, extra_split.match,
        some(extra)
        {
            return Error(HumpenMountCommand, humpen_unexpected_fstab_token, HUMPEN_PARSE_ERROR);
        },
        none_do_nothing()
    );

    return Success(HumpenMountCommand, HumpenMountCommand__new(&device_path, &filesystem_type, &mountpoint));
}

static Result(SpawnResult) Humpen__spawn_watchdog_line(ConstBStringPtr line)
{
    BString command_line = Humpen__line_without_comment(line);
    BString remaining = BString__strip(&command_line);
    if (BString__is_empty(&remaining))
    {
        return Success(SpawnResult, 0);
    }

    match_result(HumpenMountCommand, Humpen__parse_mount_command(&remaining),
        success(command)
        {
            IO__printf(
                &bstr("humpen: mounting %B as %B on %B\n"),
                &command.device_path,
                &command.filesystem_type,
                &command.mountpoint
            );
            return OS__spawn(
                P_NOWAIT,
                command.driver_path,
                &command.arg0,
                &command.arg1,
                &command.arg2
            );
        },
        error(code, message)
        {
            return Error(SpawnResult, message, code);
        }
    );

    return Error(SpawnResult, humpen_invalid_fstab_line, HUMPEN_PARSE_ERROR);
}

static Result(MountResult) Humpen__mount_fstab(ConstDynStringPtr fstab)
{
    match_optional(DynStringLines, DynString__lines(fstab),
        some(lines)
        {
            for_each_ll(DynStringLines, lines, line,
            {
                match_optional(BString, DynString__bstring(&line),
                    some(bstring_line)
                    {
                        match_result(SpawnResult, Humpen__spawn_watchdog_line(&bstring_line),
                            success_do_nothing(),
                            error(code, message)
                            {
                                DynStringLines__dispose_all(&lines);
                                return Error(MountResult, message, code);
                            }
                        );
                    },
                    none()
                    {
                        DynStringLines__dispose_all(&lines);
                        return Error(MountResult, bstr("fstab line too long"), HUMPEN_FSTAB_ERROR);
                    }
                );
            });
            DynStringLines__dispose_all(&lines);
            return Success(MountResult, 0);
        },
        none()
        {
            return Error(MountResult, bstr("failed to split fstab lines"), HUMPEN_FSTAB_ERROR);
        }
    );

    return Error(MountResult, bstr("failed to mount fstab"), HUMPEN_FSTAB_ERROR);
}

int HUMPEN_ENTRYPOINT(void)
{
    int result = 1;

    match_optional(DynString, Humpen__load_fstab(),
        some(fstab)
        {
            match_result(MountResult, Humpen__mount_fstab(&fstab),
                success(spawn_result)
                {
                    result = (int) spawn_result;
                },
                error(code, message)
                {
                    IO__printf(&bstr("humpen: %B (%i)\n"), &message, (int) code);
                    result = 1;
                }
            );
            DynString__dispose(&fstab);
        },
        none() { IO__printf(&bstr("humpen: no fstab found\n")); }
    );

    return result;
}
