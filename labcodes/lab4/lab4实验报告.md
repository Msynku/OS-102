# lab4 进程管理 实验报告

> 实验日期：2026-11-18

### 小组成员及分工

| 姓名 | 学号 | 负责模块 |
| --- | --- | --- |
| 唐源 | 2411938 | 页表查找与页面映射：`get_pte`、`page_insert`、`page_remove` 及物理页引用计数分析 |
| 马诗渊 | 2413905 | 内核线程创建：`alloc_proc`、`do_fork`、`kernel_thread` 及进程控制块、首次运行现场分析 |
| 颜国庄 | 2413140 | 线程调度与切换：`schedule`、`proc_run`、`switch_to`、中断状态保存与恢复分析 |

实验运行、测试结果检查与报告整理由小组共同负责。

## 一、整体逻辑线理解

lab4 的核心目标，是让内核从"只具备物理内存管理和基础页表能力"进一步走到"能够创建、登记、调度内核线程并完成上下文切换"。整章围绕 **虚拟内存页操作 → 进程创建与初始化 → 进程调度** 三条主线展开，对应的执行链可以概括为：

```
kern_init
  └─ pmm_init        # 物理内存管理 + 建立/校验多级页表（模块一）
  └─ pic_init / idt_init   # 中断控制器 + 中断入口（为后续时钟中断铺垫）
  └─ vmm_init        # 虚拟内存描述结构初始化
  └─ proc_init       # 创建 idleproc 与 initproc（模块二）
  └─ clock_init / intr_enable
  └─ cpu_idle        # 空闲循环，检测到 need_resched 就调度
        └─ schedule  # 从 proc_list 选出下一个 RUNNABLE 线程（模块三）
             └─ proc_run    # 关中断、切换 current、切换页表
                  └─ switch_to  # 保存/恢复寄存器上下文，真正切换执行流
```

前半段（`pmm_init` 里的 `page_insert`/`page_remove`）负责把"线性地址 → 物理页"的映射维护做对，让页表、引用计数、TLB 三者保持一致，这是后面一切地址空间逻辑的基础；后半段（`proc_init` → `cpu_idle` → `schedule` → `proc_run` → `switch_to`）负责把第一个真正的内核线程 `initproc` 从 `idleproc` 手里接过来，让系统从"只有一个执行流"变成"有多个可调度的执行实体"。

lab4 的一个关键认知是：**内核线程本质上是被 uCore 内核这个唯一"内核进程"所管理的特殊进程**——它们全部共享同一个内核地址空间（`copy_mm` 是空实现，`mm == NULL`），只运行在内核态。所以本实验不需要为每个线程单独建立地址空间，重点落在"PCB + 上下文 + 调度"这套机制上，为 lab5 扩展用户进程铺路。

---

## 二、各功能模块核心函数理解

### 模块一：虚拟内存页操作（`page_insert` / `page_remove`，位于 `kern/mm/pmm.c`）

这一模块建立在已有的 `get_pte()` 之上，负责映射的建立与撤销，核心是保持 **页表项内容、物理页引用计数、TLB 状态** 三者同步。

- **`page_insert(pgdir, page, la, perm)`**：把物理页 `page` 挂到线性地址 `la` 上。
  - 先用 `get_pte(pgdir, la, 1)` 定位（必要时创建中间级页表）页表项；创建失败返回 `-E_NO_MEM`。
  - `page_ref_inc(page)` 先增引用计数，再处理 `la` 上原有的映射：
    - 原地址映射的是**同一个物理页**：只更新权限，把刚才多加的引用减回去；
    - 原地址映射的是**别的物理页**：调 `page_remove_pte` 撤销旧映射（其内部会减引用、必要时释放页）。
  - 最后写入 `pte_create(page2ppn(page), PTE_V | PTE_A | PTE_D | perm)`，并 `tlb_invalidate`。
  - 关键点：A/D 位要一并置位（叶子页表项缺 A/D 会导致对写入报页错误）；"同页更新"与"异页覆盖"两种分支不能混。

