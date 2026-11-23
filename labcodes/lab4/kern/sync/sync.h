#ifndef __KERN_SYNC_SYNC_H__
#define __KERN_SYNC_SYNC_H__

#include <defs.h>
#include <intr.h>
#include <riscv.h>

static inline bool __intr_save(void) {
    // sstatus 的 SIE 位表示 S 态全局中断是否开启。
    // 返回值保存原状态：原来开启则关中断并返回 1，原来关闭则返回 0。
    if (read_csr(sstatus) & SSTATUS_SIE) {
        intr_disable();
        return 1;
    }
    return 0;
}

static inline void __intr_restore(bool flag) {
    // flag 是 __intr_save 的返回值；原本开启才重新打开，嵌套时不会提前开中断。
    if (flag) {
        intr_enable();
    }
}

// x 是调用者提供的 bool 变量，宏会把原中断状态写进去。
#define local_intr_save(x) \
    do {                   \
        x = __intr_save(); \
    } while (0)
#define local_intr_restore(x) __intr_restore(x);

#endif /* !__KERN_SYNC_SYNC_H__ */
