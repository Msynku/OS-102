#include <defs.h>
#include <assert.h>
#include <pmm.h>
#include <proc.h>
#include <sched.h>
#include <stdio.h>

static void lab4_pass(const char *name)
{
    cprintf("[Test %s] PASS\n", name);
}

void test_lab4_pmm_basic(void)
{
    struct Page *p1 = alloc_page();
    struct Page *p2 = alloc_page();
    struct Page *p3 = alloc_page();
    pte_t *ptep;
    size_t free_before;

    assert(p1 != NULL && p2 != NULL && p3 != NULL);

    // 这里检查软件读取的 PTE 和引用计数，并不实际访问这些权限组合的地址。
    assert(page_insert(boot_pgdir_va, p1, 0, 0) == 0);
    assert((ptep = get_pte(boot_pgdir_va, 0, 0)) != NULL);
    assert((*ptep & PTE_V) && pte2page(*ptep) == p1 && page_ref(p1) == 1);

    // 覆盖为另一页：p1 同时被两处引用，p2 的旧引用归零。
    assert(page_insert(boot_pgdir_va, p2, PGSIZE, PTE_U | PTE_W) == 0);
    assert(page_insert(boot_pgdir_va, p1, PGSIZE, 0) == 0);
    assert(page_ref(p1) == 2 && page_ref(p2) == 0);
    assert((ptep = get_pte(boot_pgdir_va, PGSIZE, 0)) != NULL);
    assert(pte2page(*ptep) == p1 && (*ptep & (PTE_U | PTE_W)) == 0);

    // 同一物理页重复映射到同一地址，不应多计引用或释放该页。
    assert(page_insert(boot_pgdir_va, p3, 2 * PGSIZE, 0) == 0);
    free_before = nr_free_pages();
    assert(page_insert(boot_pgdir_va, p3, 2 * PGSIZE, PTE_W) == 0);
    assert(page_ref(p3) == 1 && nr_free_pages() == free_before);

    // 撤销全部映射，最后重复删除一次，检查无映射时不会重复释放。
    page_remove(boot_pgdir_va, 0);
    page_remove(boot_pgdir_va, PGSIZE);
    page_remove(boot_pgdir_va, 2 * PGSIZE);
    page_remove(boot_pgdir_va, 2 * PGSIZE);
    assert(page_ref(p1) == 0 && page_ref(p2) == 0 && page_ref(p3) == 0);

    lab4_pass("PMM");
}

void test_lab4_process_create(void)
{
    // proc_init 结束后，idle 仍在运行，init 已创建但尚未首次执行。
    assert(current == idleproc);
    assert(idleproc != NULL && idleproc->pid == 0);
    assert(idleproc->state == PROC_RUNNABLE);
    assert(idleproc->kstack == (uintptr_t)bootstack);
    assert(idleproc->parent == NULL && idleproc->mm == NULL);
    assert(idleproc->tf == NULL && idleproc->runs == 0);
    assert(idleproc->flags == 0 && idleproc->context.ra == 0);

    assert(initproc != NULL && initproc->pid == 1);
    assert(initproc->state == PROC_RUNNABLE);
    assert(initproc->parent == idleproc && initproc->mm == NULL);
    assert(initproc->context.ra != 0 && initproc->context.sp != 0);

    lab4_pass("Process Create");
}

static volatile int test_thread1_ran;
static volatile int test_thread2_ran;

static void test_lab4_thread(void *arg)
{
    int id = (int)(intptr_t)arg;
    int i;
    // 两个测试线程分别留下运行标记并主动调度，不依赖时钟抢占。
    for (i = 0; i < 3; i++)
    {
        if (id == 1) test_thread1_ran = 1;
        else test_thread2_ran = 1;
        current->need_resched = 1;
        schedule();
    }
    // 继续让出 CPU，避免线程返回后进入尚未实现的 do_exit。
    while (1)
    {
        current->need_resched = 1;
        schedule();
    }
}

void test_lab4_schedule(void)
{
    int i;
    test_thread1_ran = test_thread2_ran = 0;
    assert(kernel_thread(test_lab4_thread, (void *)(intptr_t)1, 0) > 0);
    assert(kernel_thread(test_lab4_thread, (void *)(intptr_t)2, 0) > 0);

    for (i = 0; i < 8; i++)
    {
        current->need_resched = 1;
        schedule();
    }
    assert(test_thread1_ran && test_thread2_ran); // 两个线程都曾被执行
    lab4_pass("Schedule");
}
