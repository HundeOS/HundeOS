#include "channel/block_client.h"
#include "sys/service.h"

static bool ChannelFs__exercise(BlockClientPtr self)
{
    uint8_t expected[BLOCK_CHANNEL_SECTOR_SIZE];
    uint8_t actual[BLOCK_CHANNEL_SECTOR_SIZE];
    for (size_t index = 0; index < sizeof(expected); index++)
    {
        expected[index] = (uint8_t)index;
    }
    match_result(
        BlockClient__write(self, 0, 1, (Address)expected, sizeof(expected)),
        success(count)
        {
            if (count != 1)
            {
                return false;
            }
        },
        error(code, message)
        {
            return false;
        }
    );
    match_result(
        BlockClient__read(self, 0, 1, (Address)actual, sizeof(actual)),
        success(count)
        {
            if (count != 1)
            {
                return false;
            }
        },
        error(code, message)
        {
            return false;
        }
    );
    for (size_t index = 0; index < sizeof(actual); index++)
    {
        if (actual[index] != expected[index])
        {
            return false;
        }
    }
    return true;
}

int main(int argc, char ** argv)
{
    if (argc != 2 || !service_ok(service_become_service()))
    {
        return 1;
    }
    BString path = BString__new(argv[1]);
    BlockClient client;
    match_result(BlockClient__open(&path),
        success(value)
        {
            client = value;
        },
        error(code, message)
        {
            return 1;
        }
    );
    bool passed = ChannelFs__exercise(&client);
    ChannelCode closed = BlockClient__close(&client);
    return passed && closed == CHANNEL_OK && service_ok(service_publish_ready(&bstr("channel_fs"))) ? 0 : 1;
}
