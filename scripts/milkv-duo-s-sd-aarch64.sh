#!/usr/bin/env sh

set -eu

sudo -v || exit 1

diskutil unmountDisk /dev/disk4
make clean milkv-duo-s-sd-image-aarch64
sudo dd \
  if=build/aarch64/milkv_duo_s/hundeos-milkv-duo-s.img \
  of=/dev/rdisk4 \
  bs=4m
diskutil unmountDisk /dev/disk4
