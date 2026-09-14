#include "bootfs.h"

#include "../../../../cpu.h"
#include "../../../../kernel_core.h"
#include "../../../../kalloc.h"

static BString sysinfo_path = bstr("sysinfo");
static BString invalid_path = bstr("Invalid bootfs path");
static BString invalid_handle = bstr("Invalid bootfs handle");
static BString missing_system_info = bstr("Missing system info");
static BString sysinfo_buffer_full = bstr("Sysinfo read buffer full");
static BString equals = bstr("=");
static BString newline = bstr("\n");
static BString uint64_dec_format = bstr("%llu");
static BString uint64_hex_format = bstr("0x%llx");
static BString memory_free_region_prefix = bstr("memory.free_region.");
static BString physical_start_suffix = bstr(".physical_start");
static BString physical_end_suffix = bstr(".physical_end");
static BString physical_size_suffix = bstr(".physical_size");
static BString physical_size_human_suffix = bstr(".physical_size_human");
static BString bytes_unit = bstr(" bytes");
static BString kib_unit = bstr(" KiB");
static BString mib_unit = bstr(" MiB");
static BString gib_unit = bstr(" GiB");
static BString tib_unit = bstr(" TiB");
static BString zero_digit = bstr("0");
static BString cpu_count_key = bstr("cpu.count");
static BString cpu_online_count_key = bstr("cpu.online_count");
static BString uart_physical_base_key = bstr("uart.physical_base");
static BString uart_virtual_base_key = bstr("uart.virtual_base");
static BString uart_irq_key = bstr("uart.irq");
static BString uart_mmio_size_key = bstr("uart.mmio_size");
static BString framebuffer_present_key = bstr("framebuffer.present");
static BString framebuffer_physical_base_key = bstr("framebuffer.physical_base");
static BString framebuffer_physical_size_key = bstr("framebuffer.physical_size");
static BString framebuffer_physical_size_human_key = bstr("framebuffer.physical_size_human");
static BString framebuffer_width_key = bstr("framebuffer.width");
static BString framebuffer_height_key = bstr("framebuffer.height");
static BString framebuffer_pixels_per_scanline_key = bstr("framebuffer.pixels_per_scanline");
static BString framebuffer_pixel_format_key = bstr("framebuffer.pixel_format");
static BString framebuffer_red_mask_key = bstr("framebuffer.red_mask");
static BString framebuffer_green_mask_key = bstr("framebuffer.green_mask");
static BString framebuffer_blue_mask_key = bstr("framebuffer.blue_mask");
static BString framebuffer_reserved_mask_key = bstr("framebuffer.reserved_mask");
static BString pci_present_key = bstr("pci.present");
static BString pci_ecam_physical_base_key = bstr("pci.ecam.physical_base");
static BString pci_ecam_size_key = bstr("pci.ecam.size");
static BString pci_ecam_size_human_key = bstr("pci.ecam.size_human");
static BString pci_bus_start_key = bstr("pci.bus.start");
static BString pci_bus_end_key = bstr("pci.bus.end");
static BString sd_host_present_key = bstr("sd_host.present");
static BString sd_host_physical_base_key = bstr("sd_host.physical_base");
static BString sd_host_mmio_size_key = bstr("sd_host.mmio_size");
static BString sd_host_irq_key = bstr("sd_host.irq");
static BString ethernet_present_key = bstr("ethernet.present");
static BString ethernet_mac_kind_key = bstr("ethernet.mac_kind");
static BString ethernet_physical_base_key = bstr("ethernet.physical_base");
static BString ethernet_mmio_size_key = bstr("ethernet.mmio_size");
static BString ethernet_irq_key = bstr("ethernet.irq");
static BString memory_physical_start_key = bstr("memory.physical_start");
static BString memory_physical_end_key = bstr("memory.physical_end");
static BString memory_physical_size_key = bstr("memory.physical_size");
static BString memory_physical_size_human_key = bstr("memory.physical_size_human");
static BString memory_free_region_count_key = bstr("memory.free_region.count");
static BString memory_kernel_heap_prefix = bstr("memory.kernel_heap");
static BString interrupt_controller_kind_key = bstr("interrupt_controller.kind");
static BString interrupt_controller_distributor_key = bstr("interrupt_controller.distributor_physical_base");
static BString interrupt_controller_redistributor_key = bstr("interrupt_controller.redistributor_physical_base");
static BString interrupt_controller_cpu_interface_key = bstr("interrupt_controller.cpu_interface_physical_base");

