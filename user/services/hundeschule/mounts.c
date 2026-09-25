#include "mounts.h"
#include "boot/services.h"
#include "sys/io.h"
#include "sys/service.h"

static Result(IOStatus) Hundeschule__mount_config(ChannelPtr channel, MountConfig config)
{
    match_result(
        MountClient__add(channel, config),
        success_do_nothing(),
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    if (!BString__equals(&config.mountpoint, &bstr("/")))
    {
        return Success(IOStatus, IO_OK);
    }
    match_result(
        OS__spawn(P_NOWAIT, &bstr("/sbin/services/humpen-wachhund"), &Some(BString, config.device_path),
                  &Some(BString, config.filesystem_type), &Some(BString, config.mountpoint)),
        success(pid)
        {
            ServiceCode status = service_wait_ready(&config.mountpoint);
            return service_ok(status) ? Success(IOStatus, IO_OK)
                                      : Error(IOStatus, bstr("Root filesystem not ready"), status);
        },
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
}

static Result(IOStatus) Hundeschule__mount_configured(ChannelPtr channel)
{
    IOHandle file = IO__open(&bstr("/boot/config/fstab"), IO_OPEN_READ);
    if (file < 0)
    {
        return Error(IOStatus, bstr("Cannot open fstab"), file);
    }
    Optional(DynString) text = IO__read_dyn_string(file, 2048);
    IO__close(file);
    IOStatus status = IO_OK;
    bool root = false;
    match_optional(
        text,
        some(config)
        {
            match_optional(
                DynString__lines(&config),
                some(lines)
                {
                    for_each_ll(
                        DynStringLines,
                        lines,
                        line,
                        {
                            match_optional(
                                DynString__bstring(&line),
                                some(input)
                                {
                                    BString content = MountConfig__without_comment(&input);
                                    content = BString__strip(&content);
                                    if (BString__is_empty(&content))
                                    {
                                        continue;
                                    }
                                    match_result(
                                        MountConfig__parse(&content),
                                        success(mount)
                                        {
                                            if (!BString__equals(&mount.filesystem_type, &bstr("ext2")) &&
                                                !BString__equals(&mount.filesystem_type, &bstr("fat32")))
                                            {
                                                continue;
                                            }
                                            match_result(
                                                Hundeschule__mount_config(channel, mount),
                                                success(code)
                                                {
                                                    root =
                                                        root || BString__equals(&mount.mountpoint, &bstr("/"));
                                                },
                                                error(code, message)
                                                {
                                                    status = code;
                                                }
                                            );
                                        },
                                        error(code, message)
                                        {
                                            status = IO_INVALID_MOUNT_POINT;
                                        }
                                    );
                                },
                                none()
                                {
                                    status = IO_INVALID_PATH;
                                }
                            );
                            if (status != IO_OK)
                            {
                                break;
                            }
                        }
                    );
                    DynStringLines__dispose_all(&lines);
                },
                none()
                {
                    status = IO_INVALID_PATH;
                }
            );
            DynString__dispose(&config);
        },
        none()
        {
            status = IO_INVALID_PATH;
        }
    );
    if (status != IO_OK || !root)
    {
        return Error(IOStatus, bstr("Cannot establish configured mounts"),
                     status != IO_OK ? status : IO_INVALID_MOUNT_POINT);
    }
    return Success(IOStatus, IO_OK);
}

static Result(IOStatus) Hundeschule__mount_builtin(ChannelPtr channel, ConstBStringPtr type, ConstBStringPtr path)
{
    BString driver = BString__sprintf(&bstr("/sbin/drivers/%s"), cstr_p(type));
    match_result(
        BootService__start(&driver, BString__equals(type, &bstr("memfs")) ? path : &empty_bstr(), &empty_bstr(), path),
        success_do_nothing(),
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    return MountClient__add(channel, MountConfig__new(&empty_bstr(), type, path));
}

static Result(IOStatus) Hundeschule__mount_all(ChannelPtr channel)
{
    match_result(
        Hundeschule__mount_configured(channel),
        success_do_nothing(),
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    match_result(
        Hundeschule__mount_builtin(channel, &bstr("memfs"), &bstr("/tmp")),
        success_do_nothing(),
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    match_result(
        Hundeschule__mount_builtin(channel, &bstr("devfs"), &bstr("/dev")),
        success_do_nothing(),
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    return Hundeschule__mount_builtin(channel, &bstr("procfs"), &bstr("/proc"));
}

Result(IOStatus) Hundeschule__mount_filesystems(void)
{
    Channel channel;
    match_result(
        Channel__open(ServiceDirectory__shared(), &bstr(IO_FILESYSTEM_SERVICE)),
        success(created)
        {
            channel = created;
        },
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    Result(IOStatus) result = Hundeschule__mount_all(&channel);
    Channel__close(&channel);
    return result;
}
