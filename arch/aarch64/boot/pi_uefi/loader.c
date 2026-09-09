#include <stdint.h>
#include <stddef.h>

#include "handoff.h"

#define EFI_SUCCESS 0ULL
#define EFI_ERROR_BIT 0x8000000000000000ULL
#define EFI_BUFFER_TOO_SMALL (EFI_ERROR_BIT | 5ULL)
#define EFI_NOT_FOUND (EFI_ERROR_BIT | 14ULL)
#define EFI_LOAD_ERROR (EFI_ERROR_BIT | 1ULL)
#define EFI_ABORTED (EFI_ERROR_BIT | 21ULL)

#define EFI_ALLOCATE_ANY_PAGES 0
#define EFI_ALLOCATE_ADDRESS 2
#define EFI_LOADER_DATA 2
#define EFI_FILE_MODE_READ 1ULL
#define PAGE_SIZE 4096ULL
#define PAGE_MASK (~(PAGE_SIZE - 1ULL))
#define ELF_MAGIC 0x464c457fU
#define ELF_CLASS_64 2
#define ELF_DATA_LSB 1
#define ELF_MACHINE_AARCH64 183
#define ELF_PROGRAM_LOAD 1

#define EFI_LOADER_CODE 1
#define EFI_BOOT_SERVICES_CODE 3
#define EFI_BOOT_SERVICES_DATA 4
#define EFI_RUNTIME_SERVICES_CODE 5
#define EFI_RUNTIME_SERVICES_DATA 6
#define EFI_CONVENTIONAL_MEMORY 7
#define EFI_ACPI_RECLAIM_MEMORY 9
#define EFI_ACPI_MEMORY_NVS 10

typedef uint64_t EfiStatus;
typedef void * EfiHandle;
typedef uint64_t EfiPhysicalAddress;

typedef struct EfiSimpleTextOutputProtocol EfiSimpleTextOutputProtocol;
typedef EfiSimpleTextOutputProtocol * EfiSimpleTextOutputProtocolPtr;
typedef struct EfiBootServices EfiBootServices;
typedef EfiBootServices * EfiBootServicesPtr;
typedef struct EfiSystemTable EfiSystemTable;
typedef EfiSystemTable * EfiSystemTablePtr;
typedef struct EfiSimpleFileSystemProtocol EfiSimpleFileSystemProtocol;
typedef EfiSimpleFileSystemProtocol * EfiSimpleFileSystemProtocolPtr;
typedef struct EfiFileProtocol EfiFileProtocol;
typedef EfiFileProtocol * EfiFileProtocolPtr;
typedef struct EfiGraphicsOutputProtocol EfiGraphicsOutputProtocol;
typedef EfiGraphicsOutputProtocol * EfiGraphicsOutputProtocolPtr;

typedef struct EfiGuid
{
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} EfiGuid;

typedef const EfiGuid * ConstEfiGuidPtr;

typedef struct EfiTableHeader
{
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} EfiTableHeader;

struct EfiSimpleTextOutputProtocol
{
    void * reset;
    EfiStatus (*output_string)(EfiSimpleTextOutputProtocolPtr self, const uint16_t * string);
};

struct EfiBootServices
{
    EfiTableHeader header;
    void * raise_tpl;
    void * restore_tpl;
    EfiStatus (*allocate_pages)(int type, int memory_type, uint64_t pages, EfiPhysicalAddress * memory);
    EfiStatus (*free_pages)(EfiPhysicalAddress memory, uint64_t pages);
    EfiStatus (*get_memory_map)(uint64_t * memory_map_size, void * memory_map, uint64_t * map_key,
                                uint64_t * descriptor_size, uint32_t * descriptor_version);
    EfiStatus (*allocate_pool)(int pool_type, uint64_t size, void ** buffer);
    EfiStatus (*free_pool)(void * buffer);
    void * create_event;
    void * set_timer;
    void * wait_for_event;
    void * signal_event;
    void * close_event;
    void * check_event;
    void * install_protocol_interface;
    void * reinstall_protocol_interface;
    void * uninstall_protocol_interface;
    EfiStatus (*handle_protocol)(EfiHandle handle, ConstEfiGuidPtr protocol, void ** interface);
    void * reserved;
    void * register_protocol_notify;
    void * locate_handle;
    void * locate_device_path;
    void * install_configuration_table;
    void * load_image;
    void * start_image;
    void * exit;
    void * unload_image;
    EfiStatus (*exit_boot_services)(EfiHandle image_handle, uint64_t map_key);
    void * get_next_monotonic_count;
    void * stall;
    void * set_watchdog_timer;
    void * connect_controller;
    void * disconnect_controller;
    void * open_protocol;
    void * close_protocol;
    void * open_protocol_information;
    void * protocols_per_handle;
    void * locate_handle_buffer;
    EfiStatus (*locate_protocol)(ConstEfiGuidPtr protocol, void * registration, void ** interface);
};

