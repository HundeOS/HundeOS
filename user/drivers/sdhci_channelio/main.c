#include "channel/block_server.h"
#include "channel/block_runtime.h"
#define DRIVER_LOG_TAG "SDHCI"

#include <stdbool.h>
#include <stdint.h>

#include "../../../common/types/bstring.h"
#include "sys/debug.h"
#include "sys/pci_config.h"
#include "sys/process_sleep.h"
#include "sys/sd_host_config.h"
#include "sys/service.h"
#include "sys/syslog.h"
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

#ifndef SDHCI_SLEEP_USEC
#define SDHCI_SLEEP_USEC sleep_usec
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
#define CV181X_VENDOR_MSHC_CTRL 0x200
#define CV181X_VENDOR_PHY_TX_RX_DLY 0x240
#define CV181X_VENDOR_PHY_CONFIG 0x24c
#define CV181X_MSHC_CTRL_DEFAULT 0x00000302U
#define CV181X_PHY_TX_RX_DLY_DEFAULT 0x01000100U
#define CV181X_PHY_CONFIG_ENABLE 0x00000001U

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
#define SDHCI_TRANSFER_AUTO_CMD12 0x0004U
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

typedef enum SdhciResponseKind
{
    SDHCI_RESPONSE_NONE,
    SDHCI_RESPONSE_R1,
    SDHCI_RESPONSE_R1B,
    SDHCI_RESPONSE_R2,
    SDHCI_RESPONSE_R3,
    SDHCI_RESPONSE_R6,
    SDHCI_RESPONSE_R7,
} SdhciResponseKind;

typedef enum SdhciCommandFailure
{
    SDHCI_COMMAND_OK,
    SDHCI_COMMAND_INHIBITED,
    SDHCI_COMMAND_RESPONSE_TIMEOUT,
    SDHCI_COMMAND_BUSY_TIMEOUT,
} SdhciCommandFailure;

typedef struct SdhciCommand
{
    uint32_t index;
    uint32_t argument;
    SdhciResponseKind response_kind;
    uint16_t transfer_mode;
    bool data_present;
} SdhciCommand;

typedef const SdhciCommand * ConstSdhciCommandPtr;

typedef struct SdhciCommandResult
{
    bool ok;
    SdhciCommandFailure failure;
    bool recovered;
    uint32_t failure_state;
    uint32_t failure_status;
    uint32_t response[4];
} SdhciCommandResult;

typedef const SdhciCommandResult * ConstSdhciCommandResultPtr;

typedef struct SdhciDevice
{
    ServiceMappedMemoryPtr mapping;
    SdhciMmioPtr mmio;
    uint64_t irq;
    SdHostKind kind;
    uint64_t source_clock_hz;
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

static SdhciCommand SdhciCommand__new(uint32_t index, uint32_t argument, SdhciResponseKind response_kind,
                                      uint16_t transfer_mode, bool data_present)
{
    return (SdhciCommand){
        .index = index,
        .argument = argument,
        .response_kind = response_kind,
        .transfer_mode = transfer_mode,
        .data_present = data_present,
    };
}

static SdhciCommandResult SdhciCommandResult__new(bool ok, uint32_t response0, uint32_t response1, uint32_t response2,
                                                  uint32_t response3)
{
    return (SdhciCommandResult){
        .ok = ok,
        .failure = SDHCI_COMMAND_OK,
        .recovered = false,
        .failure_state = 0,
        .failure_status = 0,
        .response = {response0, response1, response2, response3},
    };
}

static SdhciDevice SdhciDevice__new(ServiceMappedMemoryPtr mapping, SdhciMmioPtr mmio, uint64_t irq, SdHostKind kind,
                                    uint64_t source_clock_hz, ConstBStringPtr path)
{
    return (SdhciDevice){
        .mapping = mapping,
        .mmio = mmio,
        .irq = irq,
        .kind = kind,
        .source_clock_hz = source_clock_hz,
        .path = *path,
    };
}

static SdhciDevice SdhciDevice__new_empty(void)
{
    return SdhciDevice__new(0, 0, 0, SD_HOST_KIND_GENERIC, 0, &bstr(""));
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
                some(function)
                {
                    return Some(SdhciPciFunction, function);
                },
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
                                          SDHCI_PCI_READ8(function_base, SDHCI_PCI_CONFIG_INTERRUPT_LINE),
                                          SD_HOST_KIND_GENERIC, 0));
        },
        none_do_nothing()
    );

    return None(SdHostConfig);
}

