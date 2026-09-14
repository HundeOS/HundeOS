#include "channel/block_server.h"
#define DRIVER_LOG_TAG "SDHCI"

#include <stdbool.h>
#include <stdint.h>

#include "../../../common/types/bstring.h"
#include "sys/debug.h"
#include "sys/pci_config.h"
#include "sys/sd_host_config.h"
#include "sys/service.h"
#include "sys/sync.h"
#include "channel/signal.h"

#ifndef SDHCI_ENTRYPOINT
#define SDHCI_ENTRYPOINT main
#endif

#ifndef SDHCI_LOAD_CONFIG
#define SDHCI_LOAD_CONFIG SdHostConfig__load
#endif

#ifndef SDHCI_LOAD_PCI_CONFIG
#define SDHCI_LOAD_PCI_CONFIG PciConfig__load
#endif

#define SDHCI_DEVICE_PATH "/dev/mmc0"
#define SDHCI_BLOCK_SIZE 512ULL
#define SDHCI_POLL_LIMIT 1000000ULL
#define SDHCI_APP_CMD_RETRIES 1000ULL

#define SDHCI_PCI_DEVICE_COUNT 32ULL
#define SDHCI_PCI_FUNCTION_COUNT 8ULL
#define SDHCI_PCI_ECAM_BUS_SIZE 0x100000ULL
#define SDHCI_PCI_ECAM_DEVICE_SIZE 0x8000ULL
#define SDHCI_PCI_ECAM_FUNCTION_SIZE 0x1000ULL
#define SDHCI_PCI_MMIO_SIZE 0x1000ULL

#define SDHCI_PCI_VENDOR_INVALID 0xffffU
#define SDHCI_PCI_CONFIG_VENDOR_ID 0x00
#define SDHCI_PCI_CONFIG_COMMAND 0x04
#define SDHCI_PCI_CONFIG_PROG_IF 0x09
#define SDHCI_PCI_CONFIG_SUBCLASS 0x0a
#define SDHCI_PCI_CONFIG_CLASS_CODE 0x0b
#define SDHCI_PCI_CONFIG_HEADER_TYPE 0x0e
#define SDHCI_PCI_CONFIG_BAR0 0x10
#define SDHCI_PCI_CONFIG_INTERRUPT_LINE 0x3c
#define SDHCI_PCI_HEADER_TYPE_MULTIFUNCTION 0x80U
#define SDHCI_PCI_COMMAND_MEMORY_SPACE 0x0002U
#define SDHCI_PCI_COMMAND_BUS_MASTER 0x0004U
#define SDHCI_PCI_BAR_IO_SPACE 0x00000001U
#define SDHCI_PCI_BAR_MEMORY_TYPE_MASK 0x00000006U
#define SDHCI_PCI_BAR_MEMORY_64 0x00000004U
#define SDHCI_PCI_BAR_MEMORY_ADDRESS_MASK 0xfffffff0U
#define SDHCI_PCI_CLASS_SYSTEM_PERIPHERAL 0x08U
#define SDHCI_PCI_SUBCLASS_SD_HOST 0x05U

#define SDHCI_DMA_ADDRESS 0x00
#define SDHCI_BLOCK_SIZE_REG 0x04
#define SDHCI_BLOCK_COUNT_REG 0x06
#define SDHCI_ARGUMENT 0x08
#define SDHCI_TRANSFER_MODE 0x0c
#define SDHCI_COMMAND 0x0e
#define SDHCI_RESPONSE 0x10
#define SDHCI_BUFFER 0x20
#define SDHCI_PRESENT_STATE 0x24
#define SDHCI_HOST_CONTROL 0x28
#define SDHCI_POWER_CONTROL 0x29
#define SDHCI_CLOCK_CONTROL 0x2c
#define SDHCI_TIMEOUT_CONTROL 0x2e
#define SDHCI_SOFTWARE_RESET 0x2f
#define SDHCI_INT_STATUS 0x30
#define SDHCI_INT_ENABLE 0x34
#define SDHCI_SIGNAL_ENABLE 0x38
#define SDHCI_CAPABILITIES 0x40
#define SDHCI_HOST_VERSION 0xfc

#define SDHCI_PRESENT_CMD_INHIBIT 0x00000001U
#define SDHCI_PRESENT_DAT_INHIBIT 0x00000002U
#define SDHCI_PRESENT_CARD_INSERTED 0x00010000U

#define SDHCI_POWER_ON 0x01U
#define SDHCI_POWER_330 0x0eU

#define SDHCI_CLOCK_INTERNAL_ENABLE 0x0001U
#define SDHCI_CLOCK_INTERNAL_STABLE 0x0002U
#define SDHCI_CLOCK_SD_ENABLE 0x0004U

#define SDHCI_RESET_ALL 0x01U
#define SDHCI_RESET_CMD 0x02U
#define SDHCI_RESET_DATA 0x04U