typedef struct BootSysinfoWriter
{
    IOBufferPtr buffer;
    FilePointer target_offset;
    size_t requested_size;
    FilePointer cursor;
    size_t written;
    bool failed;
} BootSysinfoWriter;

typedef BootSysinfoWriter * BootSysinfoWriterPtr;

static BootSysinfoWriter BootSysinfoWriter__new(IOBufferPtr buffer, FilePointer offset, size_t size)
{
    return (BootSysinfoWriter){
        .buffer = buffer,
        .target_offset = offset,
        .requested_size = size,
        .cursor = 0,
        .written = 0,
        .failed = false,
    };
}

static void BootSysinfoWriter__append_char(BootSysinfoWriterPtr self, char ch)
{
    if (self->cursor >= self->target_offset && self->written < self->requested_size && !self->failed)
    {
        match_result(
            IOBuffer__push(self->buffer, ch),
            success(pos)
            {
                self->written++;
                ;
            },
            error(code, message)
            {
                self->failed = true;
                ;
            }
        );
    }

    self->cursor++;
}

static void BootSysinfoWriter__append_bstring(BootSysinfoWriterPtr self, ConstBStringPtr text)
{
    for_each_bstring(text, index, ch, { BootSysinfoWriter__append_char(self, ch); });
}

static void BootSysinfoWriter__append_key_value_prefix(BootSysinfoWriterPtr self, ConstBStringPtr key)
{
    BootSysinfoWriter__append_bstring(self, key);
    BootSysinfoWriter__append_bstring(self, &equals);
}

static void BootSysinfoWriter__append_hex_line(BootSysinfoWriterPtr self, ConstBStringPtr key, uint64_t value)
{
    BootSysinfoWriter__append_key_value_prefix(self, key);
    BString text = BString__sprintf(&uint64_hex_format, value);
    BootSysinfoWriter__append_bstring(self, &text);
    BootSysinfoWriter__append_bstring(self, &newline);
}

static void BootSysinfoWriter__append_dec_line(BootSysinfoWriterPtr self, ConstBStringPtr key, uint64_t value)
{
    BootSysinfoWriter__append_key_value_prefix(self, key);
    BString text = BString__sprintf(&uint64_dec_format, value);
    BootSysinfoWriter__append_bstring(self, &text);
    BootSysinfoWriter__append_bstring(self, &newline);
}

static void BootSysinfoWriter__append_human_size(BootSysinfoWriterPtr self, uint64_t bytes)
{
    const uint64_t kib = 1024ULL;
    const uint64_t mib = kib * 1024ULL;
    const uint64_t gib = mib * 1024ULL;
    const uint64_t tib = gib * 1024ULL;

    if (bytes >= tib && bytes % tib == 0)
    {
        BString text = BString__sprintf(&uint64_dec_format, bytes / tib);
        BootSysinfoWriter__append_bstring(self, &text);
        BootSysinfoWriter__append_bstring(self, &tib_unit);
    }
    else if (bytes >= gib && bytes % gib == 0)
    {
        BString text = BString__sprintf(&uint64_dec_format, bytes / gib);
        BootSysinfoWriter__append_bstring(self, &text);
        BootSysinfoWriter__append_bstring(self, &gib_unit);
    }
    else if (bytes >= mib && bytes % mib == 0)
    {
        BString text = BString__sprintf(&uint64_dec_format, bytes / mib);
        BootSysinfoWriter__append_bstring(self, &text);
        BootSysinfoWriter__append_bstring(self, &mib_unit);
    }
    else if (bytes >= kib && bytes % kib == 0)
    {
        BString text = BString__sprintf(&uint64_dec_format, bytes / kib);
        BootSysinfoWriter__append_bstring(self, &text);
        BootSysinfoWriter__append_bstring(self, &kib_unit);
    }
    else
    {
        BString text = BString__sprintf(&uint64_dec_format, bytes);
        BootSysinfoWriter__append_bstring(self, &text);
        BootSysinfoWriter__append_bstring(self, &bytes_unit);
    }
}

static void BootSysinfoWriter__append_human_size_line(BootSysinfoWriterPtr self, ConstBStringPtr key, uint64_t bytes)
{
    BootSysinfoWriter__append_key_value_prefix(self, key);
    BootSysinfoWriter__append_human_size(self, bytes);
    BootSysinfoWriter__append_bstring(self, &newline);
}

