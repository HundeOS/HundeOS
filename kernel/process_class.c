#include "process_class.h"

bool ProcessClass__is_service(ProcessClass self)
{
    return self == PROCESS_CLASS_SERVICE || self == PROCESS_CLASS_REALTIME_SERVICE;
}

uint64_t ProcessClass__priority(ProcessClass self)
{
    switch (self)
    {
    case PROCESS_CLASS_REALTIME_SERVICE:
        return 2;
    case PROCESS_CLASS_SERVICE:
    case PROCESS_CLASS_NORMAL:
    default:
        return 1;
    }
}

const char * ProcessClass__to_string(ProcessClass self)
{
    switch (self)
    {
    case PROCESS_CLASS_REALTIME_SERVICE:
        return "realtime_service";
    case PROCESS_CLASS_SERVICE:
        return "service";
    case PROCESS_CLASS_NORMAL:
        return "normal";
    default:
        return "unknown";
    }
}
