#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/* Lab4 用 PCB 管理内核线程：idleproc 承接启动现场，initproc 由 do_fork 创建。
 * 各线程共享内核地址空间，但有独立的内核栈和切换现场。 */

// 普通进程通过 proc_list 参与遍历和调度；idleproc 单独保存，不加入此表。
list_entry_t proc_list;

#define HASH_SHIFT 10
#define HASH_LIST_SIZE (1 << HASH_SHIFT) // 2 的 10 次方，即 1024 个哈希桶
#define pid_hashfn(x) (hash32(x, HASH_SHIFT)) // 把 PID 映射到一个桶的编号

// 按 PID 散列到链表，供 find_proc 快速查找；idleproc 也不加入此表。
static list_entry_t hash_list[HASH_LIST_SIZE];

struct proc_struct *idleproc = NULL; // 0 号空闲线程：没有其他线程可运行时使用
struct proc_struct *initproc = NULL; // 1 号内核线程：第一次运行时执行 init_main
struct proc_struct *current = NULL;  // 当前 CPU 正在执行的线程的 PCB

static int nr_process = 0; // 已创建的进程/线程总数，包括不在 proc_list 中的 idleproc

void kernel_thread_entry(void);
void forkrets(struct trapframe *tf);
void switch_to(struct context *from, struct context *to);

// set_proc_name - set the name of proc
char *
set_proc_name(struct proc_struct *proc, const char *name)
{
    memset(proc->name, 0, sizeof(proc->name));
    return memcpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - get the name of proc
char *
get_proc_name(struct proc_struct *proc)
{
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// 为现存进程分配不重复的 PID；后续进程回收后可循环复用。
static int
get_pid(void)
{
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;                 // 遍历链表时取出的进程 PCB
    list_entry_t *list = &proc_list, *le;     // list 是表头，le 用来逐个访问节点
    // last_pid 是上次分配的编号；next_safe 是后面最近的已占用 PID 边界。
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++last_pid >= MAX_PID) // 编号到上限后回到 1；0 专门留给 idleproc
    {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe) // 到达已占用区间时重新扫描，避免分配重复 PID
    {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list)
        {
            proc = le2proc(le, list_link); // 从 PCB 内的链表节点找到所属 PCB
            if (proc->pid == last_pid)
            {
                if (++last_pid >= next_safe)
                {
                    if (last_pid >= MAX_PID)
                    {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid)
            {
                next_safe = proc->pid; // 记录比候选 PID 大的最近已占用编号
            }
        }
    }
    return last_pid; // 返回不与当前进程冲突的 PID
}


// 新线程首次被调度时，switch_to 的 ret 进入 forkret。
static void
forkret(void)
{
    forkrets(current->tf); // current->tf 是新线程内核栈上准备好的中断帧
}

// 按进程 PID 找到对应哈希桶，把 PCB 的 hash_link 节点挂进去。
static void
hash_proc(struct proc_struct *proc)
{
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// 按 PID 查找 PCB；编号无效或桶中找不到时返回 NULL。
struct proc_struct *
find_proc(int pid)
{
    if (0 < pid && pid < MAX_PID)
    {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list; // list 是桶的表头
        while ((le = list_next(le)) != list)
        {
            struct proc_struct *proc = le2proc(le, hash_link); // 哈希节点转回所属 PCB
            if (proc->pid == pid)
            {
                return proc;
            }
        }
    }
    return NULL;
}


// 为 proc 分配内核栈；成功返回 0，物理页不够时返回 -E_NO_MEM。
static int
setup_kstack(struct proc_struct *proc)
{
    struct Page *page = alloc_pages(KSTACKPAGE); // page 指向两页连续物理页的首页描述符
    if (page != NULL)
    {
        proc->kstack = (uintptr_t)page2kva(page); // 转为内核可访问的栈起始虚拟地址
        return 0;
    }
    return -E_NO_MEM;
}

// 把内核栈虚拟地址转回物理页描述符，再归还这两页。
static void
put_kstack(struct proc_struct *proc)
{
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}


// 线程退出入口：本阶段没有实现释放资源、变为僵尸态及通知父进程。
int do_exit(int error_code)
{
    // Lab4 尚未实现线程退出和资源回收；init_main 返回后会进入这里。
    panic("process exit!!.\n");
}

// initproc 第一次被调度时执行的函数；arg 是创建线程时传入的字符串。
static int
init_main(void *arg)
{
    cprintf("this initproc, pid = %d, name = \"%s\"\n", current->pid, get_proc_name(current));
    cprintf("To U: \"%s\".\n", (const char *)arg);
    cprintf("To U: \"en.., Bye, Bye. :)\"\n");
    return 0;
}

//LAB4 function2 2413905
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);

// 分配并初始化 PCB；尚未分配实际的内核栈和 PID。
static struct proc_struct *
alloc_proc(void)
{
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct)); // 为一个 PCB 申请内存
    if (proc != NULL)
    {
        proc->state = PROC_UNINIT;  // 刚创建，还不能被调度
        proc->pid = -1;             // 尚未分配正式进程号
        proc->runs = 0;             // 尚未被调度器选中
        proc->kstack = 0;           // 尚未分配内核栈
        proc->need_resched = 0;     // 暂无让出 CPU 的请求
        proc->parent = NULL;        // 父进程稍后在 do_fork 中设置
        proc->mm = NULL;            // 暂无独立的用户地址空间描述
        memset(&(proc->context), 0, sizeof(struct context)); // 清空切换时保存的寄存器
        proc->tf = NULL;            // 尚未准备中断帧
        proc->pgdir = boot_pgdir_pa; // 默认使用启动页表根的物理地址
        proc->flags = 0;            // 暂无额外进程标志
        memset(proc->name, 0, PROC_NAME_LEN + 1); // 清空名称，包含结尾的 '\0'
    }
    return proc; // 分配失败时为 NULL，否则指向初始化后的 PCB
}