static Optional(SdHostConfig) Sdhci__load_host_config(void)
{
    match_optional(
        Sdhci__load_pci_host_config(),
        some(config)
        {
            return Some(SdHostConfig, config);
        },
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

static uint16_t Sdhci__clock_divisor(uint64_t base_hz, uint64_t target_hz)
{
    uint64_t divisor = (base_hz + target_hz - 1) / target_hz;
    if (divisor <= 1)
    {
        return 0;
    }
    if ((divisor & 1ULL) != 0)
    {
        divisor++;
    }
    if (divisor > 2046)
    {
        divisor = 2046;
    }

    uint16_t encoded = (uint16_t)(divisor / 2);
    return (uint16_t)(((encoded & 0xffU) << 8) | ((encoded & 0x300U) >> 2));
}

static bool Sdhci__set_clock(SdhciDevicePtr self, uint64_t target_hz)
{
    uint64_t base_hz = self->source_clock_hz;
    if (base_hz == 0)
    {
        uint32_t capabilities = SDHCI_READ32(self->mmio, SDHCI_CAPABILITIES);
        uint64_t base_clock_mhz = (capabilities >> 8) & 0xffU;
        base_hz = (base_clock_mhz == 0 ? 100 : base_clock_mhz) * 1000000ULL;
    }
    uint16_t clock = Sdhci__clock_divisor(base_hz, target_hz) | SDHCI_CLOCK_INTERNAL_ENABLE;

    SDHCI_WRITE16(self->mmio, SDHCI_CLOCK_CONTROL, 0);
    SDHCI_WRITE16(self->mmio, SDHCI_CLOCK_CONTROL, clock);
    if (!Sdhci__wait16_set(self->mmio, SDHCI_CLOCK_CONTROL, SDHCI_CLOCK_INTERNAL_STABLE))
    {
        DRIVER_DEBUG("sdhci: clock did not stabilize\n");
        return false;
    }

    SDHCI_WRITE16(self->mmio, SDHCI_CLOCK_CONTROL, clock | SDHCI_CLOCK_SD_ENABLE);
    return true;
}

static bool Sdhci__setup_host(SdhciDevicePtr self)
{
    if (!Sdhci__reset(self->mmio, SDHCI_RESET_ALL))
    {
        DRIVER_DEBUG("sdhci: reset failed\n");
        return false;
    }

    if (self->kind == SD_HOST_KIND_CV180X || self->kind == SD_HOST_KIND_CV181X)
    {
        uint32_t mshc_control = SDHCI_READ32(self->mmio, CV181X_VENDOR_MSHC_CTRL);
        SDHCI_WRITE32(self->mmio, CV181X_VENDOR_MSHC_CTRL, mshc_control | CV181X_MSHC_CTRL_DEFAULT);
        SDHCI_WRITE32(self->mmio, CV181X_VENDOR_PHY_TX_RX_DLY, CV181X_PHY_TX_RX_DLY_DEFAULT);
        SDHCI_WRITE32(self->mmio, CV181X_VENDOR_PHY_CONFIG, CV181X_PHY_CONFIG_ENABLE);
    }

    SDHCI_WRITE32(self->mmio, SDHCI_INT_STATUS, SDHCI_INT_ALL);
    SDHCI_WRITE32(self->mmio, SDHCI_INT_ENABLE, SDHCI_INT_ALL);
    SDHCI_WRITE32(self->mmio, SDHCI_SIGNAL_ENABLE, 0);
    SDHCI_WRITE8(self->mmio, SDHCI_TIMEOUT_CONTROL, 0x0e);
    SDHCI_WRITE8(self->mmio, SDHCI_POWER_CONTROL, SDHCI_POWER_330 | SDHCI_POWER_ON);

    if ((SDHCI_READ32(self->mmio, SDHCI_PRESENT_STATE) & SDHCI_PRESENT_CARD_INSERTED) == 0)
    {
        DRIVER_DEBUG("sdhci: no card inserted\n");
        return false;
    }

    if (!Sdhci__set_clock(self, 400000))
    {
        return false;
    }

    return SDHCI_SLEEP_USEC(5000) == PROCESS_SLEEP_OK;
}

static uint16_t SdhciCommand__value(ConstSdhciCommandPtr command)
{
    uint16_t response_flags = SDHCI_CMD_RESP_NONE;
    if (command->response_kind == SDHCI_RESPONSE_R2)
    {
        response_flags = SDHCI_CMD_RESP_136 | SDHCI_CMD_CRC_CHECK;
    }
    else if (command->response_kind == SDHCI_RESPONSE_R3)
    {
        response_flags = SDHCI_CMD_RESP_48;
    }
    else if (command->response_kind == SDHCI_RESPONSE_R1B)
    {
        response_flags = SDHCI_CMD_RESP_48_BUSY | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK;
    }
    else if (command->response_kind != SDHCI_RESPONSE_NONE)
    {
        response_flags = SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK;
    }
    uint16_t value = (uint16_t)((command->index << 8) | response_flags);
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
    if (command->data_present || command->response_kind == SDHCI_RESPONSE_R1B)
    {
        inhibit |= SDHCI_PRESENT_DAT_INHIBIT;
    }

    if (!Sdhci__wait32_clear(self->mmio, SDHCI_PRESENT_STATE, inhibit))
    {
        DRIVER_DEBUG("sdhci: command inhibit stuck cmd=%i\n", (int)command->index);
        result.failure = SDHCI_COMMAND_INHIBITED;
        result.failure_state = SDHCI_READ32(self->mmio, SDHCI_PRESENT_STATE);
        result.failure_status = SDHCI_READ32(self->mmio, SDHCI_INT_STATUS);
        result.recovered = Sdhci__reset(self->mmio, SDHCI_RESET_CMD);
        SDHCI_WRITE32(self->mmio, SDHCI_INT_STATUS, SDHCI_INT_ALL);
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
        result.failure = SDHCI_COMMAND_RESPONSE_TIMEOUT;
        result.failure_state = SDHCI_READ32(self->mmio, SDHCI_PRESENT_STATE);
        result.failure_status = SDHCI_READ32(self->mmio, SDHCI_INT_STATUS);
        result.recovered = Sdhci__reset(self->mmio, SDHCI_RESET_CMD);
        SDHCI_WRITE32(self->mmio, SDHCI_INT_STATUS, SDHCI_INT_ALL);
        return result;
    }

    result.ok = true;
    if (command->response_kind == SDHCI_RESPONSE_R2)
    {
        for (uint64_t index = 0; index < 4; index++)
        {
            uint64_t register_offset = SDHCI_RESPONSE + ((3 - index) * sizeof(uint32_t));
            uint32_t extra = index == 3 ? 0 : SDHCI_READ8(self->mmio, register_offset - 1);
            result.response[index] = (SDHCI_READ32(self->mmio, register_offset) << 8) | extra;
        }
    }
    else if (command->response_kind != SDHCI_RESPONSE_NONE)
    {
        result.response[0] = SDHCI_READ32(self->mmio, SDHCI_RESPONSE);
    }
    if (command->response_kind == SDHCI_RESPONSE_R1B &&
        !Sdhci__wait_interrupt(self->mmio, SDHCI_INT_TRANSFER_COMPLETE))
    {
        result.ok = false;
        result.failure = SDHCI_COMMAND_BUSY_TIMEOUT;
        result.failure_state = SDHCI_READ32(self->mmio, SDHCI_PRESENT_STATE);
        result.failure_status = SDHCI_READ32(self->mmio, SDHCI_INT_STATUS);
        result.recovered = Sdhci__reset(self->mmio, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
        SDHCI_WRITE32(self->mmio, SDHCI_INT_STATUS, SDHCI_INT_ALL);
    }

    return result;
}

static const char * SdhciCommandFailure__name(SdhciCommandFailure self)
{
    switch (self)
    {
    case SDHCI_COMMAND_INHIBITED:
        return "inhibit";
    case SDHCI_COMMAND_RESPONSE_TIMEOUT:
        return "response";
    case SDHCI_COMMAND_BUSY_TIMEOUT:
        return "busy";
    case SDHCI_COMMAND_OK:
        return "none";
    }
    return "unknown";
}

static SdhciCommandResult Sdhci__send_bootstrap_command(SdhciDevicePtr self, ConstSdhciCommandPtr command)
{
    SdhciCommandResult result = Sdhci__send_command(self, command);
    if (!result.ok)
    {
        kernel_syslog_tagf("SDHCI",
                           "bootstrap command failed cmd=%u phase=%s arg=0x%x state=0x%x status=0x%x recovered=%i",
                           command->index, SdhciCommandFailure__name(result.failure), command->argument,
                           result.failure_state, result.failure_status, result.recovered ? 1 : 0);
    }
    return result;
}

static bool Sdhci__send_app_command(SdhciDevicePtr self, uint32_t argument)
{
    SdhciCommand command = SdhciCommand__new(SD_CMD_APP_CMD, argument, SDHCI_RESPONSE_R1, 0, false);
    SdhciCommandResult result = Sdhci__send_bootstrap_command(self, &command);

    return result.ok;
}

static uint64_t Sdhci__card_block_count_from_csd(ConstSdhciCommandResultPtr csd)
{
    uint32_t csd_structure = (csd->response[0] >> 30) & 0x3U;
    if (csd_structure == 1)
    {
        uint64_t c_size = ((uint64_t)(csd->response[1] & 0x3fU) << 16) | ((uint64_t)csd->response[2] >> 16);
        return (c_size + 1ULL) * 1024ULL;
    }

    uint64_t read_bl_len = (csd->response[1] >> 16) & 0x0fU;
    uint64_t c_size = ((uint64_t)(csd->response[1] & 0x03ffU) << 2) | ((uint64_t)(csd->response[2] >> 30) & 0x03U);
    uint64_t c_size_mult = (csd->response[2] >> 15) & 0x07U;
    uint64_t block_len = 1ULL << read_bl_len;
    uint64_t mult = 1ULL << (c_size_mult + 2ULL);
    uint64_t bytes = (c_size + 1ULL) * mult * block_len;

    return bytes / SDHCI_BLOCK_SIZE;
}

static bool Sdhci__init_card(SdhciDevicePtr self)
{
    SdhciCommand idle = SdhciCommand__new(SD_CMD_GO_IDLE_STATE, 0, SDHCI_RESPONSE_NONE, 0, false);
    Sdhci__send_bootstrap_command(self, &idle);

    SdhciCommand interface_condition = SdhciCommand__new(SD_CMD_SEND_IF_COND, 0x1aa, SDHCI_RESPONSE_R7, 0, false);
    Sdhci__send_bootstrap_command(self, &interface_condition);

    SdhciCommandResult op_cond = SdhciCommandResult__new(false, 0, 0, 0, 0);
    for (uint64_t retry = 0; retry < SDHCI_APP_CMD_RETRIES; retry++)
    {
        if (!Sdhci__send_app_command(self, 0))
        {
            return false;
        }
        SdhciCommand send_op_cond =
            SdhciCommand__new(SD_ACMD_SD_SEND_OP_COND, SD_ACMD41_ARGUMENT, SDHCI_RESPONSE_R3, 0, false);
        op_cond = Sdhci__send_bootstrap_command(self, &send_op_cond);
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

    SdhciCommand send_cid = SdhciCommand__new(SD_CMD_ALL_SEND_CID, 0, SDHCI_RESPONSE_R2, 0, false);
    SdhciCommandResult cid = Sdhci__send_bootstrap_command(self, &send_cid);
    if (!cid.ok)
    {
        return false;
    }

    SdhciCommand send_rca = SdhciCommand__new(SD_CMD_SEND_RELATIVE_ADDR, 0, SDHCI_RESPONSE_R6, 0, false);
    SdhciCommandResult rca = Sdhci__send_bootstrap_command(self, &send_rca);
    if (!rca.ok)
    {
        return false;
    }
    self->rca = (uint16_t)(rca.response[0] >> 16);

    SdhciCommand send_csd =
        SdhciCommand__new(SD_CMD_SEND_CSD, ((uint32_t)self->rca) << 16, SDHCI_RESPONSE_R2, 0, false);
    SdhciCommandResult csd = Sdhci__send_bootstrap_command(self, &send_csd);
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
        SdhciCommand__new(SD_CMD_SELECT_CARD, ((uint32_t)self->rca) << 16, SDHCI_RESPONSE_R1B, 0, false);
    SdhciCommandResult select = Sdhci__send_bootstrap_command(self, &select_card);
    if (!select.ok)
    {
        return false;
    }

    if (!Sdhci__set_clock(self, 25000000))
    {
        return false;
    }

    SdhciCommand set_block_len = SdhciCommand__new(SD_CMD_SET_BLOCKLEN, SDHCI_BLOCK_SIZE, SDHCI_RESPONSE_R1, 0, false);
    SdhciCommandResult block_len = Sdhci__send_bootstrap_command(self, &set_block_len);

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
    *self = SdhciDevice__new(mapping, driver_ptr(mapping), config.irq, config.kind, config.source_clock_hz, &device_path);

    if (!Sdhci__setup_host(self))
    {
        kernel_syslog_tagf("SDHCI", "bootstrap host setup failed state=0x%x status=0x%x",
                           SDHCI_READ32(self->mmio, SDHCI_PRESENT_STATE), SDHCI_READ32(self->mmio, SDHCI_INT_STATUS));
        return false;
    }
    if (!Sdhci__init_card(self))
    {
        kernel_syslog_tagf("SDHCI", "bootstrap card init failed state=0x%x status=0x%x",
                           SDHCI_READ32(self->mmio, SDHCI_PRESENT_STATE), SDHCI_READ32(self->mmio, SDHCI_INT_STATUS));
        return false;
    }
    return true;
}

static uint32_t Sdhci__block_argument(ConstSdhciDevicePtr self, uint64_t block)
{
    if (self->high_capacity)
    {
        return (uint32_t)block;
    }

    return (uint32_t)(block * SDHCI_BLOCK_SIZE);
}

static ServiceCode Sdhci__transfer_failure(SdhciDevicePtr self, const char * phase,
                                           BlockProtocolBlockNumber first_block, BlockProtocolBlockCount count)
{
    uint32_t state = SDHCI_READ32(self->mmio, SDHCI_PRESENT_STATE);
    uint32_t status = SDHCI_READ32(self->mmio, SDHCI_INT_STATUS);
    bool recovered = Sdhci__reset(self->mmio, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
    SDHCI_WRITE32(self->mmio, SDHCI_INT_STATUS, SDHCI_INT_ALL);
    kernel_syslog_tagf("SDHCI",
                       "bootstrap read failed phase=%s block=%llu count=%llu state=0x%x status=0x%x recovered=%i",
                       phase, first_block, count, state, status, recovered ? 1 : 0);
    return SERVICE_BLOCK_REQUEST_FAILED;
}

static ServiceCode Sdhci__read_blocks(SdhciDevicePtr self, BlockProtocolBlockNumber first_block,
                                      BlockProtocolBlockCount count, ConstBufferDescriptorPtr buffer)
{
    if (count == 0 || count > UINT16_MAX || buffer->address == 0 || count > buffer->capacity / SDHCI_BLOCK_SIZE ||
        first_block >= self->block_count || count > self->block_count - first_block)
    {
        return SERVICE_INVALID_BUFFER;
    }

    SDHCI_WRITE16(self->mmio, SDHCI_BLOCK_SIZE_REG, SDHCI_BLOCK_SIZE);
    SDHCI_WRITE16(self->mmio, SDHCI_BLOCK_COUNT_REG, (uint16_t)count);

    bool multi_block = count > 1;
    SdhciCommand read_command = SdhciCommand__new(
        multi_block ? SD_CMD_READ_MULTIPLE_BLOCK : SD_CMD_READ_SINGLE_BLOCK, Sdhci__block_argument(self, first_block),
        SDHCI_RESPONSE_R1,
        SDHCI_TRANSFER_READ |
            (multi_block ? SDHCI_TRANSFER_MULTI_BLOCK | SDHCI_TRANSFER_BLOCK_COUNT_ENABLE |
                               SDHCI_TRANSFER_AUTO_CMD12
                         : 0),
        true);
    SdhciCommandResult read = Sdhci__send_command(self, &read_command);
    if (!read.ok)
    {
        kernel_syslog_tagf(
            "SDHCI", "bootstrap read failed phase=command block=%llu count=%llu state=0x%x status=0x%x recovered=%i",
            first_block, count, read.failure_state, read.failure_status, read.recovered ? 1 : 0);
        return SERVICE_BLOCK_REQUEST_FAILED;
    }

    uint8_t * target = (uint8_t *)buffer->address;
    for (uint64_t block = 0; block < count; block++)
    {
        if (!Sdhci__wait_interrupt(self->mmio, SDHCI_INT_BUFFER_READ_READY))
        {
            return Sdhci__transfer_failure(self, "data", first_block, count);
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
        return Sdhci__transfer_failure(self, "complete", first_block, count);
    }

    return SERVICE_OK;
}

static BlockProtocolStatus Sdhci__transfer_channel(BlockServerContext context, BlockProtocolOperation operation,
                                                   BlockProtocolBlockNumber block, BlockProtocolBlockCount count,
                                                   ConstBufferDescriptorPtr buffer)
{
    SdhciDevicePtr self = context;
    if (operation != BLOCK_PROTOCOL_READ)
    {
        return BLOCK_PROTOCOL_IO_ERROR;
    }
    return service_ok(Sdhci__read_blocks(self, block, count, buffer)) ? BLOCK_PROTOCOL_OK : BLOCK_PROTOCOL_IO_ERROR;
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
            if (BlockServer__listen_into(&block_server, &device.path, device.block_count, &device,
                                         Sdhci__transfer_channel) != CHANNEL_OK)
            {
                return 1;
            }

            if (!service_ok(service_publish_ready(&device.path)))
            {
                DRIVER_DEBUG("sdhci: ready publish failed\n");
                return 1;
            }

            return BlockServer__run(&block_server);
        },
        none_do_nothing()
    );

    DRIVER_DEBUG("sdhci: no SD host config found\n");
    return 1;
}
