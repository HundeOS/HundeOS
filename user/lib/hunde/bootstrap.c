#include "native.h"
#include "wuff/archive.h"
#include <string.h>

static HundeIOCompletion HundeIOCompletion__new_bootstrap(ByteCount count)
{
    return (HundeIOCompletion){.kind = HUNDE_ABI_SUCCESS, .value = count};
}

static HundeIOCompletion HundeIOCompletion__new_bootstrap_error(Code code)
{
    return (HundeIOCompletion){.kind = HUNDE_ABI_ERROR, .error = {.code = code}};
}

HundeIOCompletion hunde_rust_bootstrap_read(HundeByteSpan path, uint8_t * buffer, size_t capacity)
{
    if (path.length == 0 || path.length >= IO_PROTOCOL_DATA_SIZE)
    {
        return HundeIOCompletion__new_bootstrap_error(IO_INVALID_PATH);
    }
    BString name = BString__from_chars((const char *)path.bytes, path.length);
    match_result(
        WuffArchive__mapped(),
        success(archive)
        {
            match_result(
                WuffArchive__load_image(&archive, &name),
                success(image)
                {
                    if (image.size > capacity)
                    {
                        return HundeIOCompletion__new_bootstrap_error(IO_UNKNOWN_BUFFER);
                    }
                    memcpy(buffer, image.data, image.size);
                    return HundeIOCompletion__new_bootstrap(image.size);
                },
                error(code, message)
                {
                    return HundeIOCompletion__new_bootstrap_error(code);
                }
            );
        },
        error(code, message)
        {
            return HundeIOCompletion__new_bootstrap_error(code);
        }
    );
    return HundeIOCompletion__new_bootstrap_error(RESULT_UNKNOWN_ERROR_CODE);
}