struct EfiSystemTable
{
    EfiTableHeader header;
    uint16_t * firmware_vendor;
    uint32_t firmware_revision;
    uint32_t reserved;
    EfiHandle console_in_handle;
    void * console_in;
    EfiHandle console_out_handle;
    EfiSimpleTextOutputProtocolPtr console_out;
    EfiHandle standard_error_handle;
    EfiSimpleTextOutputProtocolPtr standard_error;
    void * runtime_services;
    EfiBootServicesPtr boot_services;
    uint64_t number_of_table_entries;
    void * configuration_table;
};

typedef struct EfiLoadedImageProtocol
{
    uint32_t revision;
    EfiHandle parent_handle;
    EfiSystemTablePtr system_table;
    EfiHandle device_handle;
    void * file_path;
    void * reserved;
    uint32_t load_options_size;
    void * load_options;
    void * image_base;
    uint64_t image_size;
    int image_code_type;
    int image_data_type;
    EfiStatus (*unload)(EfiHandle image_handle);
} EfiLoadedImageProtocol;

typedef EfiLoadedImageProtocol * EfiLoadedImageProtocolPtr;

struct EfiSimpleFileSystemProtocol
{
    uint64_t revision;
    EfiStatus (*open_volume)(EfiSimpleFileSystemProtocolPtr self, EfiFileProtocolPtr * root);
};

struct EfiFileProtocol
{
    uint64_t revision;
    EfiStatus (*open)(EfiFileProtocolPtr self, EfiFileProtocolPtr * new_handle, const uint16_t * file_name,
                      uint64_t open_mode, uint64_t attributes);
    EfiStatus (*close)(EfiFileProtocolPtr self);
    void * delete_file;
    EfiStatus (*read)(EfiFileProtocolPtr self, uint64_t * buffer_size, void * buffer);
    void * write;
    void * get_position;
    EfiStatus (*set_position)(EfiFileProtocolPtr self, uint64_t position);
    EfiStatus (*get_info)(EfiFileProtocolPtr self, ConstEfiGuidPtr information_type, uint64_t * buffer_size,
                          void * buffer);
};

typedef struct EfiFileInfo
{
    uint64_t size;
    uint64_t file_size;
    uint64_t physical_size;
    uint8_t create_time[16];
    uint8_t last_access_time[16];
    uint8_t modification_time[16];
    uint64_t attribute;
    uint16_t file_name[];
} EfiFileInfo;

typedef EfiFileInfo * EfiFileInfoPtr;

typedef struct EfiMemoryDescriptor
{
    uint32_t type;
    uint32_t padding;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} EfiMemoryDescriptor;

typedef EfiMemoryDescriptor * EfiMemoryDescriptorPtr;

typedef struct EfiPixelBitmask
{
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved_mask;
} EfiPixelBitmask;

typedef struct EfiGraphicsOutputModeInformation
{
    uint32_t version;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    uint32_t pixel_format;
    EfiPixelBitmask pixel_information;
    uint32_t pixels_per_scanline;
} EfiGraphicsOutputModeInformation;

typedef EfiGraphicsOutputModeInformation * EfiGraphicsOutputModeInformationPtr;

typedef struct EfiGraphicsOutputProtocolMode
{
    uint32_t max_mode;
    uint32_t mode;
    EfiGraphicsOutputModeInformationPtr info;
    uint64_t size_of_info;
    EfiPhysicalAddress framebuffer_base;
    uint64_t framebuffer_size;
} EfiGraphicsOutputProtocolMode;

typedef EfiGraphicsOutputProtocolMode * EfiGraphicsOutputProtocolModePtr;

