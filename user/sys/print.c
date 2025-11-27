#include <stdarg.h>
#include <stdint.h>

#include "syscall.h"

void print(const char * str)
{
    syscall(PRINT, (uint64_t)str, 0, 0, 0, 0);
}


const char * HEX_DIGITS = "0123456789ABCDEF";

int sprintf_impl(char* str, const char* format, va_list args)
{
    int count = 0;
    int str_idx = 0;

    for (int i = 0; format[i] != '\0'; i++) {
        if (format[i] != '%') {
            str[str_idx++] = format[i];
            count++;
        } else {
            i++;

            // Skip width parsing for simplicity in sprintf
            while (format[i] >= '0' && format[i] <= '9') {
                i++;
            }

            switch (format[i]) {
                case 'd':
                case 'i': {
                    int value = va_arg(args, int);

                    // Convert integer to string
                    char temp[20];
                    int temp_idx = 0;
                    int is_negative = 0;

                    if (value < 0) {
                        is_negative = 1;
                        value = -value;
                    }

                    if (value == 0) {
                        temp[temp_idx++] = '0';
                    } else {
                        int start = temp_idx;
                        while (value > 0) {
                            temp[temp_idx++] = '0' + (value % 10);
                            value /= 10;
                        }
                        // Reverse the digits
                        for (int j = 0; j < (temp_idx - start) / 2; j++) {
                            char t = temp[start + j];
                            temp[start + j] = temp[temp_idx - 1 - j];
                            temp[temp_idx - 1 - j] = t;
                        }
                    }

                    if (is_negative) {
                        str[str_idx++] = '-';
                        count++;
                    }

                    for (int j = 0; j < temp_idx; j++) {
                        str[str_idx++] = temp[j];
                        count++;
                    }
                    break;
                }

                case 's': {
                    char* s = va_arg(args, char*);
                    if (!s) s = "(null)";
                    while (*s) {
                        str[str_idx++] = *s++;
                        count++;
                    }
                    break;
                }

                case 'c': {
                    int c = va_arg(args, int);
                    str[str_idx++] = (char)c;
                    count++;
                    break;
                }
                case 'p':
                case 'x': {
                    unsigned int value = va_arg(args, unsigned int);
                    char temp[20];
                    int temp_idx = 0;

                    if (value == 0) {
                        temp[temp_idx++] = '0';
                    } else {
                        int start = temp_idx;
                        while (value > 0) {
                            temp[temp_idx++] = HEX_DIGITS[value % 16];
                            value /= 16;
                        }
                        // Reverse
                        for (int j = 0; j < (temp_idx - start) / 2; j++) {
                            char t = temp[start + j];
                            temp[start + j] = temp[temp_idx - 1 - j];
                            temp[temp_idx - 1 - j] = t;
                        }
                    }
                    if (format[i] == 'p')
                    {
                        str[str_idx++] = '0';
                        str[str_idx++] = 'x';
                        count += 2;
                    }
                    for (int j = 0; j < temp_idx; j++) {
                        str[str_idx++] = temp[j];
                        count++;
                    }
                    break;
                }

                case '%': {
                    str[str_idx++] = '%';
                    count++;
                    break;
                }

                default:
                    break;
            }
        }
    }

    str[str_idx] = '\0';
    return count;
}

int printf(const char * format, ...)
{
    char buf[4096];
    va_list args;
    va_start(args, format);
    int count = sprintf_impl(buf, format, args);
    va_end(args);
    print(buf);
    return count;
}
