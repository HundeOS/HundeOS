#include "print.h"
#include <stdint.h>

extern void puts(const char* str);
extern void put_int(int num);

void k_println(const char * str) {
    puts(str);
    puts("\n");
}

// Helper function to output a single character using puts
static void putchar(char c) {
    char str[2] = {c, '\0'};
    puts(str);
}

// Helper function to output a string character by character
static void print_string(const char* str) {
    if (!str) {
        puts("(null)");
        return;
    }
    puts(str);
}

// Helper function to convert and print unsigned integer
static void print_unsigned(unsigned int value, int base, bool uppercase) {
    char buffer[33];  // Enough for binary representation + null
    char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 32;
    
    buffer[i] = '\0';
    
    if (value == 0) {
        buffer[--i] = '0';
    } else {
        while (value > 0 && i > 0) {
            buffer[--i] = digits[value % base];
            value /= base;
        }
    }
    
    puts(&buffer[i]);
}

// Helper function to print pointer
static void print_pointer(void* ptr) {
    puts("0x");
    uintptr_t value = (uintptr_t)ptr;
    
    char buffer[17];  // 64-bit pointer = 16 hex chars + null
    int i = 16;
    buffer[i] = '\0';
    
    for (int j = 0; j < 16; j++) {
        buffer[--i] = "0123456789abcdef"[value & 0xF];
        value >>= 4;
    }
    
    // Skip leading zeros but keep at least one
    i = 0;
    while (i < 15 && buffer[i] == '0') i++;
    
    puts(&buffer[i]);
}

// Helper to get string length
static int strlen(const char* str) {
    int len = 0;
    if (!str) return 0;
    while (str[len]) len++;
    return len;
}

// Helper to copy n characters from src to dest, returns number copied
static int strncpy_len(char* dest, const char* src, int n) {
    int i = 0;
    while (i < n && src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
    return i;
}

// Simple printf implementation
int k_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    int count = 0;
    char buffer[256];  // Buffer for building output strings
    int buf_idx = 0;
    
    for (int i = 0; format[i] != '\0'; i++) {
        if (format[i] != '%') {
            // Regular character - add to buffer
            buffer[buf_idx++] = format[i];
            
            // Flush buffer if it's getting full or we hit a newline
            if (buf_idx >= 255 || format[i] == '\n') {
                buffer[buf_idx] = '\0';
                puts(buffer);
                count += buf_idx;
                buf_idx = 0;
            }
        } else {
            // Flush any pending regular text
            if (buf_idx > 0) {
                buffer[buf_idx] = '\0';
                puts(buffer);
                count += buf_idx;
                buf_idx = 0;
            }
            
            // Handle format specifier
            i++;
            
            // Handle flags (simplified - just padding)
            int width = 0;
            bool zero_pad = false;
            bool left_align = false;
            
            // Check for flags
            while (format[i] == '-' || format[i] == '0') {
                if (format[i] == '0') zero_pad = true;
                if (format[i] == '-') left_align = true;
                i++;
            }
            
            // Parse width
            while (format[i] >= '0' && format[i] <= '9') {
                width = width * 10 + (format[i] - '0');
                i++;
            }
            
            // Handle the conversion specifier
            switch (format[i]) {
                case 'd':
                case 'i': {
                    int value = va_arg(args, int);
                    put_int(value);
                    
                    // Estimate digits for count (simplified)
                    if (value < 0) count++;  // minus sign
                    if (value == 0) count++;
                    else {
                        int temp = value < 0 ? -value : value;
                        while (temp > 0) {
                            count++;
                            temp /= 10;
                        }
                    }
                    break;
                }
                
                case 'u': {
                    unsigned int value = va_arg(args, unsigned int);
                    print_unsigned(value, 10, false);
                    
                    // Count digits
                    if (value == 0) count++;
                    else {
                        unsigned int temp = value;
                        while (temp > 0) {
                            count++;
                            temp /= 10;
                        }
                    }
                    break;
                }
                
                case 'x': {
                    unsigned int value = va_arg(args, unsigned int);
                    print_unsigned(value, 16, false);
                    
                    // Count hex digits
                    if (value == 0) count++;
                    else {
                        unsigned int temp = value;
                        while (temp > 0) {
                            count++;
                            temp /= 16;
                        }
                    }
                    break;
                }
                
                case 'X': {
                    unsigned int value = va_arg(args, unsigned int);
                    print_unsigned(value, 16, true);
                    
                    // Count hex digits
                    if (value == 0) count++;
                    else {
                        unsigned int temp = value;
                        while (temp > 0) {
                            count++;
                            temp /= 16;
                        }
                    }
                    break;
                }
                
                case 'o': {
                    unsigned int value = va_arg(args, unsigned int);
                    print_unsigned(value, 8, false);
                    
                    // Count octal digits
                    if (value == 0) count++;
                    else {
                        unsigned int temp = value;
                        while (temp > 0) {
                            count++;
                            temp /= 8;
                        }
                    }
                    break;
                }
                
                case 'p': {
                    void* ptr = va_arg(args, void*);
                    print_pointer(ptr);
                    count += 2;  // "0x"
                    
                    // Count hex digits in pointer
                    uintptr_t value = (uintptr_t)ptr;
                    if (value == 0) count++;
                    else {
                        while (value > 0) {
                            count++;
                            value /= 16;
                        }
                    }
                    break;
                }
                
                case 'c': {
                    int c = va_arg(args, int);
                    putchar((char)c);
                    count++;
                    break;
                }
                
                case 's': {
                    char* str = va_arg(args, char*);
                    print_string(str);
                    count += strlen(str);
                    break;
                }
                
                case '%': {
                    putchar('%');
                    count++;
                    break;
                }
                
                default: {
                    // Unknown format specifier, just print it
                    putchar('%');
                    putchar(format[i]);
                    count += 2;
                    break;
                }
            }
        }
    }
    
    // Flush any remaining buffer content
    if (buf_idx > 0) {
        buffer[buf_idx] = '\0';
        puts(buffer);
        count += buf_idx;
    }
    
    va_end(args);
    return count;
}

// Bonus: sprintf implementation
int k_sprintf(char* str, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
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
                
                case 'x': {
                    unsigned int value = va_arg(args, unsigned int);
                    char hex_chars[] = "0123456789abcdef";
                    char temp[20];
                    int temp_idx = 0;
                    
                    if (value == 0) {
                        temp[temp_idx++] = '0';
                    } else {
                        int start = temp_idx;
                        while (value > 0) {
                            temp[temp_idx++] = hex_chars[value % 16];
                            value /= 16;
                        }
                        // Reverse
                        for (int j = 0; j < (temp_idx - start) / 2; j++) {
                            char t = temp[start + j];
                            temp[start + j] = temp[temp_idx - 1 - j];
                            temp[temp_idx - 1 - j] = t;
                        }
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
    va_end(args);
    return count;
}