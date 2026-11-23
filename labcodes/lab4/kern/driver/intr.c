#include <intr.h>
#include <riscv.h>

/* 设置 sstatus.SIE，打开当前 hart 的 S 态全局中断。 */
void intr_enable(void) { set_csr(sstatus, SSTATUS_SIE); }

/* 清除 sstatus.SIE，暂时关闭当前 hart 的 S 态全局中断。 */
void intr_disable(void) { clear_csr(sstatus, SSTATUS_SIE); }
