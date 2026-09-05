#define DRIVER_LOG_TAG "HUNDEBUS"

#include <stdbool.h>
#include <stdint.h>

#include "../../../common/types/dyn_string.h"
#include "sys/debug.h"
#include "sys/io_server.h"
#include "sys/pci_config.h"
#include "sys/service.h"
#include "types/fixed_array.h"
#include "../../user.h"

#ifndef HUNDEBUS_ENTRYPOINT
#define HUNDEBUS_ENTRYPOINT main
#endif

#define HUNDEBUS_BUSINFO_PATH "/dev/businfo"
#define HUNDEBUS_BUSINFO_INITIAL_CAPACITY 1024

#define PCI_VENDOR_INVALID 0xffffU
#define PCI_DEVICE_COUNT 32ULL
#define PCI_FUNCTION_COUNT 8ULL
#define PCI_ECAM_BUS_SIZE 0x100000ULL
#define PCI_ECAM_DEVICE_SIZE 0x8000ULL
#define PCI_ECAM_FUNCTION_SIZE 0x1000ULL

#define PCI_CONFIG_VENDOR_ID 0x00
#define PCI_CONFIG_DEVICE_ID 0x02
#define PCI_CONFIG_CLASS_CODE 0x0b
#define PCI_CONFIG_SUBCLASS 0x0a
#define PCI_CONFIG_PROG_IF 0x09
#define PCI_CONFIG_HEADER_TYPE 0x0e
#define PCI_HEADER_TYPE_MULTIFUNCTION 0x80U
#define PCI_CONFIG_READ_BYTES 0x10ULL

#define HUNDEBUS_MAX_PCI_DEVICES 256

typedef struct HundebusPciDevice
{
    uint64_t bus;
    uint64_t device;
    uint64_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t header_type;
}
HundebusPciDevice;

typedef HundebusPciDevice * HundebusPciDevicePtr;
typedef const HundebusPciDevice * ConstHundebusPciDevicePtr;

OptionalDef(HundebusPciDevice);

static HundebusPciDevice HundebusPciDevice__new(
    uint64_t bus,
    uint64_t device,
    uint64_t function,
    uint16_t vendor_id,
    uint16_t device_id,
    uint8_t class_code,
    uint8_t subclass,
    uint8_t prog_if,
    uint8_t header_type
)
{
    return (HundebusPciDevice) {
        .bus = bus,
        .device = device,
        .function = function,
        .vendor_id = vendor_id,
        .device_id = device_id,
        .class_code = class_code,
        .subclass = subclass,
        .prog_if = prog_if,
        .header_type = header_type,
    };
}

static bool HundebusPciDevice__equals(HundebusPciDevicePtr self, HundebusPciDevicePtr other)
{
    return self->bus == other->bus && self->device == other->device && self->function == other->function;
}

FixedArrayDefine(
    HundebusPciDevice,
    HUNDEBUS_MAX_PCI_DEVICES,
    HundebusPciDevices,
    (EqualFunction) HundebusPciDevice__equals
);
FixedArrayBodyDefine(HundebusPciDevices);

typedef struct Hundebus
{
    PciConfig pci_config;
    Optional(ServiceMappedMemoryPtr) ecam;
    HundebusPciDevices pci_devices;
    DynString businfo;
    IOHandle businfo_handle;
}
Hundebus;

typedef Hundebus * HundebusPtr;

static Hundebus Hundebus__new(void)
{
    return (Hundebus) {
        .pci_config = { 0 },
        .ecam = None(ServiceMappedMemoryPtr),
        .pci_devices = HundebusPciDevices__new(),
        .businfo = { 0 },
        .businfo_handle = -1,
    };
}

#if !defined(HUNDEBUS_PCI_READ8) || !defined(HUNDEBUS_PCI_READ16)
static volatile uint8_t * Hundebus__reg8(void * base, uint64_t offset)
{
    return (volatile uint8_t *) ((uint64_t) base + offset);
}

static volatile uint16_t * Hundebus__reg16(void * base, uint64_t offset)
{
    return (volatile uint16_t *) ((uint64_t) base + offset);
}
#endif

#ifndef HUNDEBUS_PCI_READ8
#define HUNDEBUS_PCI_READ8(base, offset) (*Hundebus__reg8((base), (offset)))
#endif

#ifndef HUNDEBUS_PCI_READ16
#define HUNDEBUS_PCI_READ16(base, offset) (*Hundebus__reg16((base), (offset)))
#endif

static uint64_t Hundebus__config_offset(PciConfigPtr config, uint64_t bus, uint64_t device, uint64_t function)
{
    return ((bus - config->bus_start) * PCI_ECAM_BUS_SIZE)
        + (device * PCI_ECAM_DEVICE_SIZE)
        + (function * PCI_ECAM_FUNCTION_SIZE);
}

