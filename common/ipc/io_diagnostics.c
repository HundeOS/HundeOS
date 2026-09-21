#include "io_diagnostics.h"

static Optional(IODiagnosticsPtr) counters = None(IODiagnosticsPtr);

void IODiagnostics__attach(IODiagnosticsPtr self)
{
    counters = Some(IODiagnosticsPtr, self);
}

void IODiagnostics__record(IODiagnosticEvent event, uint64_t amount)
{
    match_optional(
        counters,
        some(target)
        {
            switch (event)
            {
            case IO_DIAGNOSTIC_UART_BYTES:
                __atomic_fetch_add(&target->uart_bytes, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_UART_IRQS:
                __atomic_fetch_add(&target->uart_irqs, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_UART_READS:
                __atomic_fetch_add(&target->uart_reads, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_UART_EMPTY:
                __atomic_fetch_add(&target->uart_empty, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_BACKEND_READS:
                __atomic_fetch_add(&target->backend_reads, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_BACKEND_EMPTY:
                __atomic_fetch_add(&target->backend_empty, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_WARDEN_RELAYS:
                __atomic_fetch_add(&target->warden_relays, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_WAIT_ENTRIES:
                __atomic_fetch_add(&target->wait_entries, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_UNCHANGED_WAKES:
                __atomic_fetch_add(&target->unchanged_wakes, amount, __ATOMIC_RELAXED);
                break;
            }
        },
        none_do_nothing()
    );
}

static IODiagnostics IODiagnostics__new(ConstIODiagnosticsPtr self)
{
    return (IODiagnostics){
        .uart_bytes = __atomic_load_n(&self->uart_bytes, __ATOMIC_RELAXED),
        .uart_irqs = __atomic_load_n(&self->uart_irqs, __ATOMIC_RELAXED),
        .uart_reads = __atomic_load_n(&self->uart_reads, __ATOMIC_RELAXED),
        .uart_empty = __atomic_load_n(&self->uart_empty, __ATOMIC_RELAXED),
        .backend_reads = __atomic_load_n(&self->backend_reads, __ATOMIC_RELAXED),
        .backend_empty = __atomic_load_n(&self->backend_empty, __ATOMIC_RELAXED),
        .warden_relays = __atomic_load_n(&self->warden_relays, __ATOMIC_RELAXED),
        .wait_entries = __atomic_load_n(&self->wait_entries, __ATOMIC_RELAXED),
        .unchanged_wakes = __atomic_load_n(&self->unchanged_wakes, __ATOMIC_RELAXED),
    };
}

IODiagnostics IODiagnostics__snapshot(ConstIODiagnosticsPtr self)
{
    return IODiagnostics__new(self);
}
