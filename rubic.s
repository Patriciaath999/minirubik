
    .option norvc
    .equ P, 5040
    .equ O, 729
    .equ MAXD, 11
    .text
    .globl main
main:
    la      sp, stack_top
    la      t0, input_state
    la      t1, cubie_p
    la      t2, cubie_o
    li      t3, 0
parse_loop:
    lbu     t4, 0(t0)
    addi    t4, t4, -49
    sb      t4, 0(t1)
    lbu     t4, 7(t0)
    addi    t4, t4, -49
    sb      t4, 0(t2)
    addi    t0, t0, 1
    addi    t1, t1, 1
    addi    t2, t2, 1
    addi    t3, t3, 1
    li      t4, 7
    blt     t3, t4, parse_loop

    /* Lehmer permutation rank: p = p*(7-i) + smaller-to-right. */
    la      t0, cubie_p
    li      s0, 0
    li      t1, 0
rank_p_outer:
    li      t2, 0
    addi    t3, t1, 1
rank_p_inner:
    li      t4, 7
    bge     t3, t4, rank_p_fold
    add     t5, t0, t3
    lbu     t5, 0(t5)
    add     t6, t0, t1
    lbu     t6, 0(t6)
    bgeu    t5, t6, rank_p_next
    addi    t2, t2, 1
rank_p_next:
    addi    t3, t3, 1
    j       rank_p_inner
rank_p_fold:
    li      t3, 0
    li      t4, 1
    beq     t1, t3, rank_p_mul7
    li      t3, 1
    beq     t1, t3, rank_p_mul6
    li      t3, 2
    beq     t1, t3, rank_p_mul5
    li      t3, 3
    beq     t1, t3, rank_p_mul4
    li      t3, 4
    beq     t1, t3, rank_p_mul3
    li      t3, 5
    beq     t1, t3, rank_p_mul2
    j       rank_p_add
rank_p_mul7:
    slli    t4, s0, 3
    sub     s0, t4, s0
    j       rank_p_add
rank_p_mul6:
    slli    t4, s0, 2
    slli    t5, s0, 1
    add     s0, t4, t5
    j       rank_p_add
rank_p_mul5:
    slli    t4, s0, 2
    add     s0, t4, s0
    j       rank_p_add
rank_p_mul4:
    slli    s0, s0, 2
    j       rank_p_add
rank_p_mul3:
    slli    t4, s0, 1
    add     s0, t4, s0
    j       rank_p_add
rank_p_mul2:
    slli    s0, s0, 1
rank_p_add:
    add     s0, s0, t2
    addi    t1, t1, 1
    li      t3, 7
    blt     t1, t3, rank_p_outer

    /* Base-3 orientation rank over the first six corner orientations. */
    la      t0, cubie_o
    li      s1, 0
    li      t1, 0
rank_o_loop:
    slli    t2, s1, 1
    add     s1, t2, s1
    add     t3, t0, t1
    lbu     t3, 0(t3)
    add     s1, s1, t3
    addi    t1, t1, 1
    li      t4, 6
    blt     t1, t4, rank_o_loop

    /* Non-recursive IDA*: next candidate move at each depth. */
    la      t0, perm_dist
    add     t1, t0, s0
    lbu     s2, 0(t1)
    la      t0, orient_dist
    add     t1, t0, s1
    lbu     t2, 0(t1)
    bgeu    s2, t2, init_bound
    mv      s2, t2
init_bound:
    sw      s0, start_p, t0
    sw      s1, start_o, t0
bound_loop:
    li      t0, MAXD
    bgt     s2, t0, fail
    li      s3, 0
    la      t0, next_move
    sw      zero, 0(t0)
    la      t0, depth_p
    sw      s0, 0(t0)
    la      t0, depth_o
    sw      s1, 0(t0)