static void BootSysinfoWriter__append_region_key(BootSysinfoWriterPtr self, ConstBStringPtr prefix,
                                                 ConstBStringPtr suffix)
{
    BootSysinfoWriter__append_bstring(self, prefix);
    BootSysinfoWriter__append_bstring(self, suffix);
}

static void BootSysinfoWriter__append_memory_region(BootSysinfoWriterPtr self, ConstBStringPtr prefix,
                                                    MemoryRegionPtr region)
{
    BootSysinfoWriter__append_region_key(self, prefix, &physical_start_suffix);
    BootSysinfoWriter__append_bstring(self, &equals);
    BString physical_start = BString__sprintf(&uint64_hex_format, region->physical_start);
    BootSysinfoWriter__append_bstring(self, &physical_start);
    BootSysinfoWriter__append_bstring(self, &newline);
    BootSysinfoWriter__append_region_key(self, prefix, &physical_end_suffix);
    BootSysinfoWriter__append_bstring(self, &equals);
    BString physical_end = BString__sprintf(&uint64_hex_format, region->physical_end);
    BootSysinfoWriter__append_bstring(self, &physical_end);
    BootSysinfoWriter__append_bstring(self, &newline);
    uint64_t size = region->physical_end - region->physical_start;
    BootSysinfoWriter__append_region_key(self, prefix, &physical_size_suffix);
    BootSysinfoWriter__append_bstring(self, &equals);
    BString physical_size = BString__sprintf(&uint64_hex_format, size);
    BootSysinfoWriter__append_bstring(self, &physical_size);
    BootSysinfoWriter__append_bstring(self, &newline);
    BootSysinfoWriter__append_region_key(self, prefix, &physical_size_human_suffix);
    BootSysinfoWriter__append_bstring(self, &equals);
    BootSysinfoWriter__append_human_size(self, size);
    BootSysinfoWriter__append_bstring(self, &newline);
}

static void BootSysinfoWriter__append_physical_memory_region_key(BootSysinfoWriterPtr self, uint64_t index,
                                                                 ConstBStringPtr suffix)
{
    BootSysinfoWriter__append_bstring(self, &memory_free_region_prefix);
    if (index < 10)
    {
        BootSysinfoWriter__append_bstring(self, &zero_digit);
    }
    BString index_text = BString__sprintf(&uint64_dec_format, index);
    BootSysinfoWriter__append_bstring(self, &index_text);
    BootSysinfoWriter__append_bstring(self, suffix);
}

static void BootSysinfoWriter__append_physical_memory_region(BootSysinfoWriterPtr self, uint64_t index,
                                                             PhysicalMemoryRegionPtr region)
{
    BootSysinfoWriter__append_physical_memory_region_key(self, index, &physical_start_suffix);
    BootSysinfoWriter__append_bstring(self, &equals);
    BString physical_start = BString__sprintf(&uint64_hex_format, region->physical_start);
    BootSysinfoWriter__append_bstring(self, &physical_start);
    BootSysinfoWriter__append_bstring(self, &newline);
    BootSysinfoWriter__append_physical_memory_region_key(self, index, &physical_end_suffix);
    BootSysinfoWriter__append_bstring(self, &equals);
    BString physical_end = BString__sprintf(&uint64_hex_format, region->physical_end);
    BootSysinfoWriter__append_bstring(self, &physical_end);
    BootSysinfoWriter__append_bstring(self, &newline);
    uint64_t size = region->physical_end - region->physical_start;
    BootSysinfoWriter__append_physical_memory_region_key(self, index, &physical_size_suffix);
    BootSysinfoWriter__append_bstring(self, &equals);
    BString physical_size = BString__sprintf(&uint64_hex_format, size);
    BootSysinfoWriter__append_bstring(self, &physical_size);
    BootSysinfoWriter__append_bstring(self, &newline);
    BootSysinfoWriter__append_physical_memory_region_key(self, index, &physical_size_human_suffix);
    BootSysinfoWriter__append_bstring(self, &equals);
    BootSysinfoWriter__append_human_size(self, size);
    BootSysinfoWriter__append_bstring(self, &newline);
}

