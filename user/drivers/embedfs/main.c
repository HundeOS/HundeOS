#include "bootstrap.h"
#include "sys/service.h"

int main(void)
{
    static BootstrapArchive archive_service;
    EmbeddedFsHeaderPtr header = (EmbeddedFsHeaderPtr)EMBEDDED_FS_VADDR;
    if (header->blob_size > EMBEDDED_FS_MAX_SIZE)
    {
        return 1;
    }
    EmbeddedFs archive = EmbeddedFs__new(EMBEDDED_FS_VADDR, EMBEDDED_FS_VADDR + header->blob_size);
    if (!EmbeddedFs__is_valid(&archive) || !service_ok(service_become_service()))
    {
        return 1;
    }
    Embedfs__init(&archive_service.filesystem, &archive);
    if (IOService__listen_into(&archive_service.service, &bstr("/sbin"), &archive_service, BootstrapArchive__execute,
                               BootstrapArchive__disconnect) != IO_OK)
    {
        return 1;
    }
    if (!service_ok(service_publish_ready(&bstr("/sbin"))))
    {
        return 1;
    }
    Channel__prepare_wait();
    while (true)
    {
        if (!IOService__poll(&archive_service.service))
        {
            Channel__wait_event();
        }
    }
}
