#ifndef HUNDE_FILESYSTEM_COMPONENT_H
#define HUNDE_FILESYSTEM_COMPONENT_H

#include "io_component.h"

#define HUNDE_BLOCK_HOST_VERSION 1U
#define HUNDE_FILESYSTEM_COMPONENT_ENTRY "hunde_filesystem_component_entry_v1"

typedef uint64_t HundeBlockSector;
typedef uint64_t HundeBlockSectorCount;
typedef HundeStatusResult (*HundeBlockRead)(HundeHostContext context, HundeBlockSector sector, uint8_t * destination,
                                            uint64_t length);
typedef HundeStatusResult (*HundeBlockWrite)(HundeHostContext context, HundeBlockSector sector, const uint8_t * source,
                                             uint64_t length);

typedef struct HundeBlockHostOperationsV1
{
    HundeAbiHeader header;
    HundeHostContext context;
    HundeBlockSectorCount sectors;
    HundeBlockRead read;
    HundeBlockWrite write;
} HundeBlockHostOperationsV1;

typedef const HundeBlockHostOperationsV1 * ConstHundeBlockHostOperationsV1Ptr;
typedef HundeIOComponentLoadResult (*HundeFilesystemComponentEntryV1)(ConstHundeIOHostOperationsV6Ptr host,
                                                                      ConstHundeBlockHostOperationsV1Ptr block);

_Static_assert(sizeof(HundeBlockHostOperationsV1) == 40, "Block host ABI changed");
_Static_assert(offsetof(HundeBlockHostOperationsV1, sectors) == 16, "Block geometry ABI changed");
_Static_assert(offsetof(HundeBlockHostOperationsV1, read) == 24, "Block read ABI changed");

#endif
