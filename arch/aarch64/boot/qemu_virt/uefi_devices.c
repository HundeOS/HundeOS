#include "../uefi_devices.h"

__BOOT__(void) UefiDevices__configure(BootPlatformInfoPtr self)
{
    self->virtio_mmio = Some(BootVirtioMmioInfo, BootVirtioMmioInfo__new(0x0a000000ULL, 0x200ULL, 32ULL, 48ULL));
    self->sd_host = None(SdHostInfo);
    self->ethernet_mac = None(EthernetMacInfo);
}
