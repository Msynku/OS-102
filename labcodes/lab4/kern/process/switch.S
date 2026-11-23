//LAB4 function3 2413140
/* a0=&旧线程context，a1=&新线程context；按 proc.h 中字段顺序保存/恢复。 */
/* ra、sp 和 s0~s11 共 14 个寄存器；其余调用者保存寄存器由调用约定处理。 */
    .text
    .globl switch_to
switch_to:
    /* sd 将寄存器存到内存；RV64 的每个寄存器占 8 字节，偏移对应 context 中的字段。 */
    sd ra, 0(a0)    /* 保存旧线程的返回地址到 prev->context.ra */
    sd sp, 8(a0)    /* 保存旧线程的栈指针到 prev->context.sp */
    sd s0, 16(a0)   /* 后面的 s0~s11 依次保存到旧 context */
    sd s1, 24(a0)
    sd s2, 32(a0)
    sd s3, 40(a0)
    sd s4, 48(a0)
    sd s5, 56(a0)
    sd s6, 64(a0)
    sd s7, 72(a0)
    sd s8, 80(a0)
    sd s9, 88(a0)
    sd s10, 96(a0)
    sd s11, 104(a0)

    /* ld 从新 context 取回寄存器；恢复 sp 后使用的是新线程的内核栈。 */
    ld ra, 0(a1)    /* 恢复新线程的返回地址 */
    ld sp, 8(a1)    /* 恢复新线程的栈指针 */
    ld s0, 16(a1)   /* 后面的 s0~s11 依次从新 context 恢复 */
    ld s1, 24(a1)
    ld s2, 32(a1)
    ld s3, 40(a1)
    ld s4, 48(a1)
    ld s5, 56(a1)
    ld s6, 64(a1)
    ld s7, 72(a1)
    ld s8, 80(a1)
    ld s9, 88(a1)
    ld s10, 96(a1)
    ld s11, 104(a1)

    /* ret 使用新线程的 ra；首次运行时 ra 指向 forkret。 */
    ret
