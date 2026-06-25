#ifndef __AARCH64_EXCEPTION_DECODE_H
#define __AARCH64_EXCEPTION_DECODE_H

#include <stdint.h>
#include <stdbool.h>

const char * AArch64Exception__class_name(uint64_t ec);
const char * AArch64Exception__fault_status_name(uint64_t esr);
const char * AArch64Exception__data_abort_access_name(uint64_t esr);
bool AArch64Exception__has_fault_status(uint64_t ec);
uint64_t AArch64Exception__fault_status_code(uint64_t esr);

#endif
