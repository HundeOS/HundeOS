#ifndef __USER_LIB_WUFF_FILESYSTEM_H
#define __USER_LIB_WUFF_FILESYSTEM_H

#include "loader.h"

#define WUFF_LIBRARY_DIRECTORY "/usr/lib/"
#define WUFF_MAX_IMAGE_SIZE (16ULL * 1024ULL * 1024ULL)

Result(WuffImage) WuffFilesystem__load_image(ConstBStringPtr path);
void WuffFilesystem__release_image(ConstWuffImagePtr image);
WuffDependencyOperations WuffFilesystem__dependencies(void);

#endif
