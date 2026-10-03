#!/bin/sh

set -eu

vendor_dir=$1
target=$2
target_cflags=$3

exec "$vendor_dir/configure" \
    --target="$target" \
    --prefix=/usr \
    --disable-multilib \
    --disable-nls \
    --disable-newlib-supplied-syscalls \
    --disable-newlib-multithread \
    --disable-newlib-io-float \
    --disable-newlib-io-long-double \
    --disable-newlib-wide-orient \
    --disable-newlib-fvwrite-in-streamio \
    --disable-newlib-fseek-optimization \
    --enable-newlib-nano-malloc \
    --enable-lite-exit \
    CFLAGS_FOR_TARGET="$target_cflags"
