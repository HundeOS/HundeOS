#define DRIVER_LOG_TAG "HUNDEBUS"

#include <stdbool.h>
#include <stdint.h>

#include "bus/info.h"
#include "sys/debug.h"
#include "sys/pci_config.h"
#include "sys/service.h"
#include "types/fixed_array.h"
#include "../../user.h"

#ifndef HUNDEBUS_ENTRYPOINT
#define HUNDEBUS_ENTRYPOINT main
#endif

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

static BusPciDevice BusPciDevice__new(uint64_t bus, uint64_t device, uint64_t function, uint16_t vendor_id,
                                      uint16_t device_id, uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                                      uint8_t header_type)
{
    return (BusPciDevice){
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

typedef struct Hundebus
{
    PciConfig pci_config;
    Optional(ServiceMappedMemoryPtr) ecam;
    BusPciDevices pci_devices;
} Hundebus;

typedef Hundebus * HundebusPtr;

static Hundebus Hundebus__new(void)
{
    return (Hundebus){
        .pci_config = {0},
        .ecam = None(ServiceMappedMemoryPtr),
        .pci_devices = BusPciDevices__new(),
    };
}

#if !defined(HUNDEBUS_PCI_READ8) || !defined(HUNDEBUS_PCI_READ16)
static volatile uint8_t * Hundebus__reg8(void * base, uint64_t offset)
{
    return (volatile uint8_t *)((uint64_t)base + offset);
}

static volatile uint16_t * Hundebus__reg16(void * base, uint64_t offset)
{
    return (volatile uint16_t *)((uint64_t)base + offset);
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
    return ((bus - config->bus_start) * PCI_ECAM_BUS_SIZE) + (device * PCI_ECAM_DEVICE_SIZE) +
           (function * PCI_ECAM_FUNCTION_SIZE);
}

static bool Hundebus__config_range_available(HundebusPtr self, uint64_t offset, uint64_t byte_count)
{
    match_optional(
        self->ecam,
        some(ecam)
        {
            size_t mapped_ecam_size = mapped_size(ecam);
            return offset <= self->pci_config.ecam_size && byte_count <= self->pci_config.ecam_size - offset &&
                   offset <= mapped_ecam_size && byte_count <= mapped_ecam_size - offset;
        },
        none()
        {
            return false;
        }
    );
}

static Optional(BusPciDevice)
    Hundebus__read_pci_device(HundebusPtr self, uint64_t bus, uint64_t device, uint64_t function)
{
    uint64_t function_offset = Hundebus__config_offset(&self->pci_config, bus, device, function);
    if (!Hundebus__config_range_available(self, function_offset, PCI_CONFIG_READ_BYTES))
    {
        return None(BusPciDevice);
    }

    match_optional(
        self->ecam,
        some(ecam)
        {
            void * function_base = driver_ptr_offset(ecam, function_offset);
            uint16_t vendor_id = HUNDEBUS_PCI_READ16(function_base, PCI_CONFIG_VENDOR_ID);
            if (vendor_id == PCI_VENDOR_INVALID)
            {
                return None(BusPciDevice);
            }

            BusPciDevice result = BusPciDevice__new(bus, device, function, vendor_id,
                                                    HUNDEBUS_PCI_READ16(function_base, PCI_CONFIG_DEVICE_ID),
                                                    HUNDEBUS_PCI_READ8(function_base, PCI_CONFIG_CLASS_CODE),
                                                    HUNDEBUS_PCI_READ8(function_base, PCI_CONFIG_SUBCLASS),
                                                    HUNDEBUS_PCI_READ8(function_base, PCI_CONFIG_PROG_IF),
                                                    HUNDEBUS_PCI_READ8(function_base, PCI_CONFIG_HEADER_TYPE));

            return Some(BusPciDevice, result);
        },
        none()
        {
            return None(BusPciDevice);
        }
    );
}

static bool Hundebus__record_pci_device(HundebusPtr self, ConstBusPciDevicePtr device)
{
    match_result(
        BusPciDevices__push(&self->pci_devices, *device),
        success(code)
        {
            return true;
        },
        error(code, message)
        {
            return false;
        }
    );
    return false;
}

static uint64_t Hundebus__scan_pci_device(HundebusPtr self, uint64_t bus, uint64_t device)
{
    BusPciDevice pci_device;
    match_optional(
        Hundebus__read_pci_device(self, bus, device, 0),
        some(found)
        {
            pci_device = found;
        },
        none()
        {
            return 0;
        }
    );

    uint64_t found = 1;
    Hundebus__record_pci_device(self, &pci_device);

    if ((pci_device.header_type & PCI_HEADER_TYPE_MULTIFUNCTION) == 0)
    {
        return found;
    }

    for (uint64_t function = 1; function < PCI_FUNCTION_COUNT; function++)
    {
        match_optional(
            Hundebus__read_pci_device(self, bus, device, function),
            some(pci_function)
            {
                found++;
                Hundebus__record_pci_device(self, &pci_function);
            },
            none_do_nothing()
        );
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

static bool Hundebus__init(HundebusPtr self)
{
    *self = Hundebus__new();

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
    DRIVER_DEBUG("hundebus: found %i pci device functions\n", (int)found);

    if (found != BusPciDevices__len(&self->pci_devices))
    {
        DRIVER_DEBUG("hundebus: PCI inventory capacity exceeded\n");
        return false;
    }

    return true;
}

int HUNDEBUS_ENTRYPOINT(void)
{
    static Hundebus hundebus;
    if (!Hundebus__init(&hundebus))
    {
        return 1;
    }

    static IOService service;
    if (IOService__listen_into(&service, &bstr(BUS_INFO_SERVICE), &hundebus.pci_devices, BusInfo__execute,
                               BusInfo__disconnect) != IO_OK)
    {
        return 1;
    }
    if (!service_ok(service_publish_ready(&bstr(BUS_INFO_SERVICE))))
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
