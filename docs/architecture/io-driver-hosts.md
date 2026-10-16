# I/O and driver hosts

## Status and target

This document defines the agreed consolidation of normal I/O execution into two
trusted userspace processes:

- `hunde-io-host` owns application-facing I/O policy and high-level components.
- `hunde-treiber-host` owns hardware-facing mechanisms and device drivers.

The target is not yet implemented. The current `hund-io-system` remains a
stateless router, filesystem and protocol implementations run as separate
services, and `hund-unterbrict` is the initial driver host. VirtIO block already
runs inside `hund-unterbrict` and receives hosted IRQ dispatch through local
calls. Migration must preserve working service protocols until their ownership
moves into a host.

This is consolidation of I/O execution, not consolidation of every userspace
service. `hunde-mama`, `hundekanal-warter`, `hunde-puffer`, tracing, and other
system coordination services retain their existing responsibilities. Kernel
boot, panic, and diagnostic output remains independent of both hosts.

## Topology

```text
applications
    |
    | authenticated Channel
    v
hunde-io-host
    namespace, mounts, handles, filesystems, sockets, streams, caches
    |
    | authenticated Channel and managed shared buffers
    v
hunde-treiber-host
    IRQ routing, MMIO, DMA, block, network, input, and console devices
    |
    v
hardware
```

An application operation crosses into `hunde-io-host` once. High-level routing
and protocol composition within that host use local calls, not Channels between
modules. An operation that reaches hardware crosses once more into
`hunde-treiber-host`. Replies follow the same two process boundaries. Cache hits
and operations implemented entirely by the I/O host do not enter the driver
host.

Existing routing redirects occur mainly during resource open, and current file
data already travels directly between applications, filesystem services, and
block drivers. The consolidation must therefore be evaluated with measurements
rather than assuming every path loses a process boundary. Record open latency,
cache-hit latency, cache-miss latency, Channel publications, scheduling
transitions, and representative network paths before and after migration.

## `hunde-io-host` responsibilities

`hunde-io-host` owns:

- the common application-facing I/O endpoint and authenticated session state;
- namespaces, mount routing, paths, open resources, and handle ownership;
- filesystem, socket, stream, pipe, terminal, and system-information components;
- filesystem metadata and data caches, network protocol state, and any shared
  raw-block cache introduced for current callers; and
- coordination of buffer leases and outstanding high-level operations.

Filesystem and protocol components run in the host address space and receive a
host-created request context derived from authenticated Channel metadata. A
module-provided PID, session, or handle is never authoritative. Local dispatch
must retain the existing ownership, access, stale-handle, cleanup, and progress
rules described in [Userspace I/O system](io-system.md).

Caching remains I/O policy. The driver host may retain device queue state and
hardware-specific metadata, but it does not own a general filesystem or block
cache. A shared raw-block cache, if required by measured callers, belongs in the
I/O host so write ordering, invalidation, flush, discard, and filesystem
coherence remain above the hardware boundary.

## `hunde-treiber-host` responsibilities

`hunde-treiber-host` evolves from `hund-unterbrict` and owns:

- delegated device IRQ receipt, dispatch, completion validation, and rearming;
- device discovery, claims, MMIO mappings, DMA rings, and hardware reset;
- block, network-interface, input, UART, display, and other hardware drivers;
- per-device fibres and progress while other devices or clients are blocked; and
- fail-closed cleanup of device claims and IRQ routes if the host exits.

Hosted IRQ delivery and acknowledgement use local calls as already implemented
for VirtIO block. Hardware drivers do not use a Channel to communicate with an
interrupt component in the same host. Device interrupt sources are still
cleared before the host rearms them, and kernel-owned interrupt entry, masking,
authentication, and privileged controller operations remain in the kernel.

The driver host exposes stable device operations to the I/O host over existing
Channel protocols. It contains hardware mechanism, not filesystem, socket, or
application policy.

## Buffers and data movement

Bulk bytes continue to use directional leases managed by `hunde-puffer`.
Channels carry requests, descriptors, commands, and completions. Host
consolidation must not introduce private message rings, inline bulk payloads, or
unmanaged shared mappings.

The I/O and driver hosts receive only the attachments and permissions required
for the current operation. When an application buffer can safely participate in
the complete path, the driver host may perform DMA against that managed lease.
Transforming components may require a separate lease or copy; the architecture
does not promise universal zero-copy operation. Completion, cancellation, and
DMA quiescence must precede lease release or recycling.

See [Shared buffers: hunde-puffer](shared-buffers.md).

## Components and failure domains

Dynamically loaded components share all memory and authority of their host.
Language memory safety reduces accidental corruption but is not a security
boundary. A component can still deadlock the event loop, panic, misuse an
authorized hardware operation, or leave DMA active. The two processes are the
isolation and failure boundaries; components inside a host are trusted.

Host-facing component interfaces must:

- use a versioned C ABI and fixed-width, explicitly laid-out values;
- receive host operations explicitly rather than resolving private host state;
- keep language-specific objects and ABIs behind the module boundary;
- support asynchronous completion without blocking the whole host; and
- make resource ownership and cleanup explicit.

The loading and language contract is defined in
[Dynamic linking and host modules](dynamic-linking.md).

## Migration

1. Measure representative current file, cached file, block, console, and network
   paths, including open-time redirects and steady-state requests.
2. Define static host component interfaces and link current C implementations
   into the appropriate host. Local calls replace Channels only after ownership
   and cleanup move with the component.
3. Complete `hunde-treiber-host` migration while retaining existing external
   device protocols for `hunde-io-host`.
4. Move namespace, filesystem, socket, stream, pipe, terminal, and information
   components into `hunde-io-host`, preserving the common application I/O API.
5. Verify AArch64 and RISC-V behavior and compare the recorded measurements.
6. Introduce dynamic ELF loading without changing the component ABI or host
   responsibility boundary.

Do not preserve a permanent Channel path between components after both sides
have moved into one host. Do not remove an existing process boundary until the
replacement host owns its resources, blocked-operation progress, cleanup, and
tests.

## Open design questions

- Define how boot-critical modules are supplied without depending on the
  filesystem or device stack that those modules are needed to create.
- Decide which existing service endpoint names remain as compatibility aliases
  during migration and when each alias is removed.
- Define recovery of sessions, buffers, device state, and outstanding operations
  after either host exits. Current service restart recovery remains deferred.
- Decide whether safe runtime unloading is required. Initial dynamically loaded
  drivers remain resident because unload requires proven callback, fibre, IRQ,
  and DMA quiescence.
