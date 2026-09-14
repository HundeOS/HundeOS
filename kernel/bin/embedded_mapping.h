#ifndef HUND_KERNEL_EMBEDDED_MAPPING_H
#define HUND_KERNEL_EMBEDDED_MAPPING_H
#include "../process.h"
#include "fs/embedded_fs.h"

Result(ProcessSetupStatus) EmbeddedArchive__map_process(ProcessPtr self);
#endif
