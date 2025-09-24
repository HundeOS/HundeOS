void noop() {
    __asm__ __volatile__("nop");
}

void wait_for_interrupt() {
    __asm__ __volatile__("wfe");
}

void cpu_relax(void) {
    __asm__ __volatile__("yield");
}