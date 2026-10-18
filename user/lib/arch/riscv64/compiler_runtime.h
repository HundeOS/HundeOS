#ifndef HUNDE_USER_LIB_ARCH_RISCV64_COMPILER_RUNTIME_H
#define HUNDE_USER_LIB_ARCH_RISCV64_COMPILER_RUNTIME_H

#include <stdint.h>

double __adddf3(double left, double right);
double __subdf3(double left, double right);
double __muldf3(double left, double right);
double __divdf3(double left, double right);
double __floatsidf(int32_t value);
double __floatunsidf(uint32_t value);
double __floatdidf(int64_t value);
double __floatundidf(uint64_t value);
int __eqdf2(double left, double right);
int __ltdf2(double left, double right);
int __ledf2(double left, double right);
int __gtdf2(double left, double right);
int __gedf2(double left, double right);
int __unorddf2(double left, double right);

#endif
