#include "boot/services.h"
#include "io/mount_client.h"
#include "sys/boot_info.h"
#include "sys/process.h"
#include "sys/service.h"
#include "sys/syslog.h"

typedef volatile uint32_t * MamaRegisterPtr;

typedef struct MamaBootDisk
{
    uint64_t slot;
    BString path;
} MamaBootDisk;

ResultDef(MamaBootDisk);

static MamaBootDisk MamaBootDisk__new(uint64_t slot, ConstBStringPtr path)
{
    return (MamaBootDisk){slot, *path};
}

static Result(MamaBootDisk) MamaBootDisk__find(ConstBootVirtioMmioInfoPtr virtio_mmio)
{
    ServiceMappedMemoryPtr mapping;
    ServiceCode status =
        service_map_mmio(virtio_mmio->physical_base, virtio_mmio->slot_count * virtio_mmio->slot_size, &mapping);
    if (!service_ok(status))
    {
        return Error(MamaBootDisk, bstr("Cannot probe boot disk"), status);
    }
    MamaBootDisk found = MamaBootDisk__new(0, &empty_bstr());
    uint64_t matches = 0;
    uint64_t ordinal = 0;
    for (uint64_t remaining = virtio_mmio->slot_count; remaining > 0; remaining--)
    {
        uint64_t slot = remaining - 1;
        MamaRegisterPtr registers = (MamaRegisterPtr)driver_ptr_offset(mapping, slot * virtio_mmio->slot_size);
        if (registers[0] != 0x74726976U || registers[1] < 1 || registers[2] != 2)
        {
            continue;
        }
        ordinal++;
        uint64_t capacity = registers[0x100 / 4] | ((uint64_t)registers[0x104 / 4] << 32);
        // Retain the existing boot-image selection rule, rejecting ambiguity.
        if (capacity == 8 * 1024 * 1024 / 512)
        {
            BString path = BString__sprintf(&bstr("/dev/virtio%llu"), ordinal);
            found = MamaBootDisk__new(slot, &path);
            matches++;
        }
    }
    return matches == 1 ? Success(MamaBootDisk, found)
                        : Error(MamaBootDisk, bstr("Expected one 8 MiB boot configuration disk"),
                                SERVICE_DEVICE_REGISTRATION_FAILED);
}

static Result(ServiceCode) Mama__start_services(void)
{
    match_result(
        BootService__bootstrap(&bstr("/sbin/services/hundekanal-warter"), &empty_bstr(), &empty_bstr(),
                               &bstr("hundekanal-warter")),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ServiceCode, message, code);
        }
    );
    match_result(
        BootService__bootstrap(&bstr("/sbin/services/hunde-puffer"), &empty_bstr(), &empty_bstr(),
                               &bstr("hunde-puffer")),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ServiceCode, message, code);
        }
    );
    match_result(
        BootService__bootstrap(&bstr("/sbin/services/hund-unterbrict"), &empty_bstr(), &empty_bstr(),
                               &bstr("hund-unterbrict")),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ServiceCode, message, code);
        }
    );
    match_result(
        BootService__bootstrap(&bstr("/sbin/services/hund-dateisystem"), &empty_bstr(), &empty_bstr(),
                               &bstr(IO_FILESYSTEM_SERVICE)),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ServiceCode, message, code);
        }
    );
    return BootService__bootstrap(&bstr("/sbin/services/hund-io-system"), &empty_bstr(), &empty_bstr(),
                                  &bstr(IO_SYSTEM_SERVICE));
}

static Result(IOStatus) Mama__mount(ChannelPtr mounts, ConstBStringPtr type, ConstBStringPtr mountpoint)
{
    return MountClient__add(mounts, MountConfig__new(&empty_bstr(), type, mountpoint));
}

static Result(IOStatus) Mama__boot_mounts(ChannelPtr mounts)
{
    match_result(
        BootService__bootstrap(&bstr("/sbin/drivers/embedfs"), &empty_bstr(), &empty_bstr(), &bstr("/sbin")),
        success_do_nothing(),
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    match_result(
        Mama__mount(mounts, &bstr("embedfs"), &bstr("/sbin")),
        success_do_nothing(),
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    match_result(
        BootService__bootstrap(&bstr("/sbin/drivers/sysinfofs"), &empty_bstr(), &empty_bstr(), &bstr("/boot")),
        success_do_nothing(),
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    match_result(
        Mama__mount(mounts, &bstr("sysinfofs"), &bstr("/boot")),
        success_do_nothing(),
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    BootVirtioMmioInfo virtio_mmio;
    match_result(
        BootHardwareInfo__load(),
        success(info)
        {
            match_optional(
                info.virtio_mmio,
                some(value)
                {
                    virtio_mmio = value;
                },
                none()
                {
                    return Error(IOStatus, bstr("VirtIO MMIO information unavailable"),
                                 SERVICE_DEVICE_REGISTRATION_FAILED);
                }
            );
        },
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    MamaBootDisk disk;
    match_result(
        MamaBootDisk__find(&virtio_mmio),
        success(value)
        {
            disk = value;
        },
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    if (!service_ok(service_wait_ready(&disk.path)))
    {
        return Error(IOStatus, bstr("Hosted boot block device unavailable"), SERVICE_DEVICE_REGISTRATION_FAILED);
    }
    match_result(
        BootService__bootstrap(&bstr("/sbin/drivers/ext2fs"), &disk.path, &bstr("/boot/config"), &bstr("/boot/config")),
        success_do_nothing(),
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    return MountClient__add(mounts, MountConfig__new(&disk.path, &bstr("ext2"), &bstr("/boot/config")));
}

int main(void)
{
    if (!service_ok(service_become_service()) || !service_ok(service_publish_ready(&bstr("hunde-mama"))))
    {
        return 1;
    }
    match_result(
        Mama__start_services(),
        success_do_nothing(),
        error(code, message)
        {
            kernel_syslog_tagf("hunde-mama", "%B (%i)", &message, (int)code);
            return 1;
        }
    );
    Channel mounts;
    match_result(
        Channel__open(ServiceDirectory__shared(), &bstr(IO_FILESYSTEM_SERVICE)),
        success(channel)
        {
            mounts = channel;
        },
        error(code, message)
        {
            return 1;
        }
    );
    IOStatus status = IO_OK;
    match_result(
        Mama__boot_mounts(&mounts),
        success_do_nothing(),
        error(code, message)
        {
            kernel_syslog_tagf("hunde-mama", "%B (%i)", &message, (int)code);
            status = code;
        }
    );
    Channel__close(&mounts);
    if (status != IO_OK)
    {
        return 1;
    }
    kernel_syslog_tagf("hunde-mama", "bootstrap mounts ready: /sbin /boot /boot/config");
    match_result(
        OS__boot_complete(),
        success_do_nothing(),
        error(code, message)
        {
            kernel_syslog_tagf("hunde-mama", "bootstrap handoff failed: %B", &message);
            return 1;
        }
    );
    kernel_syslog_tagf("hunde-mama", "boot level 0 complete");
    match_result(
        OS__spawn(P_NOWAIT, &bstr("/sbin/services/hundeschule"), &SPAWN_NO_ARG, &SPAWN_NO_ARG, &SPAWN_NO_ARG),
        success_do_nothing(),
        error(code, message)
        {
            return 1;
        }
    );
    while (true)
    {
        ProcessChildEvent event = ProcessChildEvent__new();
        if (wait_for_child_event(&event) < 0)
        {
            return 1;
        }
        kernel_syslog_tagf("hunde-mama", "bootstrap child exited: pid=%llu", event.pid);
    }
}
