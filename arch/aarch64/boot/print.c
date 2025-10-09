#include "print.h"
#include "../io/uart.h"
#include "mutex.h"

static __BOOT_DATA__(boot_mutex) print_lock = {
    .lock = 0
};

inline void _send(unsigned int c) {

    for (int j = 0; j < 1000; j++) {
        __asm__ __volatile__("nop");
        volatile int status = (*P_UART0_FR);
        if (status & 0x20) {
            break;
        }
    }

    *P_UART0_DR = c;
}

__BOOT__(void) _print(const char * str) {
    lock(&print_lock);
    for (const char * p = str; *p; p++ ) {
        _send(*p);
        if (*p == '\n') {
            _send('\r');  // Add carriage return
        }
    }
    unlock(&print_lock);
}


__BOOT_STRING__(HEX_DIGITS) = "0123456789ABCDEF";
__BOOT__(void) _print_base(uint64_t value, const int base) {
    char buffer[64];  // Enough for binary representation + null
    int i = 63;

    buffer[i] = '\0';

    if (value == 0) {
        buffer[--i] = '0';
    } else {
        while (value > 0 && i > 0) {
            buffer[--i] = HEX_DIGITS[value % base];
            value /= base;
        }
    }

    _print(&buffer[i]);
}


__BOOT__(void) _print_unsigned(uint64_t value) {
    _print_base(value, 16);
}


__BOOT__(void) _print_binary(uint64_t value) {
    _print_base(value, 2);
}
