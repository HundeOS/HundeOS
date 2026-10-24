#ifndef HUNDE_RUST_FILESYSTEM_H
#define HUNDE_RUST_FILESYSTEM_H

#include "native.h"

ResultDef(IORequest);
Result(IORequest) IORequest__new_rust(ConstHundeIORequestV6Ptr input);
HundeIOCompletion HundeIOCompletion__new_reply(IORequestOperation operation, IOReply reply);

#endif
