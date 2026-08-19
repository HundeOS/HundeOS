#ifndef __TEST_KERNEL_FIXTURES_IO_SUBSYSTEM_H
#define __TEST_KERNEL_FIXTURES_IO_SUBSYSTEM_H

#include "../../../kernel/subsystems/io/io_subsystem.h"
#include "../../../kernel/subsystems/io/file/filesystem.h"

FilesystemPtr init_root_filesystem_with_embedded_binaries(KernelPtr kernel);

#endif