search_node:
    /* if depth exceeds 0, current node's parent path move is path[d-1]. */
    la      t0, next_move
    slli    t1, s3, 2
    add     t0, t0, t1
    lw      t2, 0(t0)
    li      t3, 9
    bge     t2, t3, backtrack
    addi    t4, t2, 1
    sw      t4, 0(t0)
    /* Forbid two consecutive moves on same face. */
    beqz    s3, choose_move
    addi    t5, s3, -1
    la      t6, path
    slli    t5, t5, 2
    add     t5, t5, t6
    lw      t5, 0(t5)
    li      t6, 3
    divu    t5, t5, t6
    divu    t6, t2, t6
    beq     t5, t6, search_node
choose_move:
    /* candidate move m=face*3+turn. Apply that many quarter transitions. */
    la      t0, depth_p
    slli    t1, s3, 2
    add     t0, t0, t1
    lw      t5, 0(t0)
    la      t0, depth_o
    add     t0, t0, t1
    lw      t6, 0(t0)
    mv      a0, t2
    li      a1, 3
    divu    a2, a0, a1
    remu    a3, a0, a1
    addi    a3, a3, 1
apply_turns:
    la      t0, perm_move
    li      a1, P
    mul     a4, a2, a1
    add     a4, a4, t5
    slli    a4, a4, 1
    add     a4, a4, t0
    lhu     t5, 0(a4)
    la      t0, orient_move
    li      a1, O
    mul     a4, a2, a1
    add     a4, a4, t6
    slli    a4, a4, 1
    add     a4, a4, t0
    lhu     t6, 0(a4)
    addi    a3, a3, -1
    bnez    a3, apply_turns
    addi    t0, s3, 1
    la      t1, depth_p
    slli    t2, t0, 2
    add     t1, t1, t2
    sw      t5, 0(t1)
    la      t1, depth_o
    add     t1, t1, t2
    sw      t6, 0(t1)
    la      t1, path
    slli    t2, s3, 2
    add     t1, t1, t2
    lw      t2, 0(sp)
    sw      t2, 0(t1)
    la      t1, perm_dist
    add     t1, t1, t5
    lbu     t2, 0(t1)
    la      t1, orient_dist
    add     t1, t1, t6
    lbu     t3, 0(t1)
    bgeu    t2, t3, h_ready
    mv      t2, t3
h_ready:
    beqz    t2, found
    add     t3, t0, t2
    bgt     t3, s2, search_node
    mv      s3, t0
    la      t1, next_move
    slli    t2, s3, 2
    add     t1, t1, t2
    sw      zero, 0(t1)
    j       search_node
backtrack:
    beqz    s3, next_bound
    addi    s3, s3, -1
    j       search_node
next_bound:
    addi    s2, s2, 1
    j       bound_loop
found:
    /* Print each move as its compact token (R/R2/R', B..., D...). */
    li      s3, 0
print_loop:
    la      t0, path
    slli    t1, s3, 2
    add     t0, t0, t1
    lw      t2, 0(t0)
    li      t3, 3
    divu    t4, t2, t3
    remu    t5, t2, t3
    la      t0, face_chars
    add     t0, t0, t4
    lbu     a0, 0(t0)
    li      a7, 11
    ecall
    li      t0, 1
    beq     t5, t0, print_two
    li      t0, 2
    beq     t5, t0, print_prime
print_space:
    addi    s3, s3, 1
    bge     s3, s2, print_done
    li      a0, 32
    li      a7, 11
    ecall
    j       print_loop
print_two:
    li      a0, 50
    li      a7, 11
    ecall
    j       print_space
print_prime:
    li      a0, 39
    li      a7, 11
    ecall
    j       print_space
print_done:
    li      a0, 10
    li      a7, 11
    ecall
exit:
    li      a7, 10
    ecall
fail:
    la      a0, fail_text
    li      a7, 4
    ecall
    j       exit

    .data
    .align 2
input_state: .ascii "12345671111111\0"
fail_text:   .asciz "search failed\n"
face_chars:  .ascii "RBD"
cubie_p:     .space 7
cubie_o:     .space 7
start_p:     .word 0
start_o:     .word 0
depth_p:     .space 48
depth_o:     .space 48
next_move:   .space 48
path:        .space 48
    .space 8192
stack_top:
