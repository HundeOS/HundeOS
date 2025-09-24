void halt() {
    while(1) {
        __asm__ __volatile__("wfi");
    }
}
