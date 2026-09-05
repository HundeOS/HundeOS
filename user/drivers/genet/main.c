#define DRIVER_LOG_TAG "GENET"

#include <stdint.h>

#include "types/result.h"
#include "hardware/ethernet.h"
#include "sys/debug.h"
#include "sys/ethernet_config.h"
#include "sys/service.h"
#include "sys/sync.h"

#define GENET_READY_PATH "/dev/eth0"
#define GENET_SYS_REV_CTRL 0x0000
#define GENET_SYS_PORT_CTRL 0x0004
#define GENET_VERSION_MAJOR_SHIFT 24U
#define GENET_VERSION_MAJOR_MASK 0x0f000000U
#define GENET_SUPPORTED_MAJOR_VERSION 5U

typedef void * GenetMmioPtr;
typedef volatile uint32_t * GenetRegister32Ptr;

typedef struct GenetDevice
{
    ServiceMappedMemoryPtr mapping;
    GenetMmioPtr mmio;
    uint64_t irq;
    BString path;
}
GenetDevice;

typedef GenetDevice * GenetDevicePtr;

ResultDef(GenetDevice);

static BString genet_error_service_registration_failed = bstr("genet service registration failed");
static BString genet_error_config_not_present = bstr("genet ethernet config not present");
static BString genet_error_unsupported_config = bstr("genet unsupported ethernet config");
static BString genet_error_mmio_map_failed = bstr("genet mmio map failed");
static BString genet_error_unsupported_version = bstr("genet unsupported version");

static GenetDevice GenetDevice__new(
    ServiceMappedMemoryPtr mapping,
    GenetMmioPtr mmio,
    uint64_t irq,
    ConstBStringPtr path)
{
    return (GenetDevice) {
        .mapping = mapping,
        .mmio = mmio,
        .irq = irq,
        .path = *path,
    };
}

static GenetRegister32Ptr Genet__reg32(GenetMmioPtr base, uint64_t offset)
{
    return (GenetRegister32Ptr) ((uint64_t) base + offset);
}

static uint32_t Genet__read32(GenetMmioPtr base, uint64_t offset)
{
    return *Genet__reg32(base, offset);
}

static uint32_t Genet__major_version(uint32_t sys_rev_ctrl)
{
    return (sys_rev_ctrl & GENET_VERSION_MAJOR_MASK) >> GENET_VERSION_MAJOR_SHIFT;
}

static bool Genet__version_supported(uint32_t sys_rev_ctrl)
{
    return Genet__major_version(sys_rev_ctrl) == GENET_SUPPORTED_MAJOR_VERSION;
}

static bool Genet__config_supported(EthernetConfigPtr config)
{
    return config->mac_kind == ETHERNET_MAC_KIND_BCM2711_GENET
        && config->physical_base != 0
        && config->mmio_size >= 0x10000;
}

static Result(GenetDevice) Genet__init(void)
{
    if (!service_ok(service_become_service()))
    {
        DRIVER_DEBUG("genet: service registration failed\n");
        return Error(GenetDevice, genet_error_service_registration_failed, SERVICE_NOT_SERVICE_PROCESS);
    }

    match_optional(EthernetConfig, EthernetConfig__load(),
        some(config)
        {
            if (!Genet__config_supported(&config))
            {
                DRIVER_DEBUG("genet: unsupported ethernet config\n");
                return Error(GenetDevice, genet_error_unsupported_config, SERVICE_OPERATION_NOT_SUPPORTED);
            }

            ServiceMappedMemoryPtr mapping;
            if (!service_ok(service_map_mmio(config.physical_base, config.mmio_size, &mapping)))
            {
                DRIVER_DEBUG("genet: mmio map failed\n");
                return Error(GenetDevice, genet_error_mmio_map_failed, SERVICE_MAP_FAILED);
            }

            GenetMmioPtr mmio = driver_ptr(mapping);
            uint32_t sys_rev_ctrl = Genet__read32(mmio, GENET_SYS_REV_CTRL);
            if (!Genet__version_supported(sys_rev_ctrl))
            {
                DRIVER_DEBUG("genet: unsupported version 0x%x\n", sys_rev_ctrl);
                return Error(GenetDevice, genet_error_unsupported_version, SERVICE_OPERATION_NOT_SUPPORTED);
            }

            return Success(GenetDevice, GenetDevice__new(mapping, mmio, config.irq, &bstr(GENET_READY_PATH)));
        },
        none()
        {
            DRIVER_DEBUG("genet: ethernet config not present\n");
            return Error(GenetDevice, genet_error_config_not_present, SERVICE_OPERATION_NOT_SUPPORTED);
        }
    );

    return Error(GenetDevice, genet_error_config_not_present, SERVICE_OPERATION_NOT_SUPPORTED);
}

int main(void)
{
    GenetDevice device;
    match_result(GenetDevice, Genet__init(),
        success(value) { device = value; },
        error(code, message) { return 1; }
    );

    if (!service_ok(service_publish_ready(&device.path)))
    {
        DRIVER_DEBUG("genet: ready publish failed\n");
        return 1;
    }

    ServiceEvent event;
    for (ServiceCode status = service_wait(&event); service_alive(status); status = service_wait(&event))
    {
    }

    return 0;
}