- **`page_remove(pgdir, la)`**：幂等地撤销 `la` 上的映射。用 `get_pte(pgdir, la, 0)`（不创建）定位，找不到就安静返回；找到则交给 `page_remove_pte` 处理。

- **`page_remove_pte`（辅助）**：只有当页表项有效时，才 `pte2page` 得到物理页 → `page_ref_dec`，引用计数归零则 `free_page`，随后把页表项清 0 并 `tlb_invalidate`。

这两个函数是 `check_pgdir()` 能通过的关键：它反复测试"建立映射、同一物理页多次映射的引用计数、撤销后旧页表项是否清干净"这三件事。

### 模块二：进程创建与初始化（`kern/process/proc.c`）

这一模块把系统从"能跑内核代码"推进到"能创建并登记多个内核线程"。

- **`alloc_proc()`**：用 `kmalloc` 分配一个 PCB，并把所有字段放到安全空白状态——`state = PROC_UNINIT`、`pid = -1`、`runs = 0`、`kstack = 0`、`parent/mm/tf = NULL`、`context` 清零、`pgdir = boot_pgdir_pa`、名字清零。避免后续误用未初始化字段。

- **`proc_init()`**：先把 `proc_list` 和 `hash_list` 初始化，再把"当前执行现场"包装成第 0 个内核线程 `idleproc`：`pid = 0`、`state = PROC_RUNNABLE`、`kstack = bootstack`、`need_resched = 1`。然后调用 `kernel_thread(init_main, "Hello world!!", 0)` 创建第一个真正的内核线程，并通过 `find_proc(pid)` 拿到 `initproc`。最后用一串 `assert` 校验两个线程的关键字段。idleproc 的特殊之处在于它不是 fork 出来的，而是直接"继承"了当前运行现场。

- **`kernel_thread(fn, arg, clone_flags)`**：因为启动初期没有父进程可复制，需要手动构造一个"空的中断帧"作模板——清零 `tf`，把 `s0 = fn`、`s1 = arg`，`status` 在 `read_csr(sstatus)` 基础上 `| SSTATUS_SPP | SSTATUS_SPIE` 且 `& ~SSTATUS_SIE`（保证首次返回进入 S 态、保留中断使能、当前关中断），`epc = kernel_thread_entry`，再交给 `do_fork(clone_flags | CLONE_VM, 0, &tf)`。

- **`do_fork(clone_flags, stack, tf)`**：进程创建的主干，7 步：`alloc_proc` → `setup_kstack`（分配 2 页内核栈）→ `copy_mm` → `copy_thread` → 关中断后 `get_pid`+`hash_proc`+`list_add`+`nr_process++` → `wakeup_proc` 置 `RUNNABLE` → 返回 pid。前几步任何一步失败都要沿 `bad_fork_cleanup_*` 链回退释放已占资源，避免泄漏。

- **`copy_thread(proc, esp, tf)`**：把 trapframe 安放到新线程自己的内核栈顶 `(kstack + KSTACKSIZE) - 1`，拷贝 tf，把 `gpr.a0 = 0`（子进程返回值）、`gpr.sp = (esp==0) ? tf : esp`，并把 `context.ra = forkret`、`context.sp = tf`。这步精心组织出了新线程首次运行链路 `forkret → forkrets → __trapret`。

- **`copy_mm(clone_flags, proc)`**：lab4 阶段占位实现，`assert(current->mm == NULL)` 后直接返回 0——内核线程共享内核地址空间，不复制 mm。

### 模块三：进程调度（`proc.c` / `sched.c` / `switch.S`）

这一模块把已创建的内核线程真正交给 CPU。

- **`cpu_idle()`**：idle 线程主体，无限循环检测 `current->need_resched`，置位就调 `schedule()` 让出 CPU。

- **`schedule()`**：FIFO/轮转调度器。关中断的临界区内：清 `current->need_resched`；若 `current == idleproc` 则从 `proc_list` 表头找，否则从 `current` 的下一个找；遍历找第一个 `PROC_RUNNABLE` 的线程，找不到就用 `idleproc` 保底；`next->runs++` 后若 `next != current` 调 `proc_run(next)`。

