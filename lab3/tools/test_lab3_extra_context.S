#include <riscv.h>

    .text
    .align 2
    .globl test_lab3_extra_context
test_lab3_extra_context:
    addi sp, sp, -48
    STORE ra, 0*REGBYTES(sp)
    STORE s0, 1*REGBYTES(sp)
    STORE s1, 2*REGBYTES(sp)
    STORE gp, 3*REGBYTES(sp)
    STORE tp, 4*REGBYTES(sp)
    mv s0, sp
    csrr s1, sstatus
    li a0, 0x55a

    .macro CHECK_CONTEXT
    bne sp, s0, .Lextra_fail
    li t0, 0x55a
    bne a0, t0, .Lextra_fail
    LOAD t0, 0*REGBYTES(sp)
    bne ra, t0, .Lextra_fail
    LOAD t0, 3*REGBYTES(sp)
    bne gp, t0, .Lextra_fail
    LOAD t0, 4*REGBYTES(sp)
    bne tp, t0, .Lextra_fail
    csrr t0, sstatus
    xor t0, t0, s1
    andi t0, t0, SSTATUS_SIE
    bnez t0, .Lextra_fail
    .endm

    # Fixed encodings prevent assembler relaxation from hiding a length bug.
    .2byte 0x9002       # c.ebreak, 2 bytes
    CHECK_CONTEXT
    .4byte 0x00100073   # ebreak, 4 bytes
    CHECK_CONTEXT
    .2byte 0x0000       # illegal compressed instruction, 2 bytes
    CHECK_CONTEXT
    .4byte 0x0000007b   # illegal custom opcode, 4 bytes
    CHECK_CONTEXT
    li a0, 0
    j .Lextra_return

.Lextra_fail:
    mv sp, s0
    li a0, 1
.Lextra_return:
    LOAD ra, 0*REGBYTES(sp)
    LOAD s0, 1*REGBYTES(sp)
    LOAD s1, 2*REGBYTES(sp)
    addi sp, sp, 48
    ret