#define SDHCI_INT_COMMAND_COMPLETE 0x00000001U
#define SDHCI_INT_TRANSFER_COMPLETE 0x00000002U
#define SDHCI_INT_BUFFER_READ_READY 0x00000020U
#define SDHCI_INT_ERROR 0x00008000U
#define SDHCI_INT_ALL 0xffffffffU

#define SDHCI_CMD_RESP_NONE 0x0000U
#define SDHCI_CMD_RESP_136 0x0001U
#define SDHCI_CMD_RESP_48 0x0002U
#define SDHCI_CMD_RESP_48_BUSY 0x0003U
#define SDHCI_CMD_CRC_CHECK 0x0008U
#define SDHCI_CMD_INDEX_CHECK 0x0010U
#define SDHCI_CMD_DATA_PRESENT 0x0020U

#define SDHCI_TRANSFER_BLOCK_COUNT_ENABLE 0x0002U
#define SDHCI_TRANSFER_READ 0x0010U
#define SDHCI_TRANSFER_MULTI_BLOCK 0x0020U

#define SD_CMD_GO_IDLE_STATE 0U
#define SD_CMD_SEND_IF_COND 8U
#define SD_CMD_SEND_CSD 9U
#define SD_CMD_STOP_TRANSMISSION 12U
#define SD_CMD_SET_BLOCKLEN 16U
#define SD_CMD_READ_SINGLE_BLOCK 17U
#define SD_CMD_READ_MULTIPLE_BLOCK 18U
#define SD_CMD_APP_CMD 55U
#define SD_CMD_ALL_SEND_CID 2U
#define SD_CMD_SEND_RELATIVE_ADDR 3U
#define SD_CMD_SELECT_CARD 7U
#define SD_ACMD_SD_SEND_OP_COND 41U

#define SD_OCR_READY 0x80000000U
#define SD_OCR_HIGH_CAPACITY 0x40000000U
#define SD_ACMD41_ARGUMENT 0x40300000U

typedef void * SdhciMmioPtr;
typedef volatile uint8_t * SdhciRegister8Ptr;
typedef volatile uint16_t * SdhciRegister16Ptr;
typedef volatile uint32_t * SdhciRegister32Ptr;
typedef volatile uint8_t * SdhciPciConfig8Ptr;
typedef volatile uint16_t * SdhciPciConfig16Ptr;
typedef volatile uint32_t * SdhciPciConfig32Ptr;

typedef struct SdhciCommand
{
    uint32_t index;
    uint32_t argument;
    uint16_t response_type;
    uint16_t transfer_mode;
    bool data_present;
} SdhciCommand;
typedef const SdhciCommand * ConstSdhciCommandPtr;

typedef struct SdhciCommandResult
{
    bool ok;
    uint32_t response[4];
} SdhciCommandResult;
typedef const SdhciCommandResult * ConstSdhciCommandResultPtr;

typedef struct SdhciDevice
{
    ServiceMappedMemoryPtr mapping;
    SdhciMmioPtr mmio;
    uint64_t irq;
    uint16_t rca;
    bool high_capacity;
    uint64_t block_count;
    BString path;
} SdhciDevice;

typedef SdhciDevice * SdhciDevicePtr;
typedef const SdhciDevice * ConstSdhciDevicePtr;

typedef struct SdhciPciFunction
{
    uint64_t bus;
    uint64_t device;
    uint64_t function;
} SdhciPciFunction;

typedef SdhciPciFunction * SdhciPciFunctionPtr;
typedef const SdhciPciFunction * ConstSdhciPciFunctionPtr;

OptionalDef(SdhciPciFunction);

static SdhciCommand SdhciCommand__new(uint32_t index, uint32_t argument, uint16_t response_type, uint16_t transfer_mode,
                                      bool data_present)
{
    return (SdhciCommand){
        .index = index,
        .argument = argument,
        .response_type = response_type,
        .transfer_mode = transfer_mode,
        .data_present = data_present,
    };
}

static SdhciCommandResult SdhciCommandResult__new(bool ok, uint32_t response0, uint32_t response1, uint32_t response2,
                                                  uint32_t response3)
{
    return (SdhciCommandResult){
        .ok = ok,
        .response = {response0, response1, response2, response3},
    };
}

static SdhciDevice SdhciDevice__new(ServiceMappedMemoryPtr mapping, SdhciMmioPtr mmio, uint64_t irq,
                                    ConstBStringPtr path)
{
    return (SdhciDevice){
        .mapping = mapping,
        .mmio = mmio,
        .irq = irq,
        .path = *path,
    };
}

static SdhciDevice SdhciDevice__new_empty(void)
{
    return SdhciDevice__new(0, 0, 0, &bstr(""));
}

static SdhciPciFunction SdhciPciFunction__new(uint64_t bus, uint64_t device, uint64_t function)
{
    return (SdhciPciFunction){
        .bus = bus,
        .device = device,
        .function = function,
    };
}

