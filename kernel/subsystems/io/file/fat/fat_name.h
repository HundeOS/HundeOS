#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_FAT_FAT_NAME_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_FAT_FAT_NAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../../../types/bstring.h"
#include "../../../../types/bstring_additional_types.h"

#define FAT_NAME_83_SIZE 11

bool FatName__is_valid_83_char(char ch);
bool FatName__encode_83(ConstBStringPtr name, uint8_t target[FAT_NAME_83_SIZE]);

#endif