static bool Hundebus__config_range_available(HundebusPtr self, uint64_t offset, uint64_t byte_count)
{
    match_optional(ServiceMappedMemoryPtr, self->ecam,
        some(ecam)
        {
            size_t mapped_ecam_size = mapped_size(ecam);
            return offset <= self->pci_config.ecam_size
                && byte_count <= self->pci_config.ecam_size - offset
                && offset <= mapped_ecam_size
                && byte_count <= mapped_ecam_size - offset;
        },
        none() { return false; }
    );
}

static bool Hundebus__read_pci_device(
    HundebusPtr self,
    uint64_t bus,
    uint64_t device,
    uint64_t function,
    HundebusPciDevicePtr target
)
{
    uint64_t function_offset = Hundebus__config_offset(&self->pci_config, bus, device, function);
    if (!Hundebus__config_range_available(self, function_offset, PCI_CONFIG_READ_BYTES))
    {
        return false;
    }

    match_optional(ServiceMappedMemoryPtr, self->ecam,
        some(ecam)
        {
            void * function_base = driver_ptr_offset(ecam, function_offset);
            uint16_t vendor_id = HUNDEBUS_PCI_READ16(function_base, PCI_CONFIG_VENDOR_ID);
            if (vendor_id == PCI_VENDOR_INVALID)
            {
                return false;
            }

            *target = HundebusPciDevice__new(
                bus,
                device,
                function,
                vendor_id,
                HUNDEBUS_PCI_READ16(function_base, PCI_CONFIG_DEVICE_ID),
                HUNDEBUS_PCI_READ8(function_base, PCI_CONFIG_CLASS_CODE),
                HUNDEBUS_PCI_READ8(function_base, PCI_CONFIG_SUBCLASS),
                HUNDEBUS_PCI_READ8(function_base, PCI_CONFIG_PROG_IF),
                HUNDEBUS_PCI_READ8(function_base, PCI_CONFIG_HEADER_TYPE)
            );

            return true;
        },
        none() { return false; }
    );
}

static bool Hundebus__record_pci_device(HundebusPtr self, ConstHundebusPciDevicePtr device)
{
    match_result(CollectionCode, HundebusPciDevices__push(&self->pci_devices, *device),
        success(code) { return true; },
        error(code, message) { return false; }
    );
    return false;
}

static uint64_t Hundebus__scan_pci_device(HundebusPtr self, uint64_t bus, uint64_t device)
{
    HundebusPciDevice pci_device;
    if (!Hundebus__read_pci_device(self, bus, device, 0, &pci_device))
    {
        return 0;
    }

    uint64_t found = 1;
    Hundebus__record_pci_device(self, &pci_device);

    if ((pci_device.header_type & PCI_HEADER_TYPE_MULTIFUNCTION) == 0)
    {
        return found;
    }

    for (uint64_t function = 1; function < PCI_FUNCTION_COUNT; function++)
    {
        if (Hundebus__read_pci_device(self, bus, device, function, &pci_device))
        {
            found++;
            Hundebus__record_pci_device(self, &pci_device);
        }
    }

    return found;
}

static uint64_t Hundebus__scan_pci(HundebusPtr self)
{
    uint64_t found = 0;
    for (uint64_t bus = self->pci_config.bus_start; bus <= self->pci_config.bus_end; bus++)
    {
        for (uint64_t device = 0; device < PCI_DEVICE_COUNT; device++)
        {
            found += Hundebus__scan_pci_device(self, bus, device);
        }
    }

    return found;
}

static bool Hundebus__append_line(HundebusPtr self, ConstBStringPtr format, uint64_t value)
{
    BString line = BString__sprintf(format, value);
    match_result(DynStringCode, DynString__append_bstring(&self->businfo, &line),
        success(code) { return true; },
        error(code, message) { return false; }
    );
    return false;
}

static bool Hundebus__append_device_line(HundebusPtr self, ConstBStringPtr format, uint64_t index, uint64_t value)
{
    BString line = BString__sprintf(format, index, value);
    match_result(DynStringCode, DynString__append_bstring(&self->businfo, &line),
        success(code) { return true; },
        error(code, message) { return false; }
    );
    return false;
}