#if !defined(SDHCI_READ8) || !defined(SDHCI_WRITE8)
static SdhciRegister8Ptr Sdhci__reg8(SdhciMmioPtr base, uint64_t offset)
{
    return (SdhciRegister8Ptr)((uint64_t)base + offset);
}
#endif

#if !defined(SDHCI_READ16) || !defined(SDHCI_WRITE16)
static SdhciRegister16Ptr Sdhci__reg16(SdhciMmioPtr base, uint64_t offset)
{
    return (SdhciRegister16Ptr)((uint64_t)base + offset);
}
#endif

#if !defined(SDHCI_READ32) || !defined(SDHCI_WRITE32)
static SdhciRegister32Ptr Sdhci__reg32(SdhciMmioPtr base, uint64_t offset)
{
    return (SdhciRegister32Ptr)((uint64_t)base + offset);
}
#endif

#ifndef SDHCI_READ8
#define SDHCI_READ8(base, offset) (*Sdhci__reg8((base), (offset)))
#endif

#ifndef SDHCI_WRITE8
#define SDHCI_WRITE8(base, offset, value) (*Sdhci__reg8((base), (offset)) = (uint8_t)(value))
#endif

#ifndef SDHCI_READ16
#define SDHCI_READ16(base, offset) (*Sdhci__reg16((base), (offset)))
#endif

#ifndef SDHCI_WRITE16
#define SDHCI_WRITE16(base, offset, value) (*Sdhci__reg16((base), (offset)) = (uint16_t)(value))
#endif

#ifndef SDHCI_READ32
#define SDHCI_READ32(base, offset) (*Sdhci__reg32((base), (offset)))
#endif

#ifndef SDHCI_WRITE32
#define SDHCI_WRITE32(base, offset, value) (*Sdhci__reg32((base), (offset)) = (uint32_t)(value))
#endif

static SdhciPciConfig8Ptr Sdhci__pci_reg8(void * base, uint64_t offset)
{
    return (SdhciPciConfig8Ptr)((uint64_t)base + offset);
}

static SdhciPciConfig16Ptr Sdhci__pci_reg16(void * base, uint64_t offset)
{
    return (SdhciPciConfig16Ptr)((uint64_t)base + offset);
}

static SdhciPciConfig32Ptr Sdhci__pci_reg32(void * base, uint64_t offset)
{
    return (SdhciPciConfig32Ptr)((uint64_t)base + offset);
}

#ifndef SDHCI_PCI_READ8
#define SDHCI_PCI_READ8(base, offset) (*Sdhci__pci_reg8((base), (offset)))
#endif

#ifndef SDHCI_PCI_READ16
#define SDHCI_PCI_READ16(base, offset) (*Sdhci__pci_reg16((base), (offset)))
#endif

#ifndef SDHCI_PCI_READ32
#define SDHCI_PCI_READ32(base, offset) (*Sdhci__pci_reg32((base), (offset)))
#endif

#ifndef SDHCI_PCI_WRITE16
#define SDHCI_PCI_WRITE16(base, offset, value) (*Sdhci__pci_reg16((base), (offset)) = (uint16_t)(value))
#endif

static uint64_t Sdhci__pci_config_offset(PciConfigPtr config, ConstSdhciPciFunctionPtr function)
{
    return ((function->bus - config->bus_start) * SDHCI_PCI_ECAM_BUS_SIZE) +
           (function->device * SDHCI_PCI_ECAM_DEVICE_SIZE) + (function->function * SDHCI_PCI_ECAM_FUNCTION_SIZE);
}

static void * Sdhci__pci_function_base(ServiceMappedMemoryPtr ecam, PciConfigPtr config,
                                       ConstSdhciPciFunctionPtr function)
{
    return driver_ptr_offset(ecam, Sdhci__pci_config_offset(config, function));
}

static bool Sdhci__pci_function_present(void * function_base)
{
    return SDHCI_PCI_READ16(function_base, SDHCI_PCI_CONFIG_VENDOR_ID) != SDHCI_PCI_VENDOR_INVALID;
}

static bool Sdhci__pci_function_is_sdhci(void * function_base)
{
    return SDHCI_PCI_READ8(function_base, SDHCI_PCI_CONFIG_CLASS_CODE) == SDHCI_PCI_CLASS_SYSTEM_PERIPHERAL &&
           SDHCI_PCI_READ8(function_base, SDHCI_PCI_CONFIG_SUBCLASS) == SDHCI_PCI_SUBCLASS_SD_HOST;
}

