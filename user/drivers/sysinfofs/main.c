#include "sysinfofs.h"
#include "sys/service.h"

int main(void)
{
    if (!service_ok(service_become_service()))
    {
        return 1;
    }
    static FilesystemDriver driver;
    FilesystemOperations operations = Sysinfofs__operations();
    driver = FilesystemDriver__new(&driver, &operations);
    return FilesystemDriver__run(&driver, &bstr("/boot"));
}
