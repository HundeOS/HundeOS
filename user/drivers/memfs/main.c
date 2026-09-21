#include "memfs.h"
#include "sys/service.h"

int main(int argc, char ** argv)
{
    if (argc != 2 || !service_ok(service_become_service()))
    {
        return 1;
    }
    BString mountpoint = BString__new(argv[1]);
    static Memfs filesystem;
    static IOService service;
    filesystem = Memfs__new();
    if (IOService__listen_into(&service, &mountpoint, &filesystem, Memfs__serve, Memfs__disconnect) != IO_OK)
    {
        return 1;
    }
    if (!service_ok(service_publish_ready(&mountpoint)))
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