static Optional(SdhciPciFunction)
    Sdhci__find_pci_function_on_device(ServiceMappedMemoryPtr ecam, PciConfigPtr config, uint64_t bus, uint64_t device)
{
    SdhciPciFunction first_function = SdhciPciFunction__new(bus, device, 0);
    void * first_base = Sdhci__pci_function_base(ecam, config, &first_function);
    if (!Sdhci__pci_function_present(first_base))
    {
        return None(SdhciPciFunction);
    }

    if (Sdhci__pci_function_is_sdhci(first_base))
    {
        return Some(SdhciPciFunction, first_function);
    }

    if ((SDHCI_PCI_READ8(first_base, SDHCI_PCI_CONFIG_HEADER_TYPE) & SDHCI_PCI_HEADER_TYPE_MULTIFUNCTION) == 0)
    {
        return None(SdhciPciFunction);
    }

    for (uint64_t function = 1; function < SDHCI_PCI_FUNCTION_COUNT; function++)
    {
        SdhciPciFunction candidate = SdhciPciFunction__new(bus, device, function);
        void * candidate_base = Sdhci__pci_function_base(ecam, config, &candidate);
        if (Sdhci__pci_function_present(candidate_base) && Sdhci__pci_function_is_sdhci(candidate_base))
        {
            return Some(SdhciPciFunction, candidate);
        }
    }

    return None(SdhciPciFunction);
}

static Optional(SdhciPciFunction) Sdhci__find_pci_function(ServiceMappedMemoryPtr ecam, PciConfigPtr config)
{
    for (uint64_t bus = config->bus_start; bus <= config->bus_end; bus++)
    {
        for (uint64_t device = 0; device < SDHCI_PCI_DEVICE_COUNT; device++)
        {
            match_optional(
                Sdhci__find_pci_function_on_device(ecam, config, bus, device),
                some(function) { return Some(SdhciPciFunction, function); },
                none_do_nothing()
            );
        }
    }

    return None(SdhciPciFunction);
}

static Optional(SdHostConfig) Sdhci__load_pci_host_config(void)
{
    PciConfig pci_config = PciConfig__new(0, 0, 0, 0);
    if (!SDHCI_LOAD_PCI_CONFIG(&pci_config))
    {
        return None(SdHostConfig);
    }

    ServiceMappedMemoryPtr ecam;
    if (!service_ok(service_map_mmio(pci_config.ecam_physical_base, pci_config.ecam_size, &ecam)))
    {
        DRIVER_DEBUG("sdhci: pci ecam map failed\n");
        return None(SdHostConfig);
    }

    match_optional(
        Sdhci__find_pci_function(ecam, &pci_config),
        some(function)
        {
            void * function_base = Sdhci__pci_function_base(ecam, &pci_config, &function);
            uint32_t bar0 = SDHCI_PCI_READ32(function_base, SDHCI_PCI_CONFIG_BAR0);
            if ((bar0 & SDHCI_PCI_BAR_IO_SPACE) != 0)
            {
                return None(SdHostConfig);
            }

            uint64_t physical_base = (uint64_t)(bar0 & SDHCI_PCI_BAR_MEMORY_ADDRESS_MASK);
            if ((bar0 & SDHCI_PCI_BAR_MEMORY_TYPE_MASK) == SDHCI_PCI_BAR_MEMORY_64)
            {
                physical_base |= ((uint64_t)SDHCI_PCI_READ32(function_base, SDHCI_PCI_CONFIG_BAR0 + 4)) << 32;
            }
            if (physical_base == 0)
            {
                return None(SdHostConfig);
            }

            uint16_t command = SDHCI_PCI_READ16(function_base, SDHCI_PCI_CONFIG_COMMAND);
            SDHCI_PCI_WRITE16(function_base, SDHCI_PCI_CONFIG_COMMAND,
                              command | SDHCI_PCI_COMMAND_MEMORY_SPACE | SDHCI_PCI_COMMAND_BUS_MASTER);

            return Some(SdHostConfig,
                        SdHostConfig__new(physical_base, SDHCI_PCI_MMIO_SIZE,
                                          SDHCI_PCI_READ8(function_base, SDHCI_PCI_CONFIG_INTERRUPT_LINE)));
        },
        none_do_nothing()
    );

    return None(SdHostConfig);
}

static Optional(SdHostConfig) Sdhci__load_host_config(void)
{
    match_optional(
        Sdhci__load_pci_host_config(),
        some(config) { return Some(SdHostConfig, config); },
        none_do_nothing()
    );

    return SDHCI_LOAD_CONFIG();
}

static bool Sdhci__wait8_clear(SdhciMmioPtr mmio, uint64_t offset, uint8_t mask)
{
    for (uint64_t poll = 0; poll < SDHCI_POLL_LIMIT; poll++)
    {
        if ((SDHCI_READ8(mmio, offset) & mask) == 0)
        {
            return true;
        }
    }

    return false;
}

static bool Sdhci__wait32_clear(SdhciMmioPtr mmio, uint64_t offset, uint32_t mask)
{
    for (uint64_t poll = 0; poll < SDHCI_POLL_LIMIT; poll++)
    {
        if ((SDHCI_READ32(mmio, offset) & mask) == 0)
        {
            return true;
        }
    }

    return false;
}

static bool Sdhci__wait16_set(SdhciMmioPtr mmio, uint64_t offset, uint16_t mask)
{
    for (uint64_t poll = 0; poll < SDHCI_POLL_LIMIT; poll++)
    {
        if ((SDHCI_READ16(mmio, offset) & mask) == mask)
        {
            return true;
        }
    }

    return false;
}

