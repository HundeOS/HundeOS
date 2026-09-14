#include "mount_config.h"

MountConfig MountConfig__new(ConstBStringPtr device_path, ConstBStringPtr filesystem_type, ConstBStringPtr mountpoint)
{
    return (MountConfig){.device_path = *device_path, .filesystem_type = *filesystem_type, .mountpoint = *mountpoint};
}

BString MountConfig__without_comment(ConstBStringPtr line)
{
    BStringSplit split = BString__split_first_any(line, &bstr("#"));
    return unwrap_optional(split.match, *line);
}

static Optional(BString) MountConfig__take_token(BStringPtr remaining)
{
    BStringSplit split = BString__split_first_token(remaining, &bstr(" \t\r\n"));
    *remaining = split.rest;
    return split.match;
}

Result(MountConfig) MountConfig__parse(ConstBStringPtr line)
{
    BString remaining = MountConfig__without_comment(line);
    MountConfig config;
    match_optional(
        MountConfig__take_token(&remaining),
        some(command)
        {
            if (!BString__equals(&command, &bstr("device")))
            {
                return Error(MountConfig, bstr("invalid fstab command"), 1);
            }
        },
        none() { return Error(MountConfig, bstr("missing fstab command"), 1); }
    );
    match_optional(
        MountConfig__take_token(&remaining),
        some(value) { config.device_path = value; },
        none() { return Error(MountConfig, bstr("missing fstab device"), 1); }
    );
    match_optional(
        MountConfig__take_token(&remaining),
        some(value) { config.filesystem_type = value; },
        none() { return Error(MountConfig, bstr("missing fstab filesystem"), 1); }
    );
    match_optional(
        MountConfig__take_token(&remaining),
        some(value) { config.mountpoint = value; },
        none() { return Error(MountConfig, bstr("missing fstab mountpoint"), 1); }
    );
    match_optional(
        MountConfig__take_token(&remaining),
        some(value) { return Error(MountConfig, bstr("unexpected fstab token"), 1); },
        none_do_nothing()
    );
    return Success(MountConfig, config);
}
