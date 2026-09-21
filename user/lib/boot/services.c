#include "services.h"
#include "sys/service.h"
static Result(ServiceCode) BootService__wait(ConstResultPtr(SpawnResult) spawned, ConstBStringPtr ready)
{
    match_result(
        *spawned,
        success(pid)
        {
            ServiceCode status = service_wait_ready(ready);
            return service_ok(status) ? Success(ServiceCode, status)
                                      : Error(ServiceCode, bstr("Boot service not ready"), status);
        },
        error(code, message)
        {
            return Error(ServiceCode, message, code);
        }
    );
}

Result(ServiceCode)
    BootService__start(ConstBStringPtr path, ConstBStringPtr arg0, ConstBStringPtr arg1, ConstBStringPtr ready)
{
    Optional(BString) first = SpawnArg__from_bstring(arg0);
    Optional(BString) second = SpawnArg__from_bstring(arg1);
    Result(SpawnResult) spawned = OS__spawn(P_NOWAIT, path, &first, &second, &SPAWN_NO_ARG);
    return BootService__wait(&spawned, ready);
}

Result(ServiceCode)
    BootService__bootstrap(ConstBStringPtr path, ConstBStringPtr arg0, ConstBStringPtr arg1, ConstBStringPtr ready)
{
    Optional(BString) first = SpawnArg__from_bstring(arg0);
    Optional(BString) second = SpawnArg__from_bstring(arg1);
    Result(SpawnResult) spawned = OS__boot_spawn(path, &first, &second, &SPAWN_NO_ARG);
    return BootService__wait(&spawned, ready);
}
