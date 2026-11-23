#include <defs.h>
#include <stdio.h>
#include <string.h>
#include <console.h>
#include <kdebug.h>
#include <picirq.h>
#include <trap.h>
#include <clock.h>
#include <intr.h>
#include <pmm.h>
#include <vmm.h>
#include <proc.h>
#include <kmonitor.h>
#include <dtb.h>

int kern_init(void) __attribute__((noreturn));
void grade_backtrace(void);

int kern_init(void)
{
    extern char edata[], end[];    // 链接器给出的未初始化全局数据的起止位置
    memset(edata, 0, end - edata); // 清零未初始化的全局数据
    dtb_init();                    // 读取设备树中的硬件信息
    cons_init();                   // 准备控制台输出

    const char *message = "(THU.CST) os is loading ...";
    cprintf("%s\n\n", message);

    print_kerninfo();

    // grade_backtrace();

    pmm_init(); // 准备物理页分配器，并检查启动页表和映射

    pic_init(); // 外部中断控制器入口；当前实现为空
    idt_init(); // 设置 RISC-V 的中断/异常入口 stvec

    vmm_init();  // 检查虚拟内存区域管理
    proc_init(); // 包装 idleproc，创建可运行的 initproc

    clock_init();  // 设置下一次时钟事件
    intr_enable(); // 打开 S 态全局中断

    cpu_idle(); // idleproc 根据 need_resched 请求调度
}

static void
lab1_print_cur_status(void)
{
    static int round = 0;
    round++;
}
