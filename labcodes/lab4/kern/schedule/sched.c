#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>

// proc 是要唤醒的线程；这里只改状态，不会立即切换 CPU。
void
wakeup_proc(struct proc_struct *proc) {
    assert(proc->state != PROC_ZOMBIE && proc->state != PROC_RUNNABLE); // 已退出或已就绪的线程不应再次唤醒
    // 唤醒只改变状态，真正开始执行还需要调度器选择。
    proc->state = PROC_RUNNABLE;
}

//LAB4 function3 2413140
// schedule - round-robin scheduler: pick the next RUNNABLE process after the
//            current one in proc_list (idleproc is not in proc_list).
void
schedule(void) {
    bool intr_flag;                // 进入调度前的中断状态，用于最后恢复
    list_entry_t *le, *last;       // le 是正在检查的链表节点，last 是本轮遍历的起点
    struct proc_struct *next = NULL; // next 指向找到的下一个可运行线程
    // 遍历链表和选择下一个线程期间保持当前 CPU 的中断关闭。
    local_intr_save(intr_flag);
    {
        current->need_resched = 0; // 本次调度请求正在处理，先清除标记
        // idleproc 不在链表里：从表头找；其他线程从自己之后开始轮转。
        last = (current == idleproc) ? &proc_list : &(current->list_link);
        le = last; // 从起点的下一个节点开始查找
        do {
            if ((le = list_next(le)) != &proc_list) { // 表头只是哨兵节点，不代表进程
                next = le2proc(le, list_link);        // 根据链表节点找到所属 PCB
                if (next->state == PROC_RUNNABLE) {   // 只选择处于可运行状态的线程
                    break;                            // 找到后停止向后查找
                }
            }
        } while (le != last); // 转回起点说明已经检查完一轮
        // 找不到可运行线程时由 idleproc 兜底；找到当前线程则不必切换。
        if (next == NULL || next->state != PROC_RUNNABLE) {
            next = idleproc;
        }
        next->runs++; // 记录该线程被调度选中的次数
        if (next != current) {
            proc_run(next);
        }
    }
    local_intr_restore(intr_flag); // 按进入调度前的状态恢复中断
}


