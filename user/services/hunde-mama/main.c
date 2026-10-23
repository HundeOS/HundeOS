#include "boot/services.h"
#include "io/mount_client.h"
#include "sys/boot_info.h"
#include "sys/process.h"
#include "sys/service.h"
#include "sys/syslog.h"

typedef volatile uint32_t * MamaRegisterPtr;

static BString mama_sd_device = bstr("/dev/mmc0");
static BString mama_sd_boot_config = bstr("/dev/mmc0p2");
static BString mama_sd_root = bstr("/dev/mmc0p3");
static BString mama_partition_two = bstr("2");
static BString mama_partition_three = bstr("3");

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

static Result(MamaBootDisk) MamaBootDisk__start_sd(void)
{
    match_result(
        BootService__bootstrap(&bstr("/sbin/drivers/sdhci_channelio"), &empty_bstr(), &empty_bstr(), &mama_sd_device),
        success_do_nothing(),
        error(code, message)
        {
            return Error(MamaBootDisk, message, code);
        }
    );
    match_result(
        BootService__bootstrap(&bstr("/sbin/drivers/block_partition"), &mama_sd_device, &mama_partition_two,
                               &mama_sd_boot_config),
        success_do_nothing(),
        error(code, message)
        {
            return Error(MamaBootDisk, message, code);
        }
    );
    match_result(
        BootService__bootstrap(&bstr("/sbin/drivers/block_partition"), &mama_sd_device, &mama_partition_three,
                               &mama_sd_root),
        success_do_nothing(),
        error(code, message)
        {
            return Error(MamaBootDisk, message, code);
        }
    );
    return Success(MamaBootDisk, MamaBootDisk__new(0, &mama_sd_boot_config));
}

static Result(MamaBootDisk) MamaBootDisk__from_hardware(ConstBootHardwareInfoPtr info)
{
    match_optional(
        info->virtio_mmio,
        some(virtio_mmio)
        {
            return MamaBootDisk__find(&virtio_mmio);
        },
        none_do_nothing()
    );
    match_optional(info->sd_host,
        some(sd_host)
        {
            return MamaBootDisk__start_sd();
        },
        none_do_nothing()
    );
    return Error(MamaBootDisk, bstr("No boot block device available"), SERVICE_DEVICE_REGISTRATION_FAILED);
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
    return BootService__bootstrap(&bstr("/sbin/services/hunde-io-host"), &empty_bstr(), &empty_bstr(),
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
    ServiceCode boot_ready = service_wait_ready(&bstr("/boot"));
    if (!service_ok(boot_ready))
    {
        return Error(IOStatus, bstr("Hosted boot information unavailable"), boot_ready);
    }
    match_result(
        Mama__mount(mounts, &bstr("sysinfofs"), &bstr("/boot")),
        success_do_nothing(),
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    match_result(
        BootHardwareInfo__load(),
        success(hardware)
        {
            match_result(
                MamaBootDisk__from_hardware(&hardware),
                success(disk)
                {
                    if (!service_ok(service_wait_ready(&disk.path)))
                    {
                        return Error(IOStatus, bstr("Hosted boot block device unavailable"),
                                     SERVICE_DEVICE_REGISTRATION_FAILED);
                    }
                    MountConfig config = MountConfig__new(&disk.path, &bstr("ext2.wuff"), &bstr("/boot/config"));
                    match_result(
                        MountClient__start_hosted(config),
                        success_do_nothing(),
                        error(code, message)
                        {
                            return Error(IOStatus, message, code);
                        }
                    );
                    return MountClient__add(mounts, config);
                },
                error(code, message)
                {
                    return Error(IOStatus, message, code);
                }
            );
            return Error(IOStatus, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
        },
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    return Error(IOStatus, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

static Result(IOStatus) Mama__retire_archive(ChannelPtr channel)
{
    uint64_t peer = Channel__server_pid(channel);
    if (peer == 0 || peer > INT64_MAX)
    {
        return Error(IOStatus, bstr("Invalid bootstrap archive server"), IO_UNKNOWN_PID);
    }
    IORequest request = IORequest__new(IO_REQUEST_BOOTSTRAP_RETIRE, 0);
    IOReply reply;
    match_result(
        Channel__submit(channel, (ConstChannelDataPtr)&request, sizeof(request), (ChannelDataPtr)&reply, sizeof(reply)),
        success(size)
        {
            if (size != sizeof(reply) || reply.status != IO_OK)
            {
                return Error(IOStatus, bstr("Bootstrap archive drain failed"),
                             size == sizeof(reply) ? reply.status : IO_UNKNOWN_BUFFER);
            }
            ProcessSignalResult status = kill_process(peer, PROCESS_SIGNAL_TERMINATE);
            if (status != PROCESS_SIGNAL_OK)
            {
                return Error(IOStatus, bstr("Cannot retire bootstrap archive"), status);
            }
            return Success(IOStatus, IO_OK);
        },
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    return Error(IOStatus, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

static Result(IOStatus) Mama__handoff_archive(void)
{
    match_result(
        MountClient__start_hosted(MountConfig__new(&empty_bstr(), &bstr("embedfs"), &bstr("/sbin"))),
        success_do_nothing(),
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    match_result(
        Channel__open(ServiceDirectory__shared(), &bstr("/sbin")),
        success(channel)
        {
            Result(IOStatus) result = Mama__retire_archive(&channel);
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
        Mama__handoff_archive(),
        success_do_nothing(),
        error(code, message)
        {
            kernel_syslog_tagf("hunde-mama", "%B (%i)", &message, (int)code);
            return 1;
        }
    );
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
