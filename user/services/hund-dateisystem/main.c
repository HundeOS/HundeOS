#include "namespace.h"
#include "sys/service.h"

int main(void)
{
    static FilesystemNamespace namespace;
    static IOService service;
    namespace = FilesystemNamespace__new();
    if (!service_ok(service_become_service()))
    {
        return 1;
    }
    if (IOService__listen_into(&service, &bstr(IO_FILESYSTEM_SERVICE), &namespace, FilesystemNamespace__execute,
                               FilesystemNamespace__disconnect) != IO_OK)
    {
        return 1;
    }
    if (!service_ok(service_publish_ready(&bstr(IO_FILESYSTEM_SERVICE))))
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
