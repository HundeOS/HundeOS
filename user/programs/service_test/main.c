#include "../../sys/service.h"
#include "../../user.h"

int main(void)
{
    print("service_test: started\n");

    while (true)
    {
        ServiceEvent event;
        ServiceCode result = service_wait(&event);
        if (result != SERVICE_OK)
        {
            printf("service_test: wait error %i\n", (int) result);
            continue;
        }

        printf(
            "service_test: event type=%i source=%i data=%i\n",
            (int) event.type,
            (int) event.source,
            (int) event.data
        );
    }
}
