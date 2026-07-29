#define barrier_dsb_sy() __asm__ volatile("dsb sy" ::: "memory")
#define barrier_isb_sy() __asm__ volatile("isb" ::: "memory")

void __sync_boundary()
{
    __sync_synchronize();

    barrier_dsb_sy();
    barrier_isb_sy();
}

