# OpenBSD kill

The `kill` command is built directly from the unchanged source at
`vendor/openbsd/src/bin/kill/kill.c`. HundeOS compatibility belongs in the
shared userspace interfaces, not in a local command implementation.

Positive process IDs and signal-zero existence probes are supported. PID `0`
and negative process-group targets currently return `EOPNOTSUPP`; completing
those semantics is tracked in `docs/TODO.md`.