struct EfiGraphicsOutputProtocol
{
    EfiStatus (*query_mode)(EfiGraphicsOutputProtocolPtr self, uint32_t mode_number, uint64_t * size_of_info,
                            EfiGraphicsOutputModeInformationPtr * info);
    EfiStatus (*set_mode)(EfiGraphicsOutputProtocolPtr self, uint32_t mode_number);
    void * blt;
    EfiGraphicsOutputProtocolModePtr mode;
};

typedef void (*KernelEntry)(uint64_t image_handle, uint64_t system_table, uint64_t physical_memory_end,
                            uint64_t handoff_magic, uint64_t handoff_address);

typedef struct Elf64Header
{
    unsigned char ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_header_offset;
    uint64_t section_header_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_entry_size;
    uint16_t program_header_count;
    uint16_t section_header_entry_size;
    uint16_t section_header_count;
    uint16_t section_name_index;
} Elf64Header;

typedef Elf64Header * Elf64HeaderPtr;
typedef const Elf64Header * ConstElf64HeaderPtr;

typedef struct Elf64ProgramHeader
{
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t align;
} Elf64ProgramHeader;

typedef Elf64ProgramHeader * Elf64ProgramHeaderPtr;

typedef struct LoadedKernel
{
    uint64_t entry;
} LoadedKernel;

typedef LoadedKernel * LoadedKernelPtr;

static const EfiGuid loaded_image_protocol_guid = { // hundelint: ignore-constructor-required
    .data1 = 0x5b1b31a1,
    .data2 = 0x9562,
    .data3 = 0x11d2,
    .data4 = {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b},
};

static const EfiGuid simple_file_system_protocol_guid = { // hundelint: ignore-constructor-required
    .data1 = 0x0964e5b22,
    .data2 = 0x6459,
    .data3 = 0x11d2,
    .data4 = {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b},
};

static const EfiGuid file_info_guid = { // hundelint: ignore-constructor-required
    .data1 = 0x09576e92,
    .data2 = 0x6d3f,
    .data3 = 0x11d2,
    .data4 = {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b},
};

static const EfiGuid graphics_output_protocol_guid = { // hundelint: ignore-constructor-required
    .data1 = 0x9042a9de,
    .data2 = 0x23dc,
    .data3 = 0x4a38,
    .data4 = {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a},
};

static const uint16_t kernel_path[] = {'K', 'E', 'R', 'N', 'E', 'L', '.', 'E', 'L', 'F', 0};

static PiUefiHandoff pi_uefi_handoff;

static const uint16_t load_failed[] = {'k', 'e', 'r', 'n', 'e', 'l', ' ', 'l',  'o',  'a', 'd',
                                       ' ', 'f', 'a', 'i', 'l', 'e', 'd', '\r', '\n', 0};

static const uint16_t loader_start[] = {'u', 'e', 'f', 'i', ' ', 'l', 'o', 'a',  'd',  'e',
                                        'r', ' ', 's', 't', 'a', 'r', 't', '\r', '\n', 0};

static const uint16_t kernel_loaded[] = {'k', 'e', 'r', 'n', 'e', 'l',  ' ',  'l',
                                         'o', 'a', 'd', 'e', 'd', '\r', '\n', 0};

static const uint16_t kernel_opened[] = {'k', 'e', 'r', 'n', 'e', 'l',  ' ',  'o',
                                         'p', 'e', 'n', 'e', 'd', '\r', '\n', 0};

static const uint16_t kernel_sized[] = {'k', 'e', 'r', 'n', 'e', 'l', ' ', 's', 'i', 'z', 'e', 'd', '\r', '\n', 0};

static const uint16_t kernel_open_failed[] = {'k', 'e', 'r', 'n', 'e', 'l', ' ', 'o',  'p',  'e', 'n',
                                              ' ', 'f', 'a', 'i', 'l', 'e', 'd', '\r', '\n', 0};

static const uint16_t kernel_size_failed[] = {'k', 'e', 'r', 'n', 'e', 'l', ' ', 's',  'i',  'z', 'e',
                                              ' ', 'f', 'a', 'i', 'l', 'e', 'd', '\r', '\n', 0};

static const uint16_t kernel_read_failed[] = {'k', 'e', 'r', 'n', 'e', 'l', ' ', 'r',  'e',  'a', 'd',
                                              ' ', 'f', 'a', 'i', 'l', 'e', 'd', '\r', '\n', 0};

