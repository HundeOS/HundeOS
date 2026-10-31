#include "../uefi_devices.h"

#define QEMU_VIRT_PCI_ECAM_BUS_SIZE 0x100000ULL
#define QEMU_VIRT_PCI_ECAM_DEVICE_SIZE 0x8000ULL
#define QEMU_VIRT_PCI_CLASS_CODE 0x0bULL
#define QEMU_VIRT_PCI_SUBCLASS 0x0aULL
#define QEMU_VIRT_PCI_BAR0 0x10ULL
#define QEMU_VIRT_PCI_COMMAND 0x04ULL
#define QEMU_VIRT_PCI_INTERRUPT_LINE 0x3cULL
#define QEMU_VIRT_PCI_INTERRUPT_PIN 0x3dULL
#define QEMU_VIRT_PCI_SD_CLASS 0x08U
#define QEMU_VIRT_PCI_SD_SUBCLASS 0x05U
#define QEMU_VIRT_PCI_MEMORY_ENABLE 0x0002U

typedef volatile uint8_t * QemuVirtPciRegister8Ptr;
typedef volatile uint16_t * QemuVirtPciRegister16Ptr;
typedef volatile uint32_t * QemuVirtPciRegister32Ptr;

static __BOOT__(uint8_t) QemuVirtPci__read8(uint64_t address)
{
    return *(QemuVirtPciRegister8Ptr)address;
}

static __BOOT__(uint16_t) QemuVirtPci__read16(uint64_t address)
{
    return *(QemuVirtPciRegister16Ptr)address;
}

static __BOOT__(uint32_t) QemuVirtPci__read32(uint64_t address)
{
    return *(QemuVirtPciRegister32Ptr)address;
}

static __BOOT__(void) QemuVirtPci__write8(uint64_t address, uint8_t value)
{
    *(QemuVirtPciRegister8Ptr)address = value;
}

static __BOOT__(void) QemuVirtPci__write16(uint64_t address, uint16_t value)
{
    *(QemuVirtPciRegister16Ptr)address = value;
}

static __BOOT__(void) QemuVirtPci__configure_sd_host(BootPlatformInfoPtr self)
{
    match_optional(
        self->pci,
        some(pci)
        {
            if (pci.ecam_physical_base == 0 || pci.bus_start != 0 || pci.ecam_size < QEMU_VIRT_PCI_ECAM_BUS_SIZE)
            {
                return;
            }

            for (uint64_t device = 0; device < 32; device++)
            {
                uint64_t function = pci.ecam_physical_base + (device * QEMU_VIRT_PCI_ECAM_DEVICE_SIZE);
                if (QemuVirtPci__read16(function) == 0xffffU ||
                    QemuVirtPci__read8(function + QEMU_VIRT_PCI_CLASS_CODE) != QEMU_VIRT_PCI_SD_CLASS ||
                    QemuVirtPci__read8(function + QEMU_VIRT_PCI_SUBCLASS) != QEMU_VIRT_PCI_SD_SUBCLASS)
                {
                    continue;
                }

                uint8_t pin = QemuVirtPci__read8(function + QEMU_VIRT_PCI_INTERRUPT_PIN);
                if (pin == 0 || pin > 4)
                {
                    return;
                }

                uint32_t bar0 = QemuVirtPci__read32(function + QEMU_VIRT_PCI_BAR0);
                if ((bar0 & 0x1U) != 0 || (bar0 & 0x6U) != 0)
                {
                    return;
                }
                uint64_t physical_base = bar0 & 0xfffffff0U;
                if (physical_base == 0)
                {
                    return;
                }

                uint64_t irq = 35 + ((device + pin - 1) % 4);
                QemuVirtPci__write8(function + QEMU_VIRT_PCI_INTERRUPT_LINE, (uint8_t)irq);
                QemuVirtPci__write16(function + QEMU_VIRT_PCI_COMMAND,
                                     QemuVirtPci__read16(function + QEMU_VIRT_PCI_COMMAND) |
                                         QEMU_VIRT_PCI_MEMORY_ENABLE);
                self->sd_host = Some(SdHostInfo, SdHostInfo__new(physical_base, 0x1000, irq, SD_HOST_KIND_PCI, 0));
                return;
            }
        },
        none_do_nothing()
    );
}

__BOOT__(void) UefiDevices__configure(BootPlatformInfoPtr self)
{
    self->virtio_mmio = Some(BootVirtioMmioInfo, BootVirtioMmioInfo__new(0x0a000000ULL, 0x200ULL, 32ULL, 48ULL));
    self->sd_host = None(SdHostInfo);
    self->ethernet_mac = None(EthernetMacInfo);
    QemuVirtPci__configure_sd_host(self);
}
