#ifndef HUNDE_VIRTIO_H
#define HUNDE_VIRTIO_H

#include <stdbool.h>
#include <stdint.h>

static inline bool VirtioDevice__claimable(uint32_t magic, uint32_t version, uint32_t device)
{
    return magic == 0x74726976U && version >= 1 && (device == 1 || device == 2 || device == 18);
}

#endif