static const uint16_t kernel_allocated[] = {'k', 'e', 'r', 'n', 'e', 'l', ' ',  'a',  'l', 'l',
                                            'o', 'c', 'a', 't', 'e', 'd', '\r', '\n', 0};

static const uint16_t framebuffer_found[] = {'f', 'r', 'a', 'm', 'e', 'b', 'u', 'f', 'f', 'e', 'r', ' ', 0};

static const uint16_t kernel_alloc_failed[] = {'k', 'e', 'r', 'n', 'e', 'l', ' ', 'a', 'l',  'l',  'o',
                                               'c', ' ', 'f', 'a', 'i', 'l', 'e', 'd', '\r', '\n', 0};

static const uint16_t elf_invalid[] = {'k', 'e', 'r', 'n', 'e', 'l', ' ', 'e',  'l',  'f', ' ',
                                       'i', 'n', 'v', 'a', 'l', 'i', 'd', '\r', '\n', 0};

static const uint16_t elf_segment_loaded[] = {'e', 'l', 'f', ' ', 's', 'e', 'g', 'm', 'e', 'n',
                                              't', ' ', 'l', 'o', 'a', 'd', 'e', 'd', ' ', 0};

static const uint16_t elf_entry_loaded[] = {'e', 'l', 'f', ' ', 'e', 'n', 't', 'r', 'y', ' ', 0};

static const uint16_t exit_attempt[] = {'e', 'x', 'i', 't', ' ', 'b', 'o', 'o', 't', ' ',
                                        'a', 't', 't', 'e', 'm', 'p', 't', ' ', 0};

static const uint16_t exit_map_size_failed[] = {'e', 'x', 'i', 't', ' ', 'm', 'a', 'p', ' ', 's', 'i',
                                                'z', 'e', ' ', 'f', 'a', 'i', 'l', 'e', 'd', ' ', 0};

static const uint16_t exit_map_alloc_failed[] = {'e', 'x', 'i', 't', ' ', 'm', 'a', 'p', ' ', 'a', 'l', 'l',
                                                 'o', 'c', ' ', 'f', 'a', 'i', 'l', 'e', 'd', ' ', 0};

static const uint16_t exit_map_read_failed[] = {'e', 'x', 'i', 't', ' ', 'm', 'a', 'p', ' ', 'r', 'e',
                                                'a', 'd', ' ', 'f', 'a', 'i', 'l', 'e', 'd', ' ', 0};

static const uint16_t exit_call_failed[] = {'e', 'x', 'i', 't', ' ', 'b', 'o', 'o', 't', ' ', 'c', 'a',
                                            'l', 'l', ' ', 'f', 'a', 'i', 'l', 'e', 'd', ' ', 0};

static const uint16_t exit_failed[] = {'e', 'x', 'i', 't', ' ', 'b', 'o',  'o',  't', ' ',
                                       'f', 'a', 'i', 'l', 'e', 'd', '\r', '\n', 0};

static const uint16_t boot_services_exited[] = {'b', 'o', 'o', 't', ' ', 's', 'e', 'r', 'v',  'i',  'c', 'e',
                                                's', ' ', 'e', 'x', 'i', 't', 'e', 'd', '\r', '\n', 0};

__attribute__((used, section(".reloc"))) static const uint32_t empty_relocation_block[] = {
    0,
    12,
    0,
};

static void print(EfiSystemTablePtr system_table, const uint16_t * message)
{
    if (system_table != 0 && system_table->console_out != 0 && system_table->console_out->output_string != 0)
    {
        system_table->console_out->output_string(system_table->console_out, message);
    }
}

static void * loader_memset(void * destination, uint8_t value, uint64_t size)
{
    uint8_t * dest = destination;
    for (uint64_t i = 0; i < size; i++)
    {
        dest[i] = value;
    }

    return destination;
}

static void print_hex(EfiSystemTablePtr system_table, uint64_t value)
{
    uint16_t buffer[19];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (uint64_t i = 0; i < 16; i++)
    {
        uint64_t shift = (15 - i) * 4;
        uint64_t digit = (value >> shift) & 0xf;
        buffer[i + 2] = digit < 10 ? (uint16_t)('0' + digit) : (uint16_t)('a' + digit - 10);
    }
    buffer[18] = 0;
    print(system_table, buffer);
}

