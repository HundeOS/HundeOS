#ifndef HUND_IO_DIAGNOSTICS_H
#define HUND_IO_DIAGNOSTICS_H
#include "../types/optional.h"
#include <stdint.h>

typedef enum IODiagnosticEvent
{
    IO_DIAGNOSTIC_UART_BYTES,
    IO_DIAGNOSTIC_UART_IRQS,
    IO_DIAGNOSTIC_UART_READS,
    IO_DIAGNOSTIC_UART_EMPTY,
    IO_DIAGNOSTIC_BACKEND_READS,
    IO_DIAGNOSTIC_BACKEND_EMPTY,
    IO_DIAGNOSTIC_WARDEN_RELAYS,
    IO_DIAGNOSTIC_WAIT_ENTRIES,
    IO_DIAGNOSTIC_UNCHANGED_WAKES,
} IODiagnosticEvent;

typedef struct IODiagnostics
{
    uint64_t uart_bytes;
    uint64_t uart_irqs;
    uint64_t uart_reads;
    uint64_t uart_empty;
    uint64_t backend_reads;
    uint64_t backend_empty;
    uint64_t warden_relays;
    uint64_t wait_entries;
    uint64_t unchanged_wakes;
} IODiagnostics;

typedef IODiagnostics * IODiagnosticsPtr;
typedef const IODiagnostics * ConstIODiagnosticsPtr;
OptionalDef(IODiagnostics);
OptionalDef(IODiagnosticsPtr);

// The process owns the counters. They are advisory, never scheduler inputs.
void IODiagnostics__attach(IODiagnosticsPtr self);
void IODiagnostics__record(IODiagnosticEvent event, uint64_t amount);
IODiagnostics IODiagnostics__snapshot(ConstIODiagnosticsPtr self);
#endif
