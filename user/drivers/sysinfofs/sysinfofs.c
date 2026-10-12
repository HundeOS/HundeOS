#include "sysinfofs.h"
#include "memory.h"
#include "sys/boot_info.h"

typedef struct SysinfoWriter
{
    FilesystemFileDataPtr data;
    FilesystemFileSize size;
    bool failed;
} SysinfoWriter;

typedef SysinfoWriter * SysinfoWriterPtr;

static SysinfoWriter SysinfoWriter__new(FilesystemFileDataPtr data)
{
    return (SysinfoWriter){.data = data};
}

static void SysinfoWriter__line(SysinfoWriterPtr self, ConstBStringPtr key, uint64_t value)
{
    BString line = BString__sprintf(&bstr("%s=%llu\n"), cstr_p(key), value);
    StringPos size = BString__length(&line);
    if (self->size + size > FILESYSTEM_FILE_CAPACITY)
    {
        self->failed = true;
        return;
    }
    Mem__memcpy(self->data + self->size, BString__cstr(&line), size);
    self->size += size;
}

Result(FilesystemFileSize) Sysinfofs__render(BootHardwareInfoPtr info, FilesystemFileDataPtr data)
{
    SysinfoWriter writer = SysinfoWriter__new(data);
    SysinfoWriter__line(&writer, &bstr("cpu.count"), info->cpu_count);
    SysinfoWriter__line(&writer, &bstr("cpu.online_count"), info->online_cpu_count);
    SysinfoWriter__line(&writer, &bstr("uart.physical_base"), info->uart.physical_base);
    SysinfoWriter__line(&writer, &bstr("uart.mmio_size"), info->uart.mmio_size);
    SysinfoWriter__line(&writer, &bstr("uart.irq"), info->uart.irq);
    SysinfoWriter__line(&writer, &bstr("uart.kind"), info->uart.kind);
    SysinfoWriter__line(&writer, &bstr("uart.register_shift"), info->uart.register_shift);
    SysinfoWriter__line(&writer, &bstr("uart.register_width"), info->uart.register_width);
    match_optional(
        info->rtc,
        some(value)
        {
            SysinfoWriter__line(&writer, &bstr("rtc.present"), 1);
            SysinfoWriter__line(&writer, &bstr("rtc.physical_base"), value.device.physical_base);
            SysinfoWriter__line(&writer, &bstr("rtc.mmio_size"), value.device.mmio_size);
            SysinfoWriter__line(&writer, &bstr("rtc.irq"), value.device.irq);
            SysinfoWriter__line(&writer, &bstr("rtc.kind"), value.kind);
        },
        none()
        {
            SysinfoWriter__line(&writer, &bstr("rtc.present"), 0);
        }
    );
    match_optional(
        info->framebuffer,
        some(value)
        {
            SysinfoWriter__line(&writer, &bstr("framebuffer.present"), 1);
            SysinfoWriter__line(&writer, &bstr("framebuffer.physical_base"), value.physical_base);
            SysinfoWriter__line(&writer, &bstr("framebuffer.physical_size"), value.size);
            SysinfoWriter__line(&writer, &bstr("framebuffer.width"), value.width);
            SysinfoWriter__line(&writer, &bstr("framebuffer.height"), value.height);
            SysinfoWriter__line(&writer, &bstr("framebuffer.pixels_per_scanline"), value.pixels_per_scanline);
            SysinfoWriter__line(&writer, &bstr("framebuffer.pixel_format"), value.pixel_format);
            SysinfoWriter__line(&writer, &bstr("framebuffer.red_mask"), value.red_mask);
            SysinfoWriter__line(&writer, &bstr("framebuffer.green_mask"), value.green_mask);
            SysinfoWriter__line(&writer, &bstr("framebuffer.blue_mask"), value.blue_mask);
            SysinfoWriter__line(&writer, &bstr("framebuffer.reserved_mask"), value.reserved_mask);
        },
        none()
        {
            SysinfoWriter__line(&writer, &bstr("framebuffer.present"), 0);
        }
    );
    match_optional(
        info->pci,
        some(value)
        {
            SysinfoWriter__line(&writer, &bstr("pci.present"), 1);
            SysinfoWriter__line(&writer, &bstr("pci.ecam.physical_base"), value.ecam_physical_base);
            SysinfoWriter__line(&writer, &bstr("pci.ecam.size"), value.ecam_size);
            SysinfoWriter__line(&writer, &bstr("pci.bus.start"), value.bus_start);
            SysinfoWriter__line(&writer, &bstr("pci.bus.end"), value.bus_end);
        },
        none()
        {
            SysinfoWriter__line(&writer, &bstr("pci.present"), 0);
        }
    );
    match_optional(
        info->sd_host,
        some(value)
        {
            SysinfoWriter__line(&writer, &bstr("sd_host.present"), 1);
            SysinfoWriter__line(&writer, &bstr("sd_host.physical_base"), value.device.physical_base);
            SysinfoWriter__line(&writer, &bstr("sd_host.mmio_size"), value.device.mmio_size);
            SysinfoWriter__line(&writer, &bstr("sd_host.irq"), value.device.irq);
            SysinfoWriter__line(&writer, &bstr("sd_host.kind"), value.kind);
            SysinfoWriter__line(&writer, &bstr("sd_host.source_clock_hz"), value.source_clock_hz);
        },
        none()
        {
            SysinfoWriter__line(&writer, &bstr("sd_host.present"), 0);
        }
    );
    match_optional(
        info->ethernet,
        some(value)
        {
            SysinfoWriter__line(&writer, &bstr("ethernet.present"), 1);
            SysinfoWriter__line(&writer, &bstr("ethernet.mac_kind"), value.kind);
            SysinfoWriter__line(&writer, &bstr("ethernet.physical_base"), value.device.physical_base);
            SysinfoWriter__line(&writer, &bstr("ethernet.mmio_size"), value.device.mmio_size);
            SysinfoWriter__line(&writer, &bstr("ethernet.irq"), value.device.irq);
        },
        none()
        {
            SysinfoWriter__line(&writer, &bstr("ethernet.present"), 0);
        }
    );
    SysinfoWriter__line(&writer, &bstr("memory.physical_start"), info->memory_start);
    SysinfoWriter__line(&writer, &bstr("memory.physical_end"), info->memory_end);
    SysinfoWriter__line(&writer, &bstr("memory.physical_size"), info->memory_end - info->memory_start);
    SysinfoWriter__line(&writer, &bstr("interrupt_controller.kind"), info->interrupt_controller.kind);
    if (info->interrupt_controller.kind == INTERRUPT_CONTROLLER_GIC_V2 ||
        info->interrupt_controller.kind == INTERRUPT_CONTROLLER_GIC_V3)
    {
        BootGicInfo gic = info->interrupt_controller.details.gic;
        SysinfoWriter__line(&writer, &bstr("interrupt_controller.distributor_physical_base"),
                            gic.distributor_physical_base);
        SysinfoWriter__line(&writer, &bstr("interrupt_controller.redistributor_physical_base"),
                            gic.redistributor_physical_base);
        SysinfoWriter__line(&writer, &bstr("interrupt_controller.cpu_interface_physical_base"),
                            gic.cpu_interface_physical_base);
    }
    else if (info->interrupt_controller.kind == INTERRUPT_CONTROLLER_PLIC)
    {
        BootPlicInfo plic = info->interrupt_controller.details.plic;
        SysinfoWriter__line(&writer, &bstr("interrupt_controller.plic.physical_base"), plic.physical_base);
        SysinfoWriter__line(&writer, &bstr("interrupt_controller.plic.source_count"), plic.source_count);
    }
    return writer.failed ? Error(FilesystemFileSize, bstr("Sysinfo buffer full"), IO_UNKNOWN_BUFFER)
                         : Success(FilesystemFileSize, writer.size);
}