static void print_newline(EfiSystemTablePtr system_table)
{
    static const uint16_t newline[] = {'\r', '\n', 0};
    print(system_table, newline);
}

static uint64_t align_down_4k(uint64_t value)
{
    return value & PAGE_MASK;
}

static uint64_t align_up_4k(uint64_t value)
{
    return (value + PAGE_SIZE - 1ULL) & PAGE_MASK;
}

static int is_physical_memory_type(uint32_t type)
{
    return type == EFI_LOADER_CODE || type == EFI_LOADER_DATA || type == EFI_BOOT_SERVICES_CODE ||
           type == EFI_BOOT_SERVICES_DATA || type == EFI_RUNTIME_SERVICES_CODE || type == EFI_RUNTIME_SERVICES_DATA ||
           type == EFI_CONVENTIONAL_MEMORY || type == EFI_ACPI_RECLAIM_MEMORY || type == EFI_ACPI_MEMORY_NVS;
}

static int is_free_physical_memory_type(uint32_t type)
{
    return type == EFI_CONVENTIONAL_MEMORY;
}

static void fill_framebuffer_handoff(EfiSystemTablePtr system_table, PiUefiHandoffPtr handoff)
{
    EfiGraphicsOutputProtocolPtr graphics = 0;
    EfiBootServicesPtr boot_services = system_table->boot_services;

    if (boot_services->locate_protocol == 0)
    {
        return;
    }

    EfiStatus status = boot_services->locate_protocol(&graphics_output_protocol_guid, 0, (void **)&graphics);
    if (status != EFI_SUCCESS || graphics == 0 || graphics->mode == 0 || graphics->mode->info == 0)
    {
        return;
    }

    EfiGraphicsOutputProtocolModePtr mode = graphics->mode;
    EfiGraphicsOutputModeInformationPtr info = mode->info;
    if (mode->framebuffer_base == 0 || mode->framebuffer_size == 0)
    {
        return;
    }

    handoff->framebuffer_present = 1;
    handoff->framebuffer = (PiUefiFramebuffer){ // hundelint: ignore-constructor-required
        .physical_base = mode->framebuffer_base,
        .size = mode->framebuffer_size,
        .width = info->horizontal_resolution,
        .height = info->vertical_resolution,
        .pixels_per_scanline = info->pixels_per_scanline,
        .pixel_format = info->pixel_format,
        .red_mask = info->pixel_information.red_mask,
        .green_mask = info->pixel_information.green_mask,
        .blue_mask = info->pixel_information.blue_mask,
        .reserved_mask = info->pixel_information.reserved_mask,
    };

    print(system_table, framebuffer_found);
    print_hex(system_table, handoff->framebuffer.physical_base);
    print(system_table, (const uint16_t[]){' ', 0});
    print_hex(system_table, handoff->framebuffer.size);
    print_newline(system_table);
}

static uint64_t physical_memory_end_from_map(void * memory_map, uint64_t memory_map_size, uint64_t descriptor_size)
{
    uint64_t physical_memory_end = 0;
    uint8_t * descriptor_bytes = memory_map;

    for (uint64_t offset = 0; offset + sizeof(EfiMemoryDescriptor) <= memory_map_size; offset += descriptor_size)
    {
        EfiMemoryDescriptorPtr descriptor = (EfiMemoryDescriptorPtr)(descriptor_bytes + offset);
        if (is_physical_memory_type(descriptor->type))
        {
            uint64_t descriptor_end = descriptor->physical_start + (descriptor->number_of_pages * PAGE_SIZE);
            if (descriptor_end > physical_memory_end)
            {
                physical_memory_end = descriptor_end;
            }
        }
    }

    return physical_memory_end;
}

static void fill_free_physical_regions_from_map(PiUefiHandoffPtr handoff, void * memory_map, uint64_t memory_map_size,
                                                uint64_t descriptor_size)
{
    handoff->physical_memory_region_count = 0;
    uint8_t * descriptor_bytes = memory_map;

    for (uint64_t offset = 0; offset + sizeof(EfiMemoryDescriptor) <= memory_map_size &&
                              handoff->physical_memory_region_count < PI_UEFI_MAX_PHYSICAL_MEMORY_REGIONS;
         offset += descriptor_size)
    {
        EfiMemoryDescriptorPtr descriptor = (EfiMemoryDescriptorPtr)(descriptor_bytes + offset);
        if (is_free_physical_memory_type(descriptor->type))
        {
            uint64_t region_end = descriptor->physical_start + (descriptor->number_of_pages * PAGE_SIZE);
            if (region_end > descriptor->physical_start)
            {
                uint64_t index = handoff->physical_memory_region_count;
                handoff->physical_memory_regions[index] = (PiUefiPhysicalMemoryRegion){ // hundelint: ignore-constructor-required
                    .physical_start = descriptor->physical_start,
                    .physical_end = region_end,
                };
                handoff->physical_memory_region_count++;
            }
        }
    }
}

