#include "procfs.h"
#include "sys/service.h"

int main(void)
{
    static FilesystemDriver driver;
    if (!service_ok(service_become_service()))
    {
        return 1;
    }
    FilesystemOperations operations = Procfs__operations();
    driver = FilesystemDriver__new((IOServiceContext)ProcessRegistry__shared(), &operations);
    return FilesystemDriver__run(&driver, &bstr("/proc"));
}
