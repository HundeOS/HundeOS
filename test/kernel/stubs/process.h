#ifndef __KERNEL_STUBS_PROCESS_H
#define __KERNEL_STUBS_PROCESS_H

bool process_is_started(Process * proc);
void reset_started_processes();

#endif