static void BootSysinfoWriter__append_physical_memory_regions(BootSysinfoWriterPtr self,
                                                              PhysicalMemoryRegions * regions)
{
    size_t region_count = PhysicalMemoryRegions__len(regions);
    BootSysinfoWriter__append_dec_line(self, &memory_free_region_count_key, region_count);
    for (uint64_t index = 0; index < region_count; index++)
    {
        match_optional(
            PhysicalMemoryRegions__ptr_at(regions, index),
            some(region)
            {
                BootSysinfoWriter__append_physical_memory_region(self, index, region);
                ;
            },
            none_do_nothing()
        );
    }
}

static void BootSysinfoWriter__append_framebuffer(BootSysinfoWriterPtr self, OptionalPtr(FramebufferInfo) framebuffer)
{
    match_optional(
        *framebuffer,
        some(info)
        {
            BootSysinfoWriter__append_dec_line(self, &framebuffer_present_key, 1);
            BootSysinfoWriter__append_hex_line(self, &framebuffer_physical_base_key, info.physical_base);
            BootSysinfoWriter__append_hex_line(self, &framebuffer_physical_size_key, info.size);
            BootSysinfoWriter__append_human_size_line(self, &framebuffer_physical_size_human_key, info.size);
            BootSysinfoWriter__append_dec_line(self, &framebuffer_width_key, info.width);
            BootSysinfoWriter__append_dec_line(self, &framebuffer_height_key, info.height);
            BootSysinfoWriter__append_dec_line(self, &framebuffer_pixels_per_scanline_key, info.pixels_per_scanline);
            BootSysinfoWriter__append_dec_line(self, &framebuffer_pixel_format_key, info.pixel_format);
            BootSysinfoWriter__append_hex_line(self, &framebuffer_red_mask_key, info.red_mask);
            BootSysinfoWriter__append_hex_line(self, &framebuffer_green_mask_key, info.green_mask);
            BootSysinfoWriter__append_hex_line(self, &framebuffer_blue_mask_key, info.blue_mask);
            BootSysinfoWriter__append_hex_line(self, &framebuffer_reserved_mask_key, info.reserved_mask);
        },
        none()
        {
            BootSysinfoWriter__append_dec_line(self, &framebuffer_present_key, 0);
            ;
        }
    );
}

static void BootSysinfoWriter__append_pci(BootSysinfoWriterPtr self, OptionalPtr(PciInfo) pci)
{
    match_optional(
        *pci,
        some(info)
        {
            BootSysinfoWriter__append_dec_line(self, &pci_present_key, 1);
            BootSysinfoWriter__append_hex_line(self, &pci_ecam_physical_base_key, info.ecam_physical_base);
            BootSysinfoWriter__append_hex_line(self, &pci_ecam_size_key, info.ecam_size);
            BootSysinfoWriter__append_human_size_line(self, &pci_ecam_size_human_key, info.ecam_size);
            BootSysinfoWriter__append_dec_line(self, &pci_bus_start_key, info.bus_start);
            BootSysinfoWriter__append_dec_line(self, &pci_bus_end_key, info.bus_end);
        },
        none()
        {
            BootSysinfoWriter__append_dec_line(self, &pci_present_key, 0);
            ;
        }
    );
}

static void BootSysinfoWriter__append_sd_host(BootSysinfoWriterPtr self, OptionalPtr(SdHostInfo) sd_host)
{
    match_optional(
        *sd_host,
        some(info)
        {
            BootSysinfoWriter__append_dec_line(self, &sd_host_present_key, 1);
            BootSysinfoWriter__append_hex_line(self, &sd_host_physical_base_key, info.physical_base);
            BootSysinfoWriter__append_hex_line(self, &sd_host_mmio_size_key, info.mmio_size);
            BootSysinfoWriter__append_dec_line(self, &sd_host_irq_key, info.irq);
        },
        none()
        {
            BootSysinfoWriter__append_dec_line(self, &sd_host_present_key, 0);
            ;
        }
    );
}

static void BootSysinfoWriter__append_ethernet(BootSysinfoWriterPtr self, OptionalPtr(EthernetMacInfo) ethernet_mac)
{
    match_optional(
        *ethernet_mac,
        some(info)
        {
            BootSysinfoWriter__append_dec_line(self, &ethernet_present_key, 1);
            BootSysinfoWriter__append_dec_line(self, &ethernet_mac_kind_key, info.kind);
            BootSysinfoWriter__append_hex_line(self, &ethernet_physical_base_key, info.physical_base);
            BootSysinfoWriter__append_hex_line(self, &ethernet_mmio_size_key, info.mmio_size);
            BootSysinfoWriter__append_dec_line(self, &ethernet_irq_key, info.irq);
        },
        none()
        {
            BootSysinfoWriter__append_dec_line(self, &ethernet_present_key, 0);
            ;
        }
    );
}

