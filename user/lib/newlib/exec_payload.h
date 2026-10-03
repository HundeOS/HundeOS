#ifndef _USER_LIB_NEWLIB_EXEC_PAYLOAD_H
#define _USER_LIB_NEWLIB_EXEC_PAYLOAD_H

#include "syscalls/exec.h"

ExecPayloadPtr PosixExecPayload__new(const char * path, const char * current_directory, char * const arguments[],
                                     char * const environment[],
                                     ConstExecPayloadOpenDescriptionPtr open_descriptions,
                                     size_t open_description_count, ConstExecPayloadDescriptorPtr descriptors,
                                     size_t descriptor_count);
void PosixExecPayload__dispose(ExecPayloadPtr self);

#endif
