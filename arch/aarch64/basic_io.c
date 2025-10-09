#include "io/uart.h"
#include "../kernel/arch/basic_io.h"

void BasicIO__puts(const char* str) {
    puts(str);
}

void BasicIO__put_int(int num) {
    put_int(num);
}