static bool Sdhci__wait_interrupt(SdhciMmioPtr mmio, uint32_t mask)
{
    for (uint64_t poll = 0; poll < SDHCI_POLL_LIMIT; poll++)
    {
        uint32_t status = SDHCI_READ32(mmio, SDHCI_INT_STATUS);
        if ((status & SDHCI_INT_ERROR) != 0)
        {
            DRIVER_DEBUG("sdhci: interrupt error status=0x%x\n", status);
            SDHCI_WRITE32(mmio, SDHCI_INT_STATUS, status);
            return false;
        }
        if ((status & mask) == mask)
        {
            SDHCI_WRITE32(mmio, SDHCI_INT_STATUS, mask);
            return true;
        }
    }

    DRIVER_DEBUG("sdhci: interrupt wait timed out mask=0x%x status=0x%x\n", mask, SDHCI_READ32(mmio, SDHCI_INT_STATUS));
    return false;
}

static bool Sdhci__reset(SdhciMmioPtr mmio, uint8_t mask)
{
    SDHCI_WRITE8(mmio, SDHCI_SOFTWARE_RESET, mask);

    return Sdhci__wait8_clear(mmio, SDHCI_SOFTWARE_RESET, mask);
}

static uint16_t Sdhci__clock_divisor(uint32_t capabilities, uint64_t target_hz)
{
    uint64_t base_clock_mhz = (capabilities >> 8) & 0xffU;
    if (base_clock_mhz == 0)
    {
        base_clock_mhz = 100;
    }

    uint64_t base_hz = base_clock_mhz * 1000000ULL;
    uint64_t divisor = (base_hz + target_hz - 1) / target_hz;
    if (divisor <= 1)
    {
        return 0;
    }
    if ((divisor & 1ULL) != 0)
    {
        divisor++;
    }
    if (divisor > 256)
    {
        divisor = 256;
    }

    return (uint16_t)((divisor / 2) << 8);
}

static bool Sdhci__set_clock(SdhciMmioPtr mmio, uint64_t target_hz)
{
    uint32_t capabilities = SDHCI_READ32(mmio, SDHCI_CAPABILITIES);
    uint16_t clock = Sdhci__clock_divisor(capabilities, target_hz) | SDHCI_CLOCK_INTERNAL_ENABLE;

    SDHCI_WRITE16(mmio, SDHCI_CLOCK_CONTROL, 0);
    SDHCI_WRITE16(mmio, SDHCI_CLOCK_CONTROL, clock);
    if (!Sdhci__wait16_set(mmio, SDHCI_CLOCK_CONTROL, SDHCI_CLOCK_INTERNAL_STABLE))
    {
        DRIVER_DEBUG("sdhci: clock did not stabilize\n");
        return false;
    }

    SDHCI_WRITE16(mmio, SDHCI_CLOCK_CONTROL, clock | SDHCI_CLOCK_SD_ENABLE);
    return true;
}

static bool Sdhci__setup_host(SdhciMmioPtr mmio)
{
    if (!Sdhci__reset(mmio, SDHCI_RESET_ALL))
    {
        DRIVER_DEBUG("sdhci: reset failed\n");
        return false;
    }

    SDHCI_WRITE32(mmio, SDHCI_INT_STATUS, SDHCI_INT_ALL);
    SDHCI_WRITE32(mmio, SDHCI_INT_ENABLE, SDHCI_INT_ALL);
    SDHCI_WRITE32(mmio, SDHCI_SIGNAL_ENABLE, 0);
    SDHCI_WRITE8(mmio, SDHCI_TIMEOUT_CONTROL, 0x0e);
    SDHCI_WRITE8(mmio, SDHCI_POWER_CONTROL, SDHCI_POWER_330 | SDHCI_POWER_ON);

    if ((SDHCI_READ32(mmio, SDHCI_PRESENT_STATE) & SDHCI_PRESENT_CARD_INSERTED) == 0)
    {
        DRIVER_DEBUG("sdhci: no card inserted\n");
        return false;
    }

    return Sdhci__set_clock(mmio, 400000);
}

static uint16_t SdhciCommand__value(ConstSdhciCommandPtr command)
{
    uint16_t value = (uint16_t)((command->index << 8) | command->response_type);
    if (command->response_type != SDHCI_CMD_RESP_NONE)
    {
        value |= SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK;
    }
    if (command->data_present)
    {
        value |= SDHCI_CMD_DATA_PRESENT;
    }

    return value;
}