- **`proc_run(proc)`**：在关中断临界区内完成 `current = proc` → `write_csr(satp, ...)` 切页表 → `flush_tlb()` → `switch_to(&prev->context, &next->context)`。切换的原子性由 `local_intr_save/restore` 保证。

- **`switch_to(from, to)`（汇编）**：只做最底层寄存器搬运——把 `ra/sp/s0~s11` 保存到 `from->context`，再从 `to->context` 恢复并 `ret`。因为切换发生在函数内部，caller-saved 寄存器已由编译器自动保存，所以只需保存 callee-saved 的 `s0~s11` 加上 `ra`、`sp`。新线程首次运行时 `context.ra = forkret`，因此 `ret` 会跳到 `forkret`，再由 `forkrets → __trapret` 从中断帧恢复全部寄存器，`epc = kernel_thread_entry` 处 `move a0, s1; jalr s0` 调用线程函数，返回后 `jal do_exit`。

---

## 三、每个练习的 spec 与 prompt

实验要求"借助 AI 生成代码"，每个模块我先从练习要求中提炼 spec，再据此写 prompt。

### 模块一 spec（`page_insert` / `page_remove`）

**spec 要点**：在现有多级页表框架上维护映射。`page_insert` 建立/更新 `la→page` 映射，正确处理"原地址已映射到同一页"和"已映射到其他页"两种情况，失败返回 `-E_NO_MEM` 且不留半成品；`page_remove` 幂等删除，无映射时安静返回。全程保持"页表项、引用计数、TLB"三者同步。可借助 `get_pte/pte_create/pte2page/page_ref_inc/page_ref_dec/alloc_page/free_page/tlb_invalidate` 等。

**prompt 示例**：

> 你是操作系统内核开发者，请实现 RISC-V uCore 的 `page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm)` 与 `page_remove(pde_t *pgdir, uintptr_t la)`（位于 kern/mm/pmm.c）。要求：使用 `get_pte(pgdir, la, 1)` 定位页表项；先增引用计数再处理旧映射，若旧映射指向同一物理页则撤销多余引用并只更新权限，否则先撤销旧映射；映射成功后写 `pte_create(page2ppn(page), PTE_V|PTE_A|PTE_D|perm)` 并 `tlb_invalidate`；`page_remove` 需幂等。请同时给出 `page_remove_pte` 辅助函数。注意引用计数与 TLB 刷新的正确性。

### 模块二 spec（`alloc_proc` / `proc_init` / `kernel_thread` / `do_fork` / `copy_thread` / `copy_mm`）

**spec 要点**：`alloc_proc` 返回字段全初始化的 PCB；`proc_init` 包装当前现场为 idleproc 并创建 initproc；`kernel_thread` 用局部 trapframe 组织 fn/arg/status/epc 后交给 `do_fork`；`do_fork` 完成分配 PCB→内核栈→copy_mm→copy_thread→入链表→唤醒，失败要清理；`copy_thread` 把 tf 放到内核栈顶并设置 `context.ra=forkret`、`context.sp=tf`；`copy_mm` 空实现。

**prompt 示例**：

> 请实现 uCore RISC-V lab4 的进程创建函数（kern/process/proc.c）：`alloc_proc`、`proc_init`、`kernel_thread`、`do_fork`、`copy_thread`、`copy_mm`。proc_init 中把当前执行现场设为 idleproc（pid=0、RUNNABLE、kstack=bootstack、need_resched=1），再用 kernel_thread(init_main,"Hello world!!",0) 创建 initproc 并 find_proc 取回。do_fork 按"alloc_proc→setup_kstack→copy_mm→copy_thread→关中断入链表→wakeup_proc→返回 pid"实现，出错沿 bad_fork_cleanup_* 回退。copy_thread 中 tf 放在 kstack 栈顶，a0=0，context.ra=forkret、context.sp=tf。copy_mm 因内核线程共享地址空间直接返回 0。

### 模块三 spec（`cpu_idle` / `schedule` / `proc_run` / `switch_to`）

