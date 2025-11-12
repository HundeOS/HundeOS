#ifndef __USER_USER_H_
#define __USER_USER_H_

#include <stdint.h>
#include <stdint.h>

typedef int64_t ByteCount;

void print(const char * str);
ByteCount console_read(char * buffer, uint64_t size);
ByteCount console_read_line(char * buffer, bool echo, uint64_t size);
void exit(int code);

#endif