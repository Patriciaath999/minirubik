    .text
    li   t0, 0x10000000
    li   t1, SIZE
    add  t1, t0, t1
loop:
    sw   zero, 0(t0)
    addi t0, t0, 4
    bltu t0, t1, loop
    li   a7, 10
    ecall