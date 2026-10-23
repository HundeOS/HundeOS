#include "../uefi_devices.h"

__BOOT__(void) UefiDevices__configure(BootPlatformInfoPtr self)
{
    self->virtio_mmio = None(BootVirtioMmioInfo);
    self->sd_host = Some(SdHostInfo, SdHostInfo__new(0xfe340000ULL, 0x100ULL, 126ULL, SD_HOST_KIND_GENERIC, 0));
    self->ethernet_mac =
        Some(EthernetMacInfo, EthernetMacInfo__new(ETHERNET_MAC_KIND_BCM2711_GENET, 0xfd580000ULL, 0x10000ULL, 157ULL));
}
