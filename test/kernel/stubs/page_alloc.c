#define _4MB 0x400000
#define OFFSET 0x10000000

const char __heap_start[_4MB];
const void * __heap_end = __heap_start + _4MB;
const void * __heap_start_phys = __heap_start + OFFSET; // just fake it to the virtual addr
