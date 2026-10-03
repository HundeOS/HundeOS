# Newlib Sources

The files under `src/` are the unmodified Newlib `4.6.0.20260123` release.
HundeOS builds `libm.a` and its `math.h` dependency closure for each target
architecture. The existing HundeOS libc remains the operating-system adaptation
layer; Newlib's libc and unrelated headers are not installed in the target
sysroot.

Upstream source:

- `https://sourceware.org/pub/newlib/newlib-4.6.0.20260123.tar.gz`

`SHA256SUMS` records the downloaded archive checksum. Upstream copyright and
license notices are retained in `src/`.
