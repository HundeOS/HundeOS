#include "../../user.h"
#include "../../sys/spawn.h"

#define LOGIN_INPUT_MAX 64

#ifndef LOGIN_ENTRYPOINT
#define LOGIN_ENTRYPOINT main
#endif

typedef struct LoginCredentials
{
    const char * username;
    const char * password;
    UserID user_id;
} LoginCredentials;

static const LoginCredentials LOGIN__CREDENTIALS[] = {
    { "root", "root", PROCESS_USER_ROOT },
    { "hund", "hund", PROCESS_USER_HUND },
    { "user", "user", PROCESS_USER_USER },
};
static const uint64_t LOGIN__CREDENTIAL_COUNT = sizeof(LOGIN__CREDENTIALS) / sizeof(LOGIN__CREDENTIALS[0]);

static bool Login__matches(const char * left, const char * right)
{
    uint64_t i = 0;
    while (left[i] != '\0' && right[i] != '\0')
    {
        if (left[i] != right[i])
        {
            return false;
        }
        i++;
    }

    return left[i] == '\0' && right[i] == '\0';
}

static bool Login__authenticate(ConstBStringPtr username, ConstBStringPtr password, UserID * user_id)
{
    for (uint64_t i = 0; i < LOGIN__CREDENTIAL_COUNT; i++)
    {
        if (
            Login__matches(BString__cstr(username), LOGIN__CREDENTIALS[i].username)
            && Login__matches(BString__cstr(password), LOGIN__CREDENTIALS[i].password)
        )
        {
            *user_id = LOGIN__CREDENTIALS[i].user_id;
            return true;
        }
    }

    return false;
}

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

        UserID user_id = PROCESS_USER_USER;
        if (!Login__authenticate(&username, &password, &user_id))
        {
            printf("login: invalid credentials\n");
            continue;
        }

        SpawnResult result = spawn_as(P_WAIT, user_id, "/sbin/shell", NULL, NULL, NULL);
        if (result < 0)
        {
            printf("login: failed to start shell: %i\n", (int) result);
            return 1;
        }
    }
}
