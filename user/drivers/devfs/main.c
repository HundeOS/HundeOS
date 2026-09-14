#include "devfs.h"
#include "sys/service.h"

int main(void)
{
    static FilesystemDriver driver;
    if (!service_ok(service_become_service()))
    {
        return 1;
    }
    FilesystemOperations operations = Devfs__operations();
    driver = FilesystemDriver__new((IOServiceContext)DeviceRegistry__shared(), &operations);
    return FilesystemDriver__run(&driver, &bstr("/dev"));
}
