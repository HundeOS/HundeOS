#include "namespace.h"
#include "sys/io.h"
#include "sys/service.h"
#include "sys/syslog.h"
#include "sys/spawn.h"

static Result(IOStatus) FilesystemNamespace__load_boot_mounts(FilesystemNamespacePtr self)
{
    IOHandle config = open(&bstr("/boot/config/fstab"), IO_OPEN_READ);
    if (config < 0)
    {
        return Error(IOStatus, bstr("Cannot open boot fstab"), IO_INVALID_PATH);
    }
    Optional(DynString) loaded = IO__read_dyn_string(config, 2048);
    close(config);
    match_optional(
        loaded,
        some(text)
        {
            IOStatus status = IO_OK;
            match_optional(
                DynString__lines(&text),
                some(lines)
                {
                    for_each_ll(DynStringLines, lines, line, {
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
                                        if (!BString__equals(&mount.filesystem_type, &bstr("fat32")) &&
                                            !BString__equals(&mount.filesystem_type, &bstr("ext2")))
                                        {
                                            continue;
                                        }
                                        match_result(
                                            FilesystemNamespace__add(self, mount),
                                            success_do_nothing(),
                                            error(code, message) { status = (IOStatus)code; }
                                        );
                                    },
                                    error(code, message) { status = IO_INVALID_MOUNT_POINT; }
                                );
                            },
                            none() { status = IO_INVALID_MOUNT_POINT; }
                        );
                        if (status != IO_OK)
                        {
                            break;
                        }
                    });
                    DynStringLines__dispose_all(&lines);
                },
                none() { status = IO_INVALID_PATH; }
            );
            DynString__dispose(&text);
            return status == IO_OK ? Success(IOStatus, IO_OK) : Error(IOStatus, bstr("Invalid boot mounts"), status);
        },
        none() { return Error(IOStatus, bstr("Cannot read boot fstab"), IO_INVALID_PATH); }
    );
}

static Result(IOStatus) FilesystemNamespace__start_root(FilesystemNamespacePtr self)
{
    match_find_result(
        FilesystemMounts__find(&self->mounts, FilesystemMount__matches_mountpoint, &bstr("/")),
        some(mount)
        {
            match_result(
                OS__spawn(P_NOWAIT, &bstr("/sbin/services/humpen-wachhund"), &Some(BString, mount.config.device_path),
                          &Some(BString, mount.config.filesystem_type), &Some(BString, mount.config.mountpoint)),
                success(pid)
                {
                    if (!service_ok(service_wait_ready(&mount.config.mountpoint)))
                    {
                        return Error(IOStatus, bstr("Root filesystem did not become ready"), IO_NO_DEVICE_FOUND);
                    }
                    kernel_syslog_tagf(IO_FILESYSTEM_SERVICE, "mounted %B as %B on /", &mount.config.device_path,
                                       &mount.config.filesystem_type);
                    return Success(IOStatus, IO_OK);
                },
                error(code, message) { return Error(IOStatus, message, IO_NO_DEVICE_FOUND); }
            );
        },
        none() { return Error(IOStatus, bstr("No root filesystem configured"), IO_INVALID_MOUNT_POINT); }
    );
}

static Result(IOStatus) FilesystemNamespace__start_tmp(FilesystemNamespacePtr self)
{
    MountConfig mount = MountConfig__new(&empty_bstr(), &bstr("memfs"), &bstr("/tmp"));
    match_result(
        FilesystemNamespace__add(self, mount),
        success_do_nothing(),
        error(code, message) { return Error(IOStatus, message, code); }
    );
    match_result(
        OS__spawn(P_NOWAIT, &bstr("/sbin/drivers/memfs"), &Some(BString, mount.mountpoint), &SPAWN_NO_ARG,
                  &SPAWN_NO_ARG),
        success(pid)
        {
            if (!service_ok(service_wait_ready(&mount.mountpoint)))
            {
                return Error(IOStatus, bstr("Temporary filesystem did not become ready"), IO_NO_DEVICE_FOUND);
            }
            kernel_syslog_tagf(IO_FILESYSTEM_SERVICE, "mounted memfs on /tmp");
            return Success(IOStatus, IO_OK);
        },
        error(code, message) { return Error(IOStatus, message, IO_NO_DEVICE_FOUND); }
    );
}

static Result(IOStatus) FilesystemNamespace__start_dev(FilesystemNamespacePtr self)
{
    MountConfig mount = MountConfig__new(&empty_bstr(), &bstr("devfs"), &bstr("/dev"));
    match_result(
        FilesystemNamespace__add(self, mount),
        success_do_nothing(),
        error(code, message) { return Error(IOStatus, message, code); }
    );
    match_result(
        OS__spawn(P_NOWAIT, &bstr("/sbin/drivers/devfs"), &SPAWN_NO_ARG, &SPAWN_NO_ARG, &SPAWN_NO_ARG),
        success(pid)
        {
            if (!service_ok(service_wait_ready(&mount.mountpoint)))
            {
                return Error(IOStatus, bstr("Device filesystem did not become ready"), IO_NO_DEVICE_FOUND);
            }
            kernel_syslog_tagf(IO_FILESYSTEM_SERVICE, "mounted devfs on /dev");
            return Success(IOStatus, IO_OK);
        },
        error(code, message) { return Error(IOStatus, message, IO_NO_DEVICE_FOUND); }
    );
}