static int is_valid_kernel_elf(ConstElf64HeaderPtr header, uint64_t file_size)
{
    uint32_t magic = ((uint32_t)header->ident[0]) | ((uint32_t)header->ident[1] << 8) |
                     ((uint32_t)header->ident[2] << 16) | ((uint32_t)header->ident[3] << 24);

    if (file_size < sizeof(Elf64Header))
    {
        return 0;
    }

    if (magic != ELF_MAGIC)
    {
        return 0;
    }

    if (header->ident[4] != ELF_CLASS_64 || header->ident[5] != ELF_DATA_LSB)
    {
        return 0;
    }

    if (header->machine != ELF_MACHINE_AARCH64)
    {
        return 0;
    }

    uint64_t program_headers_end =
        header->program_header_offset + ((uint64_t)header->program_header_entry_size * header->program_header_count);
    return header->program_header_entry_size == sizeof(Elf64ProgramHeader) && program_headers_end <= file_size;
}

static EfiStatus allocate_exact_pages(EfiBootServicesPtr boot_services, EfiPhysicalAddress start, uint64_t pages)
{
    EfiPhysicalAddress address = start;
    EfiStatus status = boot_services->allocate_pages(EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA, pages, &address);
    if (status == EFI_SUCCESS)
    {
        return address == start ? EFI_SUCCESS : EFI_ABORTED;
    }

    for (uint64_t page = 0; page < pages; page++)
    {
        address = start + (page * PAGE_SIZE);
        status = boot_services->allocate_pages(EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA, 1, &address);
        if (status != EFI_SUCCESS)
        {
            return status;
        }

        if (address != start + (page * PAGE_SIZE))
        {
            return EFI_ABORTED;
        }
    }

    return EFI_SUCCESS;
}

static EfiStatus open_kernel_file(EfiHandle image_handle, EfiSystemTablePtr system_table,
                                  EfiFileProtocolPtr * kernel_file)
{
    EfiBootServicesPtr boot_services = system_table->boot_services;
    EfiLoadedImageProtocolPtr loaded_image = 0;
    EfiSimpleFileSystemProtocolPtr file_system = 0;
    EfiFileProtocolPtr root = 0;

    EfiStatus status =
        boot_services->handle_protocol(image_handle, &loaded_image_protocol_guid, (void **)&loaded_image);
    if (status != EFI_SUCCESS)
    {
        return status;
    }

    status = boot_services->handle_protocol(loaded_image->device_handle, &simple_file_system_protocol_guid,
                                            (void **)&file_system);
    if (status != EFI_SUCCESS)
    {
        return status;
    }

    status = file_system->open_volume(file_system, &root);
    if (status != EFI_SUCCESS)
    {
        return status;
    }

    return root->open(root, kernel_file, kernel_path, EFI_FILE_MODE_READ, 0);
}

static EfiStatus kernel_file_size(EfiSystemTablePtr system_table, EfiFileProtocolPtr kernel_file, uint64_t * file_size)
{
    EfiBootServicesPtr boot_services = system_table->boot_services;
    uint64_t info_size = 0;
    EfiStatus status = kernel_file->get_info(kernel_file, &file_info_guid, &info_size, 0);
    if (status != EFI_BUFFER_TOO_SMALL)
    {
        return status;
    }

    EfiFileInfoPtr file_info = 0;
    status = boot_services->allocate_pool(EFI_LOADER_DATA, info_size, (void **)&file_info);
    if (status != EFI_SUCCESS)
    {
        return status;
    }

    status = kernel_file->get_info(kernel_file, &file_info_guid, &info_size, file_info);
    if (status == EFI_SUCCESS)
    {
        *file_size = file_info->file_size;
    }

    boot_services->free_pool(file_info);
    return status;
}