static SdhciCommandResult Sdhci__send_command(SdhciDevicePtr self, ConstSdhciCommandPtr command)
{
    SdhciCommandResult result = SdhciCommandResult__new(false, 0, 0, 0, 0);
    uint32_t inhibit = SDHCI_PRESENT_CMD_INHIBIT;
    if (command->data_present)
    {
        inhibit |= SDHCI_PRESENT_DAT_INHIBIT;
    }

    if (!Sdhci__wait32_clear(self->mmio, SDHCI_PRESENT_STATE, inhibit))
    {
        DRIVER_DEBUG("sdhci: command inhibit stuck cmd=%i\n", (int)command->index);
        return result;
    }

    SDHCI_WRITE32(self->mmio, SDHCI_INT_STATUS, SDHCI_INT_ALL);
    SDHCI_WRITE16(self->mmio, SDHCI_TRANSFER_MODE, command->transfer_mode);
    SDHCI_WRITE32(self->mmio, SDHCI_ARGUMENT, command->argument);
    __sync_boundary();
    SDHCI_WRITE16(self->mmio, SDHCI_COMMAND, SdhciCommand__value(command));

    if (!Sdhci__wait_interrupt(self->mmio, SDHCI_INT_COMMAND_COMPLETE))
    {
        DRIVER_DEBUG("sdhci: command failed cmd=%i\n", (int)command->index);
        return result;
    }

    result.ok = true;
    if (command->response_type != SDHCI_CMD_RESP_NONE)
    {
        result.response[0] = SDHCI_READ32(self->mmio, SDHCI_RESPONSE);
        result.response[1] = SDHCI_READ32(self->mmio, SDHCI_RESPONSE + 4);
        result.response[2] = SDHCI_READ32(self->mmio, SDHCI_RESPONSE + 8);
        result.response[3] = SDHCI_READ32(self->mmio, SDHCI_RESPONSE + 12);
    }

    return result;
}

static bool Sdhci__send_app_command(SdhciDevicePtr self, uint32_t argument)
{
    SdhciCommand command = SdhciCommand__new(SD_CMD_APP_CMD, argument, SDHCI_CMD_RESP_48, 0, false);
    SdhciCommandResult result = Sdhci__send_command(self, &command);

    return result.ok;
}

static uint64_t Sdhci__card_block_count_from_csd(ConstSdhciCommandResultPtr csd)
{
    uint32_t csd_structure = (csd->response[3] >> 30) & 0x3U;
    if (csd_structure == 1)
    {
        uint64_t c_size = ((uint64_t)(csd->response[2] & 0x3fU) << 16) | ((uint64_t)csd->response[1] >> 16);
        return (c_size + 1ULL) * 1024ULL;
    }

    uint64_t read_bl_len = (csd->response[2] >> 16) & 0x0fU;
    uint64_t c_size = ((uint64_t)(csd->response[2] & 0x03ffU) << 2) | ((uint64_t)(csd->response[1] >> 30) & 0x03U);
    uint64_t c_size_mult = (csd->response[1] >> 15) & 0x07U;
    uint64_t block_len = 1ULL << read_bl_len;
    uint64_t mult = 1ULL << (c_size_mult + 2ULL);
    uint64_t bytes = (c_size + 1ULL) * mult * block_len;

    return bytes / SDHCI_BLOCK_SIZE;
}

static bool Sdhci__init_card(SdhciDevicePtr self)
{
    SdhciCommand idle = SdhciCommand__new(SD_CMD_GO_IDLE_STATE, 0, SDHCI_CMD_RESP_NONE, 0, false);
    Sdhci__send_command(self, &idle);

    SdhciCommand interface_condition = SdhciCommand__new(SD_CMD_SEND_IF_COND, 0x1aa, SDHCI_CMD_RESP_48, 0, false);
    Sdhci__send_command(self, &interface_condition);

    SdhciCommandResult op_cond = SdhciCommandResult__new(false, 0, 0, 0, 0);
    for (uint64_t retry = 0; retry < SDHCI_APP_CMD_RETRIES; retry++)
    {
        if (!Sdhci__send_app_command(self, 0))
        {
            return false;
        }
        SdhciCommand send_op_cond =
            SdhciCommand__new(SD_ACMD_SD_SEND_OP_COND, SD_ACMD41_ARGUMENT, SDHCI_CMD_RESP_48, 0, false);
        op_cond = Sdhci__send_command(self, &send_op_cond);
        if (op_cond.ok && (op_cond.response[0] & SD_OCR_READY) != 0)
        {
            self->high_capacity = (op_cond.response[0] & SD_OCR_HIGH_CAPACITY) != 0;
            break;
        }
    }

    if (!op_cond.ok || (op_cond.response[0] & SD_OCR_READY) == 0)
    {
        DRIVER_DEBUG("sdhci: card did not become ready\n");
        return false;
    }

    SdhciCommand send_cid = SdhciCommand__new(SD_CMD_ALL_SEND_CID, 0, SDHCI_CMD_RESP_136, 0, false);
    SdhciCommandResult cid = Sdhci__send_command(self, &send_cid);
    if (!cid.ok)
    {
        return false;
    }

    SdhciCommand send_rca = SdhciCommand__new(SD_CMD_SEND_RELATIVE_ADDR, 0, SDHCI_CMD_RESP_48, 0, false);
    SdhciCommandResult rca = Sdhci__send_command(self, &send_rca);
    if (!rca.ok)
    {
        return false;
    }
    self->rca = (uint16_t)(rca.response[0] >> 16);

    SdhciCommand send_csd =
        SdhciCommand__new(SD_CMD_SEND_CSD, ((uint32_t)self->rca) << 16, SDHCI_CMD_RESP_136, 0, false);
    SdhciCommandResult csd = Sdhci__send_command(self, &send_csd);
    if (!csd.ok)
    {
        return false;
    }
    self->block_count = Sdhci__card_block_count_from_csd(&csd);
    if (self->block_count == 0)
    {
        DRIVER_DEBUG("sdhci: invalid card capacity\n");
        return false;
    }

    SdhciCommand select_card =
        SdhciCommand__new(SD_CMD_SELECT_CARD, ((uint32_t)self->rca) << 16, SDHCI_CMD_RESP_48_BUSY, 0, false);
    SdhciCommandResult select = Sdhci__send_command(self, &select_card);
    if (!select.ok)
    {
        return false;
    }

    if (!Sdhci__set_clock(self->mmio, 25000000))
    {
        return false;
    }

    SdhciCommand set_block_len = SdhciCommand__new(SD_CMD_SET_BLOCKLEN, SDHCI_BLOCK_SIZE, SDHCI_CMD_RESP_48, 0, false);
    SdhciCommandResult block_len = Sdhci__send_command(self, &set_block_len);

    return block_len.ok;
}