**spec 要点**：`cpu_idle` 循环检测 need_resched 并 schedule；`schedule` 关中断选出下一个 RUNNABLE 线程，找不到退 idleproc；`proc_run` 关中断内切 current、写 satp 切页表、flush_tlb、switch_to；`switch_to` 只保存/恢复 ra/sp/s0~s11。

**prompt 示例**：

> 请实现 uCore RISC-V lab4 的调度与切换：`cpu_idle`（proc.c）、`schedule`（sched.c）、`proc_run`（proc.c）和汇编 `switch_to`（switch.S）。schedule 用 FIFO/轮转策略从 proc_list 选下一个 RUNNABLE 线程（idleproc 不在链表，找不到则退 idleproc），临界区用 local_intr_save/restore 关中断。proc_run 内更新 current、write_csr(satp, ...) 切页表并 flush_tlb 后再 switch_to。switch_to 按 context 结构体布局（ra=0,sp=8,s0=16,…,s11=104）保存 from、恢复 to 后 ret。

---

## 四、AI 生成代码过程中出现的 bug 及解决

### 1. 核心 bug：中间级页表项 U 位导致 store page fault（最关键，排查最久）

**现象**：`check_pgdir()` 通过后，内核在 `check_boot_pgdir()` 的 `strcpy((void *)0x100, "ucore: Hello world!!")` 处触发 `store_page_fault`（写 0x100），随后跳转到 `stvec = 0x80200000`（即 `kern_entry`），导致内核反复重启、日志里不断刷 `DTB Init`，最后第二次启动时 `pa2page called with invalid pa` panic。

**排查过程**：这个 bug 极其隐蔽——用 gdb 和 qemu 的 `monitor xp` 直接读物理内存，页表内容（root[0] → level1[0] → level0[0]）数值完全正确，`satp = 0x800000000008020b`（Sv39 + 正确的根页表 PPN）也正确，但 qemu 的 `-d mmu,guest_errors` 日志显示对 0x100 的写 `riscv_cpu_tlb_fill ... ret 1`（遍历失败）。最终查 qemu 源码定位到根因：**现代 qemu（自提交 `qemu/qemu@8fcfe17` 起）严格执行 RISC-V 规范，非叶子（inner）页表项的 D/A/U 三个保留位必须全为 0**，否则直接返回 `TRANSLATE_FAIL`。而框架代码 `get_pte()` 创建中间级页表项时用的是 `pte_create(ppn, PTE_U | PTE_V)`，把 U 位置成了 1。

**解决**：把 `get_pte()` 里两处中间级页表项的创建改为 `pte_create(page2ppn(page), 0)`（只保留 V 位）；同时把 `check_pgdir()` 里按"U 位向上传播"旧约定写的断言 `assert(boot_pgdir_va[0] & PTE_U)` 改为 `assert(!(boot_pgdir_va[0] & PTE_U))`。叶子页表项（`page_insert` 里）保持 `PTE_V | PTE_A | PTE_D | perm`，A/D 位必须置 1。修复后 `check_boot_pgdir()` 通过，`initproc` 正常打印 "Hello world!!"。

> 注：该 bug 位于框架 `get_pte()`（lab2/lab3 遗留）而非本次 AI 生成的 `page_insert/page_remove`，但它是在运行 AI 生成代码触发 `check_boot_pgdir` 时暴露出来的，且与本实验"页表项/TLB"主题直接相关，故记录于此。这也提醒：**AI 生成的 `page_insert` 里显式写 `PTE_A | PTE_D` 是必要的**，缺了它同样会在写映射时页错误。

### 2. 引用计数/同页映射的边界（AI 代码易错点，写 spec 时重点约束）

`page_insert` 最容易错的是"同一线性地址已经映射到同一物理页"时的处理：如果先无条件 `page_ref_inc` 又不把多余引用减掉，引用计数会虚高；如果旧映射指向别的页却直接覆盖，旧页会泄漏。因此 spec 里明确要求"先增引用，再区分同页/异页两种情况"，并让 `check_pgdir` 里 `page_ref(p1)==2 / page_ref(p2)==0` 这类断言来兜底。

