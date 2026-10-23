#ifndef HUNDE_BOOTSTRAP_ARCHIVE_H
#define HUNDE_BOOTSTRAP_ARCHIVE_H

#include "fs/embedfs.h"

typedef struct BootstrapArchive
{
    Embedfs filesystem;
    IOService service;
    bool draining;
} BootstrapArchive;

typedef BootstrapArchive * BootstrapArchivePtr;

Optional(IOReply)
    BootstrapArchive__execute(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request);
bool BootstrapArchive__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid);

#endif
