#ifndef HUND_ARCH_DEVICE_H
#define HUND_ARCH_DEVICE_H

#include "types/address.h"
#include <stdbool.h>

bool Arch__is_claimable_virtio_device(Address mapping);
void Arch__reset_virtio_device(Address mapping, Address page_table);

#endif