// 初始化进程管理结构，并准备 idleproc 和 initproc。
void proc_init(void)
{
    int i; // 用于遍历 PID 哈希表中的每个桶

    list_init(&proc_list); // 初始化进程链表的表头；刚开始链表为空
    for (i = 0; i < HASH_LIST_SIZE; i++)
    {
        list_init(hash_list + i); // 每个哈希桶也有自己的空链表表头
    }

    if ((idleproc = alloc_proc()) == NULL)
    {
        panic("cannot alloc idleproc.\n");
    }

    // idleproc 包装当前启动现场，复用 bootstack，而不是从父进程复制。
    idleproc->pid = 0;                        // 0 号线程专门作为空闲线程
    idleproc->state = PROC_RUNNABLE;          // 已经在 CPU 上运行，可以参与调度
    idleproc->kstack = (uintptr_t)bootstack;  // 沿用启动阶段已经准备好的栈
    idleproc->need_resched = 1;               // 进入 cpu_idle 后请求调度其他线程
    set_proc_name(idleproc, "idle");          // 设置线程名称，便于查看和调试
    nr_process++;                             // 已创建数量加一，包含 idleproc

    current = idleproc; // 先设置 current，后续新线程的 parent 才是 idleproc

    // 创建 PID 1 的 initproc；此时只是登记为可运行，尚未执行 init_main。
    int pid = kernel_thread(init_main, "Hello world!!", 0); // pid 是新线程的编号
    if (pid <= 0)
    {
        panic("create init_main failed.\n");
    }

    initproc = find_proc(pid);          // 按 PID 从哈希表找到新线程的 PCB
    set_proc_name(initproc, "init");   // 将 1 号线程命名为 init

    // 下面的断言检查两个线程的状态、父子关系和首次运行现场是否准备好。
    assert(idleproc->pid == 0);
    assert(idleproc->state == PROC_RUNNABLE);
    assert(idleproc->kstack == (uintptr_t)bootstack);
    assert(idleproc->need_resched == 1);
    assert(idleproc->parent == NULL && idleproc->mm == NULL);
    assert(idleproc->tf == NULL && idleproc->runs == 0);
    assert(idleproc->flags == 0 && idleproc->context.ra == 0);

    assert(initproc->pid == 1);
    assert(initproc->state == PROC_RUNNABLE);
    assert(initproc->parent == idleproc && initproc->mm == NULL);
    assert(initproc->context.ra != 0 && initproc->context.sp != 0);
    assert(initproc->tf != NULL && initproc->context.sp == (uintptr_t)initproc->tf);
}

// 创建子线程：clone_flags 指定共享方式，stack 是初始栈指针，tf 是中断帧模板。
// 成功向创建者返回新 PID；失败返回负错误码，并释放已经申请的资源。
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{
    int ret = -E_NO_FREE_PROC; // ret 保存最终返回值；先假定进程数量已满
    struct proc_struct *proc;  // 指向即将创建的子线程 PCB
    if (nr_process >= MAX_PROCESS) // 超过进程数量上限时不再创建
    {
        goto fork_out;
    }
    ret = -E_NO_MEM; // 后续若分配 PCB、栈等资源失败，返回内存不足
    if ((proc = alloc_proc()) == NULL)
    {
        goto fork_out;
    }
    // PCB → 内核栈 → 地址空间 → 首次运行现场；失败时按相反顺序清理。
    proc->parent = current;      // current 是创建它的父线程
    if (setup_kstack(proc) != 0) // 给子线程分配独立的内核栈
    {
        goto bad_fork_cleanup_proc;
    }
    if (copy_mm(clone_flags, proc) != 0) // 准备地址空间；本实验内核线程共享内核页表
    {
        goto bad_fork_cleanup_kstack;
    }
    copy_thread(proc, stack, tf); // 把首次运行的中断帧和切换现场放到子线程中

    bool intr_flag; // 记录进入临界区前的中断状态
    local_intr_save(intr_flag); // 关中断，避免 PID 分配和链表登记过程中被打断
    {
        proc->pid = get_pid();                    // 分配不与现有进程冲突的编号
        hash_proc(proc);                          // 加入 PID 哈希表，方便按编号查找
        list_add(&proc_list, &(proc->list_link)); // 加入进程链表，供调度器遍历
        nr_process++;                             // 已创建进程数量加一
    }
    local_intr_restore(intr_flag); // 按进入前的状态恢复中断

    wakeup_proc(proc); // 把子线程置为 RUNNABLE；它还要等调度器选中才会运行

    ret = proc->pid; // 创建成功，向父线程返回子线程编号

fork_out:
    return ret;

bad_fork_cleanup_kstack:
    put_kstack(proc); // 地址空间准备失败：先归还已经分配的内核栈
bad_fork_cleanup_proc:
    kfree(proc);      // 栈分配失败或栈已归还：释放子线程 PCB
    goto fork_out;
}

