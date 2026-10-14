#!/bin/sh
# Regression check for espressif/qemu #175 (cross-thread TLB flush segfault on esp32s3).
# Every tracked qemu-system-xtensa invocation must carry "-accel tcg,thread=single".
# Run:  sh firmware/host_test/qemu_thread_single_check.sh
# Exit 0 = every call site has the flag; 1 = a call site lacks it (or none found).
cd "$(git rev-parse --show-toplevel)" || exit 2
self=firmware/host_test/qemu_thread_single_check.sh
sites=$(git grep -n 'qemu-system-xtensa' -- . ":!$self")
[ -n "$sites" ] || { echo "FAIL: no qemu-system-xtensa call site found"; exit 1; }
echo "$sites"
bad=$(echo "$sites" | grep -v -e '-accel tcg,thread=single')
if [ -n "$bad" ]; then
  echo "FAIL: call site(s) missing -accel tcg,thread=single:"
  echo "$bad"
  exit 1
fi
echo "PASS: all call sites carry -accel tcg,thread=single"