static EfiStatus load_kernel_elf(EfiSystemTablePtr system_table, EfiFileProtocolPtr kernel_file, uint64_t file_size,
                                 LoadedKernelPtr loaded_kernel)
{
    EfiBootServicesPtr boot_services = system_table->boot_services;
    Elf64Header header;
    EfiStatus status = kernel_file->set_position(kernel_file, 0);
    if (status != EFI_SUCCESS)
    {
        return status;
    }

    uint64_t read_size = sizeof(Elf64Header);
    status = kernel_file->read(kernel_file, &read_size, &header);
    if (status != EFI_SUCCESS || read_size != sizeof(Elf64Header))
    {
        print(system_table, kernel_read_failed);
        return EFI_ABORTED;
    }

    if (!is_valid_kernel_elf(&header, file_size))
    {
        print(system_table, elf_invalid);
        return EFI_LOAD_ERROR;
    }

    uint64_t program_headers_size = (uint64_t)header.program_header_entry_size * header.program_header_count;
    Elf64ProgramHeaderPtr program_headers = 0;
    status = boot_services->allocate_pool(EFI_LOADER_DATA, program_headers_size, (void **)&program_headers);
    if (status != EFI_SUCCESS)
    {
        print(system_table, kernel_alloc_failed);
        return status;
    }

    status = kernel_file->set_position(kernel_file, header.program_header_offset);
    if (status != EFI_SUCCESS)
    {
        boot_services->free_pool(program_headers);
        return status;
    }

    read_size = program_headers_size;
    status = kernel_file->read(kernel_file, &read_size, program_headers);
    if (status != EFI_SUCCESS || read_size != program_headers_size)
    {
        print(system_table, kernel_read_failed);
        boot_services->free_pool(program_headers);
        return EFI_ABORTED;
    }

    loaded_kernel->entry = 0;

    for (uint16_t i = 0; i < header.program_header_count; i++)
    {
        Elf64ProgramHeaderPtr program_header = &program_headers[i];
        if (program_header->type == ELF_PROGRAM_LOAD && program_header->memory_size > 0)
        {
            uint64_t segment_start = align_down_4k(program_header->physical_address);
            uint64_t segment_end = align_up_4k(program_header->physical_address + program_header->memory_size);
            uint64_t pages = (segment_end - segment_start) / PAGE_SIZE;
            EfiPhysicalAddress segment_address = segment_start;
            status = allocate_exact_pages(boot_services, segment_address, pages);
            if (status != EFI_SUCCESS)
            {
                print(system_table, kernel_alloc_failed);
                print_hex(system_table, program_header->physical_address);
                print(system_table, (const uint16_t[]){' ', 0});
                print_hex(system_table, status);
                print_newline(system_table);
                boot_services->free_pool(program_headers);
                return status;
            }

            if (program_header->offset + program_header->file_size > file_size)
            {
                print(system_table, elf_invalid);
                boot_services->free_pool(program_headers);
                return EFI_LOAD_ERROR;
            }

            loader_memset((void *)segment_address, 0, pages * 4096ULL);
            status = kernel_file->set_position(kernel_file, program_header->offset);
            if (status != EFI_SUCCESS)
            {
                boot_services->free_pool(program_headers);
                return status;
            }

            read_size = program_header->file_size;
            status = kernel_file->read(kernel_file, &read_size, (void *)program_header->physical_address);
            if (status != EFI_SUCCESS || read_size != program_header->file_size)
            {
                print(system_table, kernel_read_failed);
                boot_services->free_pool(program_headers);
                return EFI_ABORTED;
            }

            print(system_table, elf_segment_loaded);
            print_hex(system_table, program_header->virtual_address);
            print(system_table, (const uint16_t[]){' ', '-', '>', ' ', 0});
            print_hex(system_table, segment_address);
            print_newline(system_table);

            if (header.entry >= program_header->virtual_address &&
                header.entry < program_header->virtual_address + program_header->memory_size)
            {
                loaded_kernel->entry =
                    program_header->physical_address + (header.entry - program_header->virtual_address);
            }
        }
    }

    boot_services->free_pool(program_headers);

    if (loaded_kernel->entry == 0)
    {
        print(system_table, elf_invalid);
        return EFI_LOAD_ERROR;
    }

    print(system_table, elf_entry_loaded);
    print_hex(system_table, loaded_kernel->entry);
    print_newline(system_table);
    print(system_table, kernel_allocated);
    return EFI_SUCCESS;
}

