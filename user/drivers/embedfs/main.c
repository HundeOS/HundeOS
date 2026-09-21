#include "embedfs.h"
#include "sys/service.h"

int main(void)
{
    static Embedfs filesystem;
    static IOService service;
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
    filesystem = Embedfs__new(&archive);
    if (IOService__listen_into(&service, &bstr("/sbin"), &filesystem, Embedfs__serve, Embedfs__disconnect) != IO_OK)
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
        if (!IOService__poll(&service))
        {
            Channel__wait_event();
        }
    }
}
