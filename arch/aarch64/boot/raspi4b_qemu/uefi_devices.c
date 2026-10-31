#include "../uefi_devices.h"

__BOOT__(void) UefiDevices__configure(BootPlatformInfoPtr self)
{
    self->cpu_count = 1;
    self->memory_physical_end = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    self->virtio_mmio = None(BootVirtioMmioInfo);
    self->sd_host = Some(SdHostInfo, SdHostInfo__new(0xfe300000ULL, 0x100ULL, 158ULL, SD_HOST_KIND_BCM2835, 0));
    self->ethernet_mac = None(EthernetMacInfo);
}
