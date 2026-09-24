#ifndef __AARCH64_EXCEPTION_DECODE_H
#define __AARCH64_EXCEPTION_DECODE_H

#include <stdint.h>
#include <stdbool.h>
#include "../../kernel/arch/exception.h"

const char * Exception__class_name(uint64_t ec);
const char * Exception__fault_status_name(ExceptionSyndrome syndrome);
const char * Exception__data_abort_access_name(ExceptionSyndrome syndrome);
bool Exception__has_fault_status(uint64_t ec);
uint64_t Exception__fault_status_code(ExceptionSyndrome syndrome);

#endif
