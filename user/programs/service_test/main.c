#include "sys/service.h"
#include "../../user.h"

int main(void)
{
    IO__print(&bstr("service_test: started\n"));

    while (true)
    {
        ServiceEvent event;
        ServiceCode result = service_wait(&event);
        if (!service_ok(result))
        {
            IO__printf(&bstr("service_test: wait error %i\n"), (int) result);
            continue;
        }

        IO__printf(&bstr("service_test: event type=%i source=%i data=%i\n"),
            (int) event.type,
            (int) event.source,
            (int) event.data
        );
    }
}
