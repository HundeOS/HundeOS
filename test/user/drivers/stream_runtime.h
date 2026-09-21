#ifndef TEST_STREAM_RUNTIME_H
#define TEST_STREAM_RUNTIME_H
#include "io/stream_driver.h"
#include "sys/service.h"
int StreamDriverTest__run(int (*entry)(void));
void StreamDriverTest__step(IOStreamDriverPtr driver);
void StreamDriverTest__stop(void);
#endif