static EfiStatus exit_boot_services(EfiHandle image_handle, EfiSystemTablePtr system_table,
                                    uint64_t * physical_memory_end, PiUefiHandoffPtr handoff)
{
    EfiBootServicesPtr boot_services = system_table->boot_services;
    EfiStatus status = EFI_LOAD_ERROR;

    for (uint64_t attempt = 0; attempt < 3 && status != EFI_SUCCESS; attempt++)
    {
        print(system_table, exit_attempt);
        print_hex(system_table, attempt);
        print_newline(system_table);

        uint64_t memory_map_size = 0;
        uint64_t map_key = 0;
        uint64_t descriptor_size = 0;
        uint32_t descriptor_version = 0;
        status = boot_services->get_memory_map(&memory_map_size, 0, &map_key, &descriptor_size, &descriptor_version);
        if (status != EFI_BUFFER_TOO_SMALL)
        {
            print(system_table, exit_map_size_failed);
            print_hex(system_table, status);
            print_newline(system_table);
            return status;
        }

        memory_map_size += descriptor_size * 8ULL;
        void * memory_map = 0;
        status = boot_services->allocate_pool(EFI_LOADER_DATA, memory_map_size, &memory_map);
        if (status != EFI_SUCCESS)
        {
            print(system_table, exit_map_alloc_failed);
            print_hex(system_table, status);
            print_newline(system_table);
            return status;
        }

        status = boot_services->get_memory_map(&memory_map_size, memory_map, &map_key, &descriptor_size,
                                               &descriptor_version);
        if (status == EFI_SUCCESS)
        {
            *physical_memory_end = physical_memory_end_from_map(memory_map, memory_map_size, descriptor_size);
            fill_free_physical_regions_from_map(handoff, memory_map, memory_map_size, descriptor_size);
            status = boot_services->exit_boot_services(image_handle, map_key);
        }
        else
        {
            print(system_table, exit_map_read_failed);
            print_hex(system_table, status);
            print_newline(system_table);
        }

        if (status != EFI_SUCCESS)
        {
            print(system_table, exit_call_failed);
            print_hex(system_table, status);
            print_newline(system_table);
            boot_services->free_pool(memory_map);
        }
    }

    return status;
}

EfiStatus efi_main(EfiHandle image_handle, EfiSystemTablePtr system_table)
{
    print(system_table, loader_start);
    loader_memset(&pi_uefi_handoff, 0, sizeof(pi_uefi_handoff));
    fill_framebuffer_handoff(system_table, &pi_uefi_handoff);

    EfiFileProtocolPtr kernel_file = 0;
    EfiStatus status = open_kernel_file(image_handle, system_table, &kernel_file);
    if (status != EFI_SUCCESS)
    {
        print(system_table, kernel_open_failed);
        return status;
    }
    print(system_table, kernel_opened);

    uint64_t file_size = 0;
    status = kernel_file_size(system_table, kernel_file, &file_size);
    if (status == EFI_SUCCESS)
    {
        print(system_table, kernel_sized);
        LoadedKernel loaded_kernel = {0}; // hundelint: ignore-constructor-required
        status = load_kernel_elf(system_table, kernel_file, file_size, &loaded_kernel);
        if (status == EFI_SUCCESS)
        {
            if (kernel_file != 0)
            {
                kernel_file->close(kernel_file);
                kernel_file = 0;
            }

            uint64_t physical_memory_end = 0;
            status = exit_boot_services(image_handle, system_table, &physical_memory_end, &pi_uefi_handoff);
            if (status != EFI_SUCCESS)
            {
                print(system_table, exit_failed);
            }
            else
            {
                KernelEntry entry = (KernelEntry)loaded_kernel.entry;
                entry((uint64_t)image_handle, (uint64_t)system_table, physical_memory_end, PI_UEFI_HANDOFF_MAGIC,
                      (uint64_t)&pi_uefi_handoff);
                return EFI_SUCCESS;
            }
        }
    }
    else
    {
        print(system_table, kernel_size_failed);
    }

    if (kernel_file != 0)
    {
        kernel_file->close(kernel_file);
    }

    if (status != EFI_SUCCESS)
    {
        print(system_table, kernel_read_failed);
        return status;
    }
    return EFI_SUCCESS;
}
