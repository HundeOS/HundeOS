#include "process_types.h"

BString ParkReason__to_string(ParkReason reason)
{
    switch (reason)
    {
    case PARK_WAIT_CHILD:
        return bstr("PARK_WAIT_CHILD");
    case PARK_WAIT_IO:
        return bstr("PARK_WAIT_IO");
    case PARK_WAIT_SERVICE_EVENT:
        return bstr("PARK_WAIT_SERVICE_EVENT");
    case PARK_WAIT_CHILD_EVENT:
        return bstr("PARK_WAIT_CHILD_EVENT");
    case PARK_WAIT_SERVICE_READY:
        return bstr("PARK_WAIT_SERVICE_READY");
    case PARK_SLEEP:
        return bstr("PARK_SLEEP");
    default:
        return bstr("PARK_WAIT_UNKNOWN");
    }
}
