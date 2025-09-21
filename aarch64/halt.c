void halt() {
    while(1) {
        asm volatile("wfi");
    }
}