static bool Sdhci__bind(SdhciDevicePtr self, SdHostConfig config)
{
    ServiceMappedMemoryPtr mapping;
    if (!service_ok(service_map_mmio(config.physical_base, config.mmio_size, &mapping)))
    {
        DRIVER_DEBUG("sdhci: mmio map failed\n");
        return false;
    }

    BString device_path = bstr(SDHCI_DEVICE_PATH);
    *self = SdhciDevice__new(mapping, driver_ptr(mapping), config.irq, &device_path);

    uint32_t host_version = SDHCI_READ32(self->mmio, SDHCI_HOST_VERSION);
    DRIVER_DEBUG("sdhci: host physical_base=0x%x mmio_size=0x%x irq=%i version=0x%x\n",
                 (unsigned int)config.physical_base, (unsigned int)config.mmio_size, (int)config.irq, host_version);

    return Sdhci__setup_host(self->mmio) && Sdhci__init_card(self);
}

static uint32_t Sdhci__block_argument(ConstSdhciDevicePtr self, uint64_t block)
{
    if (self->high_capacity)
    {
        return (uint32_t)block;
    }

    return (uint32_t)(block * SDHCI_BLOCK_SIZE);
}

static ServiceCode Sdhci__read_blocks(SdhciDevicePtr self, ServiceBlockRequestPtr request)
{
    if (request->block_size != SDHCI_BLOCK_SIZE || request->block_count == 0 || request->buffer == 0 ||
        request->buffer_size < request->block_count * SDHCI_BLOCK_SIZE ||
        request->block + request->block_count > self->block_count)
    {
        return SERVICE_INVALID_BUFFER;
    }

    SDHCI_WRITE16(self->mmio, SDHCI_BLOCK_SIZE_REG, SDHCI_BLOCK_SIZE);
    SDHCI_WRITE16(self->mmio, SDHCI_BLOCK_COUNT_REG, (uint16_t)request->block_count);

    bool multi_block = request->block_count > 1;
    SdhciCommand read_command = SdhciCommand__new(
        multi_block ? SD_CMD_READ_MULTIPLE_BLOCK : SD_CMD_READ_SINGLE_BLOCK,
        Sdhci__block_argument(self, request->block), SDHCI_CMD_RESP_48,
        SDHCI_TRANSFER_READ | (multi_block ? SDHCI_TRANSFER_MULTI_BLOCK | SDHCI_TRANSFER_BLOCK_COUNT_ENABLE : 0), true);
    SdhciCommandResult read = Sdhci__send_command(self, &read_command);
    if (!read.ok)
    {
        return SERVICE_BLOCK_REQUEST_FAILED;
    }

    uint8_t * target = (uint8_t *)request->buffer;
    for (uint64_t block = 0; block < request->block_count; block++)
    {
        if (!Sdhci__wait_interrupt(self->mmio, SDHCI_INT_BUFFER_READ_READY))
        {
            return SERVICE_BLOCK_REQUEST_FAILED;
        }

        for (uint64_t word = 0; word < SDHCI_BLOCK_SIZE / sizeof(uint32_t); word++)
        {
            uint32_t data = SDHCI_READ32(self->mmio, SDHCI_BUFFER);
            uint64_t offset = (block * SDHCI_BLOCK_SIZE) + (word * sizeof(uint32_t));
            target[offset] = (uint8_t)data;
            target[offset + 1] = (uint8_t)(data >> 8);
            target[offset + 2] = (uint8_t)(data >> 16);
            target[offset + 3] = (uint8_t)(data >> 24);
        }
    }

    if (!Sdhci__wait_interrupt(self->mmio, SDHCI_INT_TRANSFER_COMPLETE))
    {
        return SERVICE_BLOCK_REQUEST_FAILED;
    }

    if (request->buffer_size >= 8)
    {
        DRIVER_DEBUG("sdhci: read request id=%i block=%i driver_buffer_first_bytes=%i,%i,%i,%i,%i,%i,%i,%i\n",
                     (int)request->request_id, (int)request->block, (int)target[0], (int)target[1], (int)target[2],
                     (int)target[3], (int)target[4], (int)target[5], (int)target[6], (int)target[7]);
    }

    return SERVICE_OK;
}

