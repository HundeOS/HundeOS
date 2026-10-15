#include "procfs.h"
#include "sys/process.h"
#include "sys/service.h"
#include "sys/system_info.h"

int main(void)
{
    static FilesystemDriver driver;
    static ProcfsContext context;
    if (!service_ok(service_become_service()))
    {
        return 1;
    }
    FilesystemOperations operations = Procfs__operations();
    match_result(
        process_registry_map(),
        success(registry)
        {
            context = ProcfsContext__new(registry, KernelTrace__load);
            driver = FilesystemDriver__new(&context, &operations);
            return FilesystemDriver__run(&driver, &bstr("/proc"));
        },
        error(code, message)
        {
            return 1;
        }
    );
}