static BootSysinfoWriter BootSysinfoWriter__write_system_info(SystemInfoPtr info, IOBufferPtr buffer,
                                                              FilePointer offset, size_t size)
{
    BootSysinfoWriter writer = BootSysinfoWriter__new(buffer, offset, size);

    BootSysinfoWriter__append_dec_line(&writer, &cpu_count_key, info->cpu_count);
    BootSysinfoWriter__append_dec_line(&writer, &cpu_online_count_key, CPU__count());
    BootSysinfoWriter__append_hex_line(&writer, &uart_physical_base_key, info->uart.physical_base);
    BootSysinfoWriter__append_hex_line(&writer, &uart_virtual_base_key, info->uart.virtual_base);
    BootSysinfoWriter__append_dec_line(&writer, &uart_irq_key, info->uart.irq);
    BootSysinfoWriter__append_hex_line(&writer, &uart_mmio_size_key, 0x1000);
    BootSysinfoWriter__append_framebuffer(&writer, &info->framebuffer);
    BootSysinfoWriter__append_pci(&writer, &info->pci);
    BootSysinfoWriter__append_sd_host(&writer, &info->sd_host);
    BootSysinfoWriter__append_ethernet(&writer, &info->ethernet_mac);
    BootSysinfoWriter__append_hex_line(&writer, &memory_physical_start_key, info->physical_memory.physical_start);
    BootSysinfoWriter__append_hex_line(&writer, &memory_physical_end_key, info->physical_memory.physical_end);
    uint64_t physical_size = info->physical_memory.physical_end - info->physical_memory.physical_start;
    BootSysinfoWriter__append_hex_line(&writer, &memory_physical_size_key, physical_size);
    BootSysinfoWriter__append_human_size_line(&writer, &memory_physical_size_human_key, physical_size);
    BootSysinfoWriter__append_physical_memory_regions(&writer, &info->physical_memory.free_regions);
    BootSysinfoWriter__append_memory_region(&writer, &memory_kernel_heap_prefix, &info->kernel_heap);
    BootSysinfoWriter__append_dec_line(&writer, &interrupt_controller_kind_key, info->interrupt_controller.kind);
    BootSysinfoWriter__append_hex_line(&writer, &interrupt_controller_distributor_key,
                                       info->interrupt_controller.distributor_physical_base);
    BootSysinfoWriter__append_hex_line(&writer, &interrupt_controller_redistributor_key,
                                       info->interrupt_controller.redistributor_physical_base);
    BootSysinfoWriter__append_hex_line(&writer, &interrupt_controller_cpu_interface_key,
                                       info->interrupt_controller.cpu_interface_physical_base);

    return writer;
}

static Result(IOInternalHandle) BootFilesystemIOState__add_handle(BootFilesystemIOStatePtr self, ConstBStringPtr path)
{
    critical_section(self->state.lock, {
        IOInternalHandle handle = self->next_handle++ | self->state.filesystem->fs_code;
        FileHandleTargetPtr target = FileHandleTarget__heap_new(path);
        FileHandleTargetMap__set(&self->state.handles, handle, target);
        cs_return Success(IOInternalHandle, handle);
    });
}

