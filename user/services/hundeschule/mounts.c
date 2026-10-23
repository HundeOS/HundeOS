#include "types/linked_list.h"
#include "mounts.h"
#include "boot/services.h"
#include "sys/io.h"
#include "sys/service.h"

static bool MountConfig__conflicts(MountConfigPtr self, void * context)
{
    MountConfigPtr candidate = context;
    return BString__equals(&self->device_path, &candidate->device_path) ||
           BString__equals(&self->mountpoint, &candidate->mountpoint);
}

static bool MountConfig__same_destination(void * self, void * context)
{
    return MountConfig__conflicts(self, context);
}

OptionalDef(MountConfig);
LinkedListDefine(MountConfig, ConfiguredMounts, MountConfig__same_destination);
typedef ConfiguredMounts * ConfiguredMountsPtr;
LinkedListBodyDefine(ConfiguredMounts);

static bool MountConfig__is_root(MountConfigPtr self, void * context)
{
    return BString__equals(&self->mountpoint, &bstr("/"));
}

static Result(IOStatus) ConfiguredMounts__add(ConfiguredMountsPtr self, MountConfig config)
{
    match_find_result(
        ConfiguredMounts__find(self, MountConfig__conflicts, &config),
        some(conflict)
        {
            return Error(IOStatus, bstr("Duplicate mount device or mountpoint"), IO_INVALID_MOUNT_POINT);
        },
        none()
        {
            match_result(
                ConfiguredMounts__push(self, config),
                success(code)
                {
                    return Success(IOStatus, IO_OK);
                },
                error(code, message)
                {
                    return Error(IOStatus, message, IO_PANIC);
                }
            );
        }
    );
    return Error(IOStatus, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

static Result(IOStatus) Hundeschule__mount_config(ChannelPtr channel, MountConfig config)
{
    if (BString__equals(&config.filesystem_type, &bstr("fat32")))
    {
        config.filesystem_type = bstr("fat32.wuff");
    }
    if (BString__equals(&config.filesystem_type, &bstr("ext2.wuff")) ||
        BString__equals(&config.filesystem_type, &bstr("fat32.wuff")))
    {
        ServiceCode device_ready = service_wait_ready(&config.device_path);
        if (!service_ok(device_ready))
        {
            return Error(IOStatus, bstr("Block device is not ready"), device_ready);
        }
        match_result(
            MountClient__start_hosted(config),
            success_do_nothing(),
            error(code, message)
            {
                return Error(IOStatus, message, code);
            }
        );
        return MountClient__add(channel, config);
    }
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

static Result(IOStatus) Hundeschule__mount_entries(ChannelPtr channel, ConfiguredMountsPtr mounts)
{
    match_find_result(
        ConfiguredMounts__find(mounts, MountConfig__is_root, mounts),
        some(config)
        {
            match_result(
                Hundeschule__mount_config(channel, config),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(IOStatus, message, code);
                }
            );
            for_each_ll(
                ConfiguredMounts,
                *mounts,
                mount,
                {
                    if (MountConfig__is_root(&mount, mounts))
                    {
                        continue;
                    }
                    match_result(
                        Hundeschule__mount_config(channel, mount),
                        success_do_nothing(),
                        error(code, message)
                        {
                            return Error(IOStatus, message, code);
                        }
                    );
                }
            );
            return Success(IOStatus, IO_OK);
        },
        none()
        {
            return Error(IOStatus, bstr("Missing root mount"), IO_INVALID_MOUNT_POINT);
        }
    );
    return Error(IOStatus, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

static Result(IOStatus) Hundeschule__mount_configured_file(ChannelPtr channel, IOHandle file)
{
    IOStatus status = IO_OK;
    ConfiguredMounts mounts = ConfiguredMounts__new();
    match_result(
        IO__read_dyn_string(file, 2048),
        success(config)
        {
            IO__close(file);
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
                                                !BString__equals(&mount.filesystem_type, &bstr("fat32")) &&
                                                !BString__equals(&mount.filesystem_type, &bstr("ext2.wuff")) &&
                                                !BString__equals(&mount.filesystem_type, &bstr("fat32.wuff")))
                                            {
                                                continue;
                                            }
                                            match_result(
                                                ConfiguredMounts__add(&mounts, mount),
                                                success_do_nothing(),
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
        error(code, message)
        {
            IO__close(file);
            return Error(IOStatus, message, code);
        }
    );
    if (status != IO_OK)
    {
        ConfiguredMounts__dispose(&mounts);
        return Error(IOStatus, bstr("Cannot establish configured mounts"), status);
    }
    Result(IOStatus) result = Hundeschule__mount_entries(channel, &mounts);
    ConfiguredMounts__dispose(&mounts);
    return result;
}

static Result(IOStatus) Hundeschule__mount_configured(ChannelPtr channel)
{
    match_result(
        IO__open(&bstr("/boot/config/fstab"), IO_OPEN_READ),
        success(file)
        {
            return Hundeschule__mount_configured_file(channel, file);
        },
        error(code, message)
        {
            return Error(IOStatus, bstr("Cannot open fstab"), code);
        }
    );
    return Error(IOStatus, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

static Result(IOStatus) Hundeschule__mount_builtin(ChannelPtr channel, ConstBStringPtr type, ConstBStringPtr path)
{
    if (BString__equals(type, &bstr("memfs")) || BString__equals(type, &bstr("procfs")))
    {
        ServiceCode status = service_wait_ready(path);
        if (!service_ok(status))
        {
            return Error(IOStatus, bstr("Hosted filesystem is not ready"), status);
        }
        return MountClient__add(channel, MountConfig__new(&empty_bstr(), type, path));
    }
    BString driver = BString__sprintf(&bstr("/sbin/drivers/%s"), cstr_p(type));
    match_result(
        BootService__start(&driver, &empty_bstr(), &empty_bstr(), path),
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
    match_result(
        Channel__open(ServiceDirectory__shared(), &bstr(IO_FILESYSTEM_SERVICE)),
        success(channel)
        {
            Result(IOStatus) result = Hundeschule__mount_all(&channel);
            Channel__close(&channel);
            return result;
        },
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    return Error(IOStatus, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}
