#include "../../user.h"
#include "sys/auth.h"
#include "sys/spawn.h"

#ifndef LOGIN_ENTRYPOINT
#define LOGIN_ENTRYPOINT main
#endif

int LOGIN_ENTRYPOINT(void)
{
    while (true)
    {
        IO__printf(&bstr("login: "));
        BString username = Console__read_line(true);
        IO__print(&bstr("\n"));

        IO__printf(&bstr("password: "));
        BString password = Console__read_line(false);
        IO__print(&bstr("\n"));

        AuthResult ticket = auth(&username, &password);
        if (ticket < 0)
        {
            IO__print(&bstr("login: invalid credentials\n"));
            continue;
        }

        SpawnResult result = spawn_with_auth(
            P_WAIT,
            (AuthTicketID) ticket,
            &bstr("/sbin/shell"),
            &SPAWN_NO_ARG,
            &SPAWN_NO_ARG,
            &SPAWN_NO_ARG
        );
        if (result < 0)
        {
            IO__printf(&bstr("login: failed to start shell: %i\n"), (int) result);
            return 1;
        }
    }
}