static Result(FileHandleTargetPtr)
    BootFilesystemIOState__get_handle(BootFilesystemIOStatePtr self, IOInternalHandle handle)
{
    critical_section(self->state.lock, {
        match_optional(
            FileHandleTargetMap__get(&self->state.handles, handle),
            some(target) { cs_return Success(FileHandleTargetPtr, target); },
            none() { cs_return Error(FileHandleTargetPtr, invalid_handle, IO_UNKNOWN_HANDLE); }
        );
    });

    return Error(FileHandleTargetPtr, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(IOInternalHandle)
    BootFilesystemIOState__delete_handle(BootFilesystemIOStatePtr self, IOInternalHandle handle)
{
    critical_section(self->state.lock, {
        match_optional(
            FileHandleTargetMap__get(&self->state.handles, handle),
            some(target)
            {
                FileHandleTarget__dispose(target);
                FileHandleTargetMap__delete(&self->state.handles, handle);
                cs_return Success(IOInternalHandle, handle);
            },
            none() { cs_return Error(IOInternalHandle, invalid_handle, IO_UNKNOWN_HANDLE); }
        );
    });

    return Error(IOInternalHandle, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(IOInternalHandle) BootFilesystemIO__open(IOStatePtr self, ConstBStringPtr path, Flags flags)
{
    if (BString__equals(path, &sysinfo_path))
    {
        return BootFilesystemIOState__add_handle((BootFilesystemIOStatePtr)self, path);
    }

    return Error(IOInternalHandle, invalid_path, IO_INVALID_PATH);
}

static Result(IOInternalHandle) BootFilesystemIO__close(IOStatePtr self, IOInternalHandle handle)
{
    return BootFilesystemIOState__delete_handle((BootFilesystemIOStatePtr)self, handle);
}

static Result(IOBufferPtr)
    BootFilesystemIO__read(IOStatePtr self, IOInternalHandle handle, size_t size, ConstExternalBufferPtr target_buffer)
{
    BootFilesystemIOStatePtr fs_state = (BootFilesystemIOStatePtr)self;

    match_result(
        BootFilesystemIOState__get_handle(fs_state, handle),
        success(target)
        {
            if (!BString__equals(&target->path, &sysinfo_path))
            {
                return Error(IOBufferPtr, invalid_path, IO_INVALID_PATH);
            }

            match_optional(
                fs_state->kernel->system_info,
                some(system_info)
                {
                    IOBuffer__reset(&target->buffer);
                    BootSysinfoWriter writer =
                        BootSysinfoWriter__write_system_info(&system_info, &target->buffer, target->offset, size);
                    if (writer.failed)
                    {
                        return Error(IOBufferPtr, sysinfo_buffer_full, IO_OPERATION_NOT_SUPPORTED);
                    }

                    if (writer.written == 0 && target->offset >= writer.cursor)
                    {
                        return Error(IOBufferPtr, invalid_path, IO_EOF);
                    }

                    target->offset += writer.written;
                    return Success(IOBufferPtr, &target->buffer);
                },
                none()
                {
                    return Error(IOBufferPtr, missing_system_info, IO_OPERATION_NOT_SUPPORTED);
                    ;
                }
            );
        },
        error(code, message) { return Error(IOBufferPtr, message, code); }
    );

    return Error(IOBufferPtr, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(FilesystemNodes)
    BootFilesystemIO__list_nodes(IOStatePtr self, FilesystemNodePtr parent, ConstBStringPtr path)
{
    BStringArray path_segments = BString__split(path, '/');
    if (BStringArray__len(&path_segments) != 0)
    {
        return Error(FilesystemNodes, invalid_path, IO_INVALID_PATH);
    }

    BootFilesystemIOStatePtr fs_state = (BootFilesystemIOStatePtr)self;
    FilesystemNodes result = EmptyArray(FilesystemNodes);
    FilesystemNodes__push(&result, FilesystemNode__heap_new(fs_state->state.filesystem, &sysinfo_path, 0));

    return Success(FilesystemNodes, result);
}

FileIO BootFilesystemIO__new()
{
    return (FileIO){
        .base_io = IO__new(BootFilesystemIO__open, BootFilesystemIO__read, IO__unsupported_write,
                           IO__unsupported_accept, BootFilesystemIO__close, IO__unsupported_park,
                           IO__unsupported_unpark, IO__unsupported_server_buffer, IO__unsupported_server_write_buffer,
                           IO__unsupported_data_available, IO__unsupported_data_consumed, IO__unsupported_eof),
        .list_nodes = BootFilesystemIO__list_nodes,
        .list_dir = FileIO__unsupported_list_dir,
        .mkdir = FileIO__unsupported_mkdir,
        .remove = FileIO__unsupported_remove,
        .link = FileIO__unsupported_link,
    };
}

BootFilesystemIOStatePtr BootFilesystemIOState__heap_new(FilesystemPtr fs, KernelPtr kernel)
{
    BootFilesystemIOStatePtr state = ptr(Kernel__alloc_kernel(sizeof(BootFilesystemIOState)), BootFilesystemIOState);
    *state = (BootFilesystemIOState){
        .state = FilesystemIOState__new(fs),
        .kernel = kernel,
        .next_handle = 1,
    };
    fs->state = &state->state;
    return state;
}
