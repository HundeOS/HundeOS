#include "../../user.h"
#include "../../sys/auth.h"
#include "../../sys/spawn.h"

#ifndef LOGIN_ENTRYPOINT
#define LOGIN_ENTRYPOINT main
#endif

int LOGIN_ENTRYPOINT(void)
{
    while (true)
    {
        printf("login: ");
        BString username = Console__read_line(true);
        print("\n");

        printf("password: ");
        BString password = Console__read_line(false);
        print("\n");

        AuthResult ticket = auth(BString__cstr(&username), BString__cstr(&password));
        if (ticket < 0)
        {
            print("login: invalid credentials\n");
            continue;
        }

        SpawnResult result = spawn_with_auth(P_WAIT, (AuthTicketID) ticket, "/sbin/shell", NULL, NULL, NULL);
        if (result < 0)
        {
            printf("login: failed to start shell: %i\n", (int) result);
            return 1;
        }
    }
}
