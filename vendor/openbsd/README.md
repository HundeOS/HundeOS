# OpenBSD Sources

The files under `src/` are vendored without local source changes from the
OpenBSD source tree at commit `32c50d61a4335230e4b69fd90e697682cbb6c2ba`.

Current files:

- `src/bin/cat/cat.c`, OpenBSD revision 1.34.
- `src/bin/cp/`, the OpenBSD `cp` sources at the pinned commit.
- `src/bin/ln/ln.c`, OpenBSD revision 1.25.
- `src/bin/kill/kill.c`, OpenBSD revision 1.15.
- `src/bin/ls/`, the OpenBSD `ls` sources at the pinned commit.
- `src/bin/mkdir/mkdir.c`, OpenBSD revision 1.31.
- `src/bin/mv/`, the OpenBSD `mv` sources and its private `cp`/`rm`
  implementations at the pinned commit.
- `src/bin/rm/rm.c`, OpenBSD revision 1.45.
- `src/usr.bin/env/env.c`, OpenBSD revision 1.19.
- `src/usr.bin/tail/`, the OpenBSD `tail` sources at the pinned commit.
- `src/usr.bin/grep/`, the OpenBSD `grep` sources at the pinned commit.
- `src/lib/libc/regex/` and `src/include/regex.h`, the OpenBSD regular
  expression implementation used by `grep`.
- `src/lib/libc/gen/signame.c`, the OpenBSD signal-name table used by `kill`.
- `src/include/fts.h`, used by `ls` and `mv` through the HundeOS libc
  compatibility implementation.

The copyright and redistribution terms are retained in each source file.
HundeOS-specific build and compatibility code lives outside this directory.