---

## 五、重要知识点及与 OS 原理的对应

| 本实验知识点 | OS 原理对应概念 | 含义、关系与差异 |
|---|---|---|
| 进程控制块 `struct proc_struct` | PCB（进程控制块） | 一致。实验里 PCB 聚合了状态、pid、内核栈、上下文、中断帧、页目录、链表节点等，是进程/线程在系统里的"档案"。 |
| 进程状态机（UNINIT/RUNNABLE/SLEEPING/ZOMBIE） | 三态/五态进程模型 | 实验只实现四态及 `alloc_proc→wakeup_proc` 就绪转换，SLEEPING/ZOMBIE 的完整转换留到后续实验。 |
| 内核线程共享地址空间（`copy_mm` 空实现、`mm==NULL`） | 进程 vs 线程、轻量级进程 | 对应"线程是共享地址空间的执行流"：内核线程只在内核态、共享内核地址空间；用户进程（lab5）才有独立 mm。 |
| 上下文 `struct context` + `switch_to` | 上下文切换、callee-saved 约定 | 只保存 `ra/sp/s0~s11` 是基于"切换发生在函数内、caller-saved 由编译器保存"的约定，是寄存器保存开销的最小化。 |
| 中断帧 `trapframe` + `forkret/forkrets/__trapret` | 现场保护与恢复、进程首次运行 | 用"伪造中断帧"让新线程通过 `sret` 首次进入 `kernel_thread_entry`，与硬件 trap 返回机制复用，是巧妙的"进程出生"方式。 |
| 多级页表 Sv39 + `get_pte/page_insert/page_remove` | 分页、页表、虚拟内存 | 一致。实验把"虚拟地址→物理页"的映射、引用计数、TLB 一致性作为地址空间基础。 |
| 内/叶子页表项 D/A/U 位（inner PTE 保留位） | 页表项格式、保留位规范 | 本实验暴露的坑：软件层面读起来"对"，硬件规范层面却非法——体现"实现要遵循 ISA 规范"而非只看逻辑正确。 |
| TLB 刷新 `sfence.vma` | TLB 与页表一致性 | 一致。改页表后必须刷新 TLB，否则出现"页表已更新、翻译仍旧值"的现象。 |
| FIFO/轮转调度 `schedule` | 进程调度、时间片轮转 | 实验是极简轮转（从当前项向后找下一个 RUNNABLE），真正的优先级/多级队列调度留到后续。 |
| 关中断保证切换原子性 `local_intr_save/restore` | 临界区、原子性 | 通过 `sstatus` 的 SIE 位关闭中断，防止切换过程中 current/页表/现场三者不一致。 |
| 空闲线程 idleproc | 调度兜底、idle 进程 | 一致：系统无任务时 idle 兜底占用 CPU，并让调度流程统一化。 |

## 六、OS 原理中重要、但本实验未对应的知识点

1. **进程间通信（IPC）与同步互斥**：信号量、互斥锁、条件变量、管程、消息传递等——lab4 只有两个内核线程、无共享临界资源竞争，尚未涉及。
2. **死锁**：资源分配、死锁预防/避免/检测与恢复完全没有对应（无多资源竞争场景）。
3. **进程调度的高级策略**：优先级调度、多级反馈队列、实时调度、CFS 等——实验只实现最简单的 FIFO/轮转，未体现"公平/优先/实时"等调度目标。
4. **用户进程与地址空间隔离**：本实验所有内核线程共享同一地址空间，未建立独立的用户地址空间、缺页异常处理和页置换（demand paging / swap）。
5. **写时复制（Copy-on-Write）**：`copy_mm` 是空实现，未体现 fork 时物理页共享 + 按需复制。
6. **进程生命周期完整管理**：`do_exit`/`do_wait`/僵尸进程回收、父进程等待子进程等——lab4 里 `do_exit` 只是 `panic` 桩。
7. **保护机制细节**：特权级切换（用户态/内核态）、系统调用、内存越界访问的完整保护（SUM/MXR 等位）尚未充分体现。
