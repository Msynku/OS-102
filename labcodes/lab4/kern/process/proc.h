#ifndef __KERN_PROCESS_PROC_H__
#define __KERN_PROCESS_PROC_H__

#include <defs.h>
#include <list.h>
#include <trap.h>
#include <memlayout.h>

// RUNNABLE 同时表示就绪和正在运行；睡眠、退出的完整流程留待后续实验。
enum proc_state
{
    PROC_UNINIT = 0, // PCB 尚未初始化完成
    PROC_SLEEPING,   // 等待事件，暂时不可运行
    PROC_RUNNABLE,   // 可运行，可能正在占用 CPU
    PROC_ZOMBIE,     // 已退出，等待父进程回收
};

// context 是线程切换时保存的精简现场；新线程首次运行前由内核填好。
// 它不同于 trapframe：前者供 switch_to 切换线程，后者供中断或异常返回。
struct context
{
    uintptr_t ra; // 返回地址：切换回来后从哪里继续；新线程首次指向 forkret
    uintptr_t sp; // 栈指针：切换回来后使用哪个内核栈位置
    uintptr_t s0; // s0~s11 是函数调用约定中需要被调用者保留的寄存器
    uintptr_t s1;
    uintptr_t s2;
    uintptr_t s3;
    uintptr_t s4;
    uintptr_t s5;
    uintptr_t s6;
    uintptr_t s7;
    uintptr_t s8;
    uintptr_t s9;
    uintptr_t s10;
    uintptr_t s11;
};

#define PROC_NAME_LEN 15
#define MAX_PROCESS 4096
#define MAX_PID (MAX_PROCESS * 2)

extern list_entry_t proc_list;

struct proc_struct
{
    enum proc_state state;        // 进程状态，调度器只选择 RUNNABLE
    int pid;                      // 进程号；-1 表示尚未分配
    int runs;                     // 被调度器选中的次数
    uintptr_t kstack;             // 内核栈低地址端；栈一般从高地址向低地址增长
    volatile bool need_resched;   // 是否请求重新调度
    struct proc_struct *parent;   // 创建它的父进程
    struct mm_struct *mm;         // 用户地址空间描述；本实验内核线程为 NULL
    struct context context;       // switch_to 保存和恢复的寄存器
    struct trapframe *tf;         // 中断返回现场，首次运行时由内核构造
    uintptr_t pgdir;              // 页表根的物理地址，写入 satp 时使用
    uint32_t flags;               // 进程标志
    char name[PROC_NAME_LEN + 1]; // 名称，末尾预留 '\0'
    list_entry_t list_link;       // 接入 proc_list，供遍历和调度
    list_entry_t hash_link;       // 接入 hash_list，供按 PID 查找
};

// 从 PCB 内嵌的链表节点反推出所属 PCB。
#define le2proc(le, member) \
    to_struct((le), struct proc_struct, member)

extern struct proc_struct *idleproc, *initproc, *current;

void proc_init(void);
void proc_run(struct proc_struct *proc);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);

char *set_proc_name(struct proc_struct *proc, const char *name);
char *get_proc_name(struct proc_struct *proc);
void cpu_idle(void) __attribute__((noreturn));

struct proc_struct *find_proc(int pid);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
int do_exit(int error_code);

#endif /* !__KERN_PROCESS_PROC_H__ */
