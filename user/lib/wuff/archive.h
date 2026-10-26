#ifndef HUNDE_WUFF_ARCHIVE_H
#define HUNDE_WUFF_ARCHIVE_H

#include "fs/embedded_fs.h"
#include "loader.h"

ResultDef(EmbeddedFs);

Result(EmbeddedFs) WuffArchive__mapped(void);
Result(WuffImage) WuffArchive__load_image(EmbeddedFsPtr archive, ConstBStringPtr path);
Result(WuffGraph)
    WuffArchive__load_module(EmbeddedFsPtr archive, ConstBStringPtr path, ConstWuffMemoryOperationsPtr memory);
WuffDependencyOperations WuffArchive__dependencies(EmbeddedFsPtr archive);

#endif
