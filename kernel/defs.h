#ifndef __KERNEL_DEFS_H_
#define __KERNEL_DEFS_H_

#ifdef __APPLE__
// Mach-O adds a leading underscore to symbol names
#define EXTERN_SYM(x) _##x
#else
#define EXTERN_SYM(x) x
#endif

#endif