static void Sdhci__handle_block_request(SdhciDevicePtr self, uint64_t request_id)
{
    ServiceBlockRequest request;
    ServiceCode request_status = service_block_get_request(request_id, &request);
    if (!service_ok(request_status))
    {
        service_block_complete(request_id, request_status, 0);
        return;
    }

    if (!BString__equals(&self->path, &request.device_path))
    {
        service_block_complete(request.request_id, SERVICE_BLOCK_REQUEST_FAILED, 0);
        return;
    }

    if (request.operation != SERVICE_BLOCK_READ)
    {
        service_block_complete(request.request_id, SERVICE_OPERATION_NOT_SUPPORTED, 0);
        return;
    }

    ServiceCode status = Sdhci__read_blocks(self, &request);
    service_block_complete(request.request_id, status, service_ok(status) ? request.block_count : 0);
}

static BlockProtocolStatus Sdhci__transfer_channel(BlockServerContext context, BlockProtocolOperation operation,
                                                   BlockProtocolBlockNumber block, ChannelDataPtr data)
{
    SdhciDevicePtr self = context;
    if (operation != BLOCK_PROTOCOL_READ)
    {
        return BLOCK_PROTOCOL_IO_ERROR;
    }
    ServiceBlockRequest request = ServiceBlockRequest__new(0, self->path, SERVICE_BLOCK_READ, block, 1,
                                                           SDHCI_BLOCK_SIZE, (Address)data, SDHCI_BLOCK_SIZE);
    return service_ok(Sdhci__read_blocks(self, &request)) ? BLOCK_PROTOCOL_OK : BLOCK_PROTOCOL_IO_ERROR;
}

static void Sdhci__ack_irq(SdhciDevicePtr self)
{
    uint32_t status = SDHCI_READ32(self->mmio, SDHCI_INT_STATUS);
    if (status != 0)
    {
        SDHCI_WRITE32(self->mmio, SDHCI_INT_STATUS, status);
    }
}

int SDHCI_ENTRYPOINT(void)
{
    if (!service_ok(service_become_service()))
    {
        DRIVER_DEBUG("sdhci: service registration failed\n");
        return 1;
    }

    match_optional(
        Sdhci__load_host_config(),
        some(config)
        {
            SdhciDevice device = SdhciDevice__new_empty();
            if (!Sdhci__bind(&device, config))
            {
                DRIVER_DEBUG("sdhci: bind failed\n");
                return 1;
            }

            if (!service_ok(service_register_block_device(&device.path, SDHCI_BLOCK_SIZE, device.block_count)))
            {
                DRIVER_DEBUG("sdhci: block device registration failed\n");
                return 1;
            }

            BlockServer block_server;
            match_result(
                BlockServer__listen(&device.path, device.block_count, &device, Sdhci__transfer_channel),
                success(server) { block_server = server; },
                error(code, message) { return 1; }
            );

            if (!service_ok(service_publish_ready(&device.path)))
            {
                DRIVER_DEBUG("sdhci: ready publish failed\n");
                return 1;
            }

            if (!service_ok(service_subscribe_irq(device.irq)))
            {
                DRIVER_DEBUG("sdhci: irq subscribe failed\n");
                return 1;
            }

            DRIVER_DEBUG("sdhci: registered %s blocks=%i\n", BString__cstr(&device.path), (int)device.block_count);

            ServiceEvent event;
            Channel__prepare_wait();
            for (ServiceCode wait_status = service_poll(&event); service_alive(wait_status);
                 wait_status = service_poll(&event))
            {
                if (service_ok(wait_status))
                {
                    __sync_boundary();
                    if (event.type == SERVICE_EVENT_IRQ && event.source == device.irq)
                    {
                        Sdhci__ack_irq(&device);
                    }
                    if (event.type == SERVICE_EVENT_BLOCK_IO)
                    {
                        Sdhci__handle_block_request(&device, event.source);
                    }
                    __sync_boundary();
                }
                if (!BlockServer__poll(&block_server) && wait_status == SERVICE_EVENT_QUEUE_EMPTY)
                {
                    Channel__wait_event();
                }
            }

            return 0;
        },
        none_do_nothing()
    );

    DRIVER_DEBUG("sdhci: no SD host config found\n");
    return 1;
}
