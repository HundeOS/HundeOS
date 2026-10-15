#!/usr/bin/env sh

set -eu

sudo -v || exit 1

diskutil unmountDisk /dev/disk4
make clean milkv-duo-s-sd-image-riscv
sudo dd \
  if=build/riscv64/milkv_duo_s/hundeos-milkv-duo-s.img \
  of=/dev/rdisk4 \
  bs=4m
diskutil unmountDisk /dev/disk4