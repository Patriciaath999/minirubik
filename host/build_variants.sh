#!/bin/sh
# Build host-test variants of rubic.s. RENDER builds get a stand-in
# LED_MATRIX_0_BASE (Ripes defines the real one); never ship these.
set -e
for r in 0 1; do for t in 0 1; do
  f=v_r${r}_t${t}
  { [ $r = 1 ] && echo "    .equ LED_MATRIX_0_BASE, 0x10080000"
    sed "s/^    .equ RENDER, 0/    .equ RENDER, $r/; s/^    .equ SELFTEST, 0/    .equ SELFTEST, $t/; s/^    .equ DELAY, 20000/    .equ DELAY, 0/" rubic.s; } > $f.s
  clang --target=riscv32 -march=rv32i -mabi=ilp32 -c $f.s -o $f.o
  ld.lld -m elf32lriscv -e main -Ttext=0x0 -Tdata=0x10000000 $f.o -o $f.elf
done; done