static Result(IOStatus) FilesystemNamespace__start_proc(FilesystemNamespacePtr self)
{
    MountConfig mount = MountConfig__new(&empty_bstr(), &bstr("procfs"), &bstr("/proc"));
    match_result(
        FilesystemNamespace__add(self, mount),
        success_do_nothing(),
        error(code, message) { return Error(IOStatus, message, code); }
    );
    match_result(
        OS__spawn(P_NOWAIT, &bstr("/sbin/drivers/procfs"), &SPAWN_NO_ARG, &SPAWN_NO_ARG, &SPAWN_NO_ARG),
        success(pid)
        {
            if (!service_ok(service_wait_ready(&mount.mountpoint)))
            {
                return Error(IOStatus, bstr("Process filesystem did not become ready"), IO_NO_DEVICE_FOUND);
            }
            kernel_syslog_tagf(IO_FILESYSTEM_SERVICE, "mounted procfs on /proc");
            return Success(IOStatus, IO_OK);
        },
        error(code, message) { return Error(IOStatus, message, IO_NO_DEVICE_FOUND); }
    );
}

static Result(IOStatus) FilesystemNamespace__start_sbin(FilesystemNamespacePtr self)
{
    MountConfig mount = MountConfig__new(&empty_bstr(), &bstr("embedfs"), &bstr("/sbin"));
    match_result(
        FilesystemNamespace__add(self, mount),
        success_do_nothing(),
        error(code, message) { return Error(IOStatus, message, code); }
    );
    match_result(
        OS__spawn(P_NOWAIT, &bstr("/sbin/drivers/embedfs"), &SPAWN_NO_ARG, &SPAWN_NO_ARG, &SPAWN_NO_ARG),
        success(pid)
        {
            if (!service_ok(service_wait_ready(&mount.mountpoint)))
            {
                return Error(IOStatus, bstr("Embedded filesystem did not become ready"), IO_NO_DEVICE_FOUND);
            }
            kernel_syslog_tagf(IO_FILESYSTEM_SERVICE, "mounted embedfs on /sbin");
            return Success(IOStatus, IO_OK);
        },
        error(code, message) { return Error(IOStatus, message, IO_NO_DEVICE_FOUND); }
    );
}

int main(void)
{
    static FilesystemNamespace namespace;
    static IOService service;
    namespace = FilesystemNamespace__new();
    if (!service_ok(service_become_service()))
    {
        return 1;
    }
    match_result(
        FilesystemNamespace__load_boot_mounts(&namespace),
        success_do_nothing(),
        error(code, message)
        {
            kernel_syslog_tagf(IO_FILESYSTEM_SERVICE, "%B (%i)", &message, (int)code);
            return 1;
        }
    );
    match_result(
        IOService__listen(&bstr(IO_FILESYSTEM_SERVICE), &namespace, FilesystemNamespace__execute,
                          FilesystemNamespace__disconnect),
        success(created) { service = created; },
        error(code, message) { return 1; }
    );
    match_result(
        FilesystemNamespace__start_root(&namespace),
        success_do_nothing(),
        error(code, message)
        {
            kernel_syslog_tagf(IO_FILESYSTEM_SERVICE, "%B (%i)", &message, (int)code);
            return 1;
        }
    );
    match_result(
        FilesystemNamespace__start_tmp(&namespace),
        success_do_nothing(),
        error(code, message)
        {
            kernel_syslog_tagf(IO_FILESYSTEM_SERVICE, "%B (%i)", &message, (int)code);
            return 1;
        }
    );
    match_result(
        FilesystemNamespace__start_dev(&namespace),
        success_do_nothing(),
        error(code, message)
        {
            kernel_syslog_tagf(IO_FILESYSTEM_SERVICE, "%B (%i)", &message, (int)code);
            return 1;
        }
    );
    match_result(
        FilesystemNamespace__start_proc(&namespace),
        success_do_nothing(),
        error(code, message)
        {
            kernel_syslog_tagf(IO_FILESYSTEM_SERVICE, "%B (%i)", &message, (int)code);
            return 1;
        }
    );
    match_result(
        FilesystemNamespace__start_sbin(&namespace),
        success_do_nothing(),
        error(code, message)
        {
            kernel_syslog_tagf(IO_FILESYSTEM_SERVICE, "%B (%i)", &message, (int)code);
            return 1;
        }
    );
    if (!service_ok(service_publish_ready(&bstr(IO_FILESYSTEM_SERVICE))))
    {
        return 1;
    }
    Channel__prepare_wait();
    while (true)
    {
        if (!IOService__poll(&service))
        {
            Channel__wait_event();
        }
    }
}
