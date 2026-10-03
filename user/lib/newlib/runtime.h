#ifndef __USER_LIB_NEWLIB_RUNTIME_H
#define __USER_LIB_NEWLIB_RUNTIME_H

void LibC__set_program_name(const char * argument_zero);
void LibC__set_environment(char ** environment);
char * LibC__getenv(const char * name);
void LibC__set_current_directory(const char * path);

#endif
