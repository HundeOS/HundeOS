#ifndef __TEST_KERNEL_FIXTURES_KERNEL_H
#define __TEST_KERNEL_FIXTURES_KERNEL_H

#include "../../../kernel/kernel_core.h"
#include "../../../kernel/subsystems/io/file/filesystem.h"

KernelPtr init_test_kernel();
KernelPtr init_test_kernel_with_filesystem(FilesystemPtr filesystem);
KernelPtr init_test_kernel_without_filesystem();

#endif