static bool Hundebus__append_pci_device(HundebusPtr self, uint64_t index, HundebusPciDevicePtr device)
{

    BString bdf = BString__sprintf(&bstr("pci.device.%llu.bdf=%llu:%llu.%llu\n"), index, device->bus, device->device, device->function);
    match_result(DynStringCode, DynString__append_bstring(&self->businfo, &bdf),
        success_do_nothing(),
        error(code, message) { return false; }
    );

    return Hundebus__append_device_line(self, &bstr("pci.device.%llu.vendor_id=0x%llx\n"), index, device->vendor_id)
        && Hundebus__append_device_line(self, &bstr("pci.device.%llu.device_id=0x%llx\n"), index, device->device_id)
        && Hundebus__append_device_line(self, &bstr("pci.device.%llu.class=0x%llx\n"), index, device->class_code)
        && Hundebus__append_device_line(self, &bstr("pci.device.%llu.subclass=0x%llx\n"), index, device->subclass)
        && Hundebus__append_device_line(self, &bstr("pci.device.%llu.prog_if=0x%llx\n"), index, device->prog_if)
        && Hundebus__append_device_line(self, &bstr("pci.device.%llu.header=0x%llx\n"), index, device->header_type);
}

static bool Hundebus__build_businfo(HundebusPtr self)
{

    DynString__clear(&self->businfo);
    if (!Hundebus__append_line(self, &bstr("pci.present=%llu\n"), 1))
    {
        return false;
    }
    if (!Hundebus__append_line(self, &bstr("pci.device.count=%llu\n"), HundebusPciDevices__len(&self->pci_devices)))
    {
        return false;
    }

    HundebusPciDevices * devices = &self->pci_devices;
    for_each_fa_ptr(HundebusPciDevices, devices, index, device,
    {
        if (!Hundebus__append_pci_device(self, index, &device))
        {
            return false;
        }
    });

    return true;
}

static uint64_t Hundebus__copy_businfo(HundebusPtr self, IOServerBufferPtr buffer)
{
    const char * text = DynString__cstr(&self->businfo);
    StringPos length = DynString__length(&self->businfo);
    uint64_t bytes = buffer->capacity < (uint64_t) length ? buffer->capacity : (uint64_t) length;
    for (uint64_t index = 0; index < bytes; index++)
    {
        buffer->data[index] = text[index];
    }
    return bytes;
}

static void Hundebus__publish_businfo(HundebusPtr self)
{
    IOServerBuffer buffer;
    IOStatus status = io_server_get_buffer(self->businfo_handle, &buffer);
    if (status != IO_OK)
    {
        IO__printf(&bstr("hundebus: get buffer error %i\n"), (int) status);
        return;
    }

    uint64_t byte_count = Hundebus__copy_businfo(self, &buffer);
    status = io_server_data_available(self->businfo_handle, byte_count);
    if (status != IO_OK)
    {
        IO__printf(&bstr("hundebus: data available error %i\n"), (int) status);
        return;
    }

    status = io_server_eof(self->businfo_handle);
    if (status != IO_OK)
    {
        IO__printf(&bstr("hundebus: eof error %i\n"), (int) status);
    }
}

static bool Hundebus__init(HundebusPtr self)
{
    *self = Hundebus__new();

    match_result(DynStringCode, DynString__init(&self->businfo, HUNDEBUS_BUSINFO_INITIAL_CAPACITY),
        success_do_nothing(),
        error(code, message) { return false; }
    );

    if (!service_ok(service_become_service()))
    {
        DRIVER_DEBUG("hundebus: service registration failed\n");
        return false;
    }

    if (!PciConfig__load(&self->pci_config))
    {
        DRIVER_DEBUG("hundebus: sysinfo read failed\n");
        return false;
    }

    ServiceMappedMemoryPtr ecam;
    if (!service_ok(service_map_mmio(self->pci_config.ecam_physical_base, self->pci_config.ecam_size, &ecam)))
    {
        DRIVER_DEBUG("hundebus: ecam map failed\n");
        return false;
    }
    self->ecam = Some(ServiceMappedMemoryPtr, ecam);

    uint64_t found = Hundebus__scan_pci(self);
    DRIVER_DEBUG("hundebus: found %i pci device functions\n", (int) found);

    if (!Hundebus__build_businfo(self))
    {
        DRIVER_DEBUG("hundebus: businfo build failed\n");
        return false;
    }

    self->businfo_handle = io_server_register(&bstr(HUNDEBUS_BUSINFO_PATH), IO_SERVER_READ);
    if (self->businfo_handle < 0)
    {
        IO__printf(&bstr("hundebus: businfo register error %i\n"), (int) self->businfo_handle);
        return false;
    }

    return true;
}

int HUNDEBUS_ENTRYPOINT(void)
{
    Hundebus hundebus;
    if (!Hundebus__init(&hundebus))
    {
        return 1;
    }

    ServiceEvent event;
    for (ServiceCode status = service_wait(&event); service_alive(status); status = service_wait(&event))
    {
        if (event.type == SERVICE_EVENT_IO && event.source == SERVICE_IO_READ_REQUEST)
        {
            Hundebus__publish_businfo(&hundebus);
        }
    }

    DynString__dispose(&hundebus.businfo);
    return 0;
}
