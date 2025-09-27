void noop() {
    __asm__ __volatile__("nop");
}

void cpu_wait() {
    __asm__ __volatile__("wfe");
}

void cpu_yield(void) {
    __asm__ __volatile__("yield");
}