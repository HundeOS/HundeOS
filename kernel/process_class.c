#include "process_class.h"

bool ProcessClass__is_service(ProcessClass self)
{
    return self == PROCESS_CLASS_SERVICE || self == PROCESS_CLASS_REALTIME_SERVICE;
}

int ProcessClass__priority(ProcessClass self)
{
    switch (self)
    {
        case PROCESS_CLASS_REALTIME_SERVICE:
            return 2;
        case PROCESS_CLASS_SERVICE:
            return 1;
        case PROCESS_CLASS_NORMAL:
        default:
            return 0;
    }
}