// fn 是新线程要执行的函数，arg 是传给该函数的参数，clone_flags 指定资源共享方式。
// 成功时返回新线程的 PID；这里先准备运行现场，线程不会立即开始执行。
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)
{
    // 临时中断帧稍后由 copy_thread 复制到新线程自己的内核栈。
    struct trapframe tf;
    memset(&tf, 0, sizeof(struct trapframe)); // 没有指定的寄存器先清零
    tf.gpr.s0 = (uintptr_t)fn;  // 线程入口函数
    tf.gpr.s1 = (uintptr_t)arg; // 入口函数参数
    // SPP 使 sret 返回 S 态；SPIE 使返回后可开中断，恢复现场期间先关 SIE。
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;
    // sret 先到汇编入口，再由入口用 s0/s1 调用 fn(arg)。
    tf.epc = (uintptr_t)kernel_thread_entry;
    return do_fork(clone_flags | CLONE_VM, 0, &tf); // 共享地址空间；栈由 copy_thread 准备
}

// clone_flags 指定共享方式，proc 是新线程的 PCB。
// 本阶段只有内核线程，没有用户地址空间需要复制，继续使用启动页表。
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc)
{
    assert(current->mm == NULL);
    return 0;
}

// proc 是子线程，esp 是指定的初始栈指针，tf 是调用者准备的中断帧模板。
// 在子线程的内核栈上准备 trapframe 和首次被调度时的 context。
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf)
{
    // kstack 是栈底，kstack + KSTACKSIZE 是栈顶；减 1 留出一个 trapframe 的空间。
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE) - 1;
    *(proc->tf) = *tf; // 把临时模板复制到子线程自己的栈上

    // a0 为子进程返回值；内核线程入口随后会用 s1 覆盖 a0 作为函数参数。
    proc->tf->gpr.a0 = 0;
    proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)(proc->tf) : esp; // 未指定栈时使用帧所在位置

    // 首次 switch_to 的 ret 进入 forkret，再按 trapframe 恢复到线程入口。
    proc->context.ra = (uintptr_t)forkret;       // 第一次切换到它时，从 forkret 开始
    proc->context.sp = (uintptr_t)(proc->tf);   // 第一次切换后使用子线程自己的栈
}

//LAB4 function3 2413140
// idleproc 的执行主体：反复检查是否需要调度，有请求时让 schedule 选择线程。
void cpu_idle(void)
{
    while (1)
    {
        if (current->need_resched) // 非 0 表示当前线程需要让出 CPU
        {
            schedule();
        }
    }
}

// proc：准备交给 CPU 执行的目标线程的 PCB（进程控制块）。
void proc_run(struct proc_struct *proc)
{
    if (proc != current) // current 指向当前线程；目标就是当前线程时不必切换
    {
        bool intr_flag; // 保存切换前中断是否开启，后面用于恢复原状态
        struct proc_struct *prev = current, *next = proc; // prev 是旧线程，next 是目标线程

        // 保存原来的中断状态并关中断，避免切换到一半被中断打断。
        local_intr_save(intr_flag);
        {
            current = proc; // 全局 current 改为新线程，让内核知道接下来运行的是谁

            // satp 是 CPU 的页表配置寄存器：高位的 8 表示使用 Sv39 三级页表。
            // proc->pgdir 是页表根的物理地址，右移 12 位得到 satp 需要的物理页号。
            write_csr(satp, 0x8000000000000000 | (proc->pgdir >> RISCV_PGSHIFT));

            // TLB 缓存虚拟地址到物理地址的翻译；刷新后不再沿用旧页表的缓存。
            flush_tlb();

            // 保存 prev 的寄存器和栈指针，再恢复 next 的现场，CPU 随之转去执行 next。
            switch_to(&(prev->context), &(next->context));
        }

        // prev 以后被重新选中时，才从 switch_to 后继续，恢复它原来的中断状态。
        local_intr_restore(intr_flag);
    }
}
