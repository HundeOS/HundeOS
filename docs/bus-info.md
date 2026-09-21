# Bus Information

`hundebus` remains a running userspace service. It scans PCI configuration
space at startup, owns the resulting inventory, and publishes the BString
service identifier `hundebus`. It no longer registers `/dev/businfo` with the
kernel or serves buffers through `io_server_*` syscalls.

## Channel Queries

The public interface is `user/lib/bus/info.h`. Clients open the existing
`Channel` to `BUS_INFO_SERVICE`, call `BusInfo__read(channel, index)` repeatedly,
and close the Channel when finished. One session serves all indexed queries.

The protocol reuses the existing `IORequest`/`IOReply` envelope and `IOService`
dispatcher. A request is `IO_REQUEST_READ`, handle and flags zero, offset equal
to the device index, and count equal to `sizeof(BusInfoPage)`.
`BusInfo__request` constructs it. There is no resource-open step or per-client
cursor: the read-only inventory belongs to the service, not the session.

A successful reply has `IO_OK`, value `sizeof(BusInfoPage)`, and the page in
its data payload. Each page contains the total device count and either one
`BusPciDevice` or no record at index equal to the total. Indices beyond the
total are errors, including values too large for a collection index. Each
record supplies bus/device/function, vendor/device IDs, class, subclass,
programming interface, and header type. Clients validate reply sizes and page
bounds before using records. Mutation and other I/O operations are rejected.

Channel establishment authenticates the warden and supplies client identity;
payload PID fields are not used as authority. Bus metadata queries are
read-only and are not restricted to `sysinfofs`. No addresses, MMIO mappings,
or device-access rights are granted by a reply.

`IOService` retains completed replies under backpressure and handles client
disconnects without blocking other sessions. There are no bus-specific shared
queues or buffers. The current inventory is immutable and limited to 256 PCI
functions. Startup fails instead of advertising a silently truncated scan.
This is an in-tree native-ABI protocol, not a cross-version serialization ABI.

## Filesystem and Bootstrap Boundaries

The intended `sysinfofs` consumer will query this service and format records
for shell users. That filesystem and its mount are not implemented by this
change. `/dev/businfo` has no compatibility file or kernel fallback.

`hundebus` still obtains bootstrap PCI ECAM configuration from `/boot/sysinfo`.
Replacing that input with a typed system-information syscall is separate work.
Bus enumeration stays in userspace. The service does not currently launch
drivers or monitor hotplug; see [Features TODO](../FEATURES-TODO.md).