static Result(FilesystemFileSize)
    Sysinfofs__load(IOServiceContext context, ConstBStringPtr path, FilesystemFileDataPtr data)
{
    if (!BString__equals(path, &bstr("/sysinfo")))
    {
        return Error(FilesystemFileSize, bstr("Unknown boot file"), IO_INVALID_PATH);
    }
    match_result(
        BootHardwareInfo__load(),
        success(info)
        {
            return Sysinfofs__render(&info, data);
        },
        error(code, message)
        {
            return Error(FilesystemFileSize, message, IO_NO_DEVICE_FOUND);
        }
    );
}

static Result(DirectoryEntryCount) Sysinfofs__list(IOServiceContext context, ConstBStringPtr path,
                                                   DirectoryEntryPtr entries, size_t capacity, uint64_t offset)
{
    if (!BString__equals(path, &bstr("/")))
    {
        return Error(DirectoryEntryCount, bstr("Unknown boot directory"), IO_INVALID_PATH);
    }
    if (capacity == 0 || offset > 0)
    {
        return Success(DirectoryEntryCount, 0);
    }
    entries[0] = DirectoryEntry__new(IO_DIRENT_ATTR_NONE, PROCESS_USER_ROOT, PROCESS_GROUP_ROOT,
                                     IO_MODE_TYPE_FILE | 0444, &bstr("sysinfo"));
    return Success(DirectoryEntryCount, 1);
}

static IOStatus Sysinfofs__store(IOServiceContext context, ConstBStringPtr path, FilesystemFileDataPtr data,
                                 FilesystemFileSize size)
{
    return IO_PERMISSION_DENIED;
}

static IOStatus Sysinfofs__mkdir(IOServiceContext context, ConstBStringPtr path)
{
    return IO_PERMISSION_DENIED;
}

static IOStatus Sysinfofs__remove(IOServiceContext context, ConstBStringPtr path, Flags flags)
{
    return IO_PERMISSION_DENIED;
}

FilesystemOperations Sysinfofs__operations(void)
{
    return FilesystemOperations__new_snapshot(Sysinfofs__load, Sysinfofs__store, Sysinfofs__list, Sysinfofs__mkdir,
                                              Sysinfofs__remove, FilesystemOperations__unsupported_link, 0);
}
