#include <default_pmm.h>
#include <defs.h>
#include <error.h>
#include <kmalloc.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <sync.h>
#include <vmm.h>
#include <riscv.h>
#include <dtb.h>

// pages[] 是物理页的描述符数组；npage 是从物理地址 0 算起的结束页号。
struct Page *pages;
size_t npage = 0;
// The kernel image is mapped at VA=KERNBASE and PA=info.base
uint_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
const size_t nbase = DRAM_BASE / PGSIZE;

// 同一张启动页表：内核通过虚拟地址访问，satp 使用物理地址。
pde_t *boot_pgdir_va = NULL;
uintptr_t boot_pgdir_pa;

// physical memory management
const struct pmm_manager *pmm_manager;

static void check_alloc_page(void);
static void check_pgdir(void);
static void check_boot_pgdir(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void)
{
    pmm_manager = &default_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n)
{
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n)
{
    struct Page *page = NULL;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        page = pmm_manager->alloc_pages(n);
    }
    local_intr_restore(intr_flag);
    return page;
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n)
{
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        pmm_manager->free_pages(base, n);
    }
    local_intr_restore(intr_flag);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void)
{
    size_t ret;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        ret = pmm_manager->nr_free_pages();
    }
    local_intr_restore(intr_flag);
    return ret;
}

//lab2 请补充你在lab2中的代码
static void page_init(void)
{
    extern char end[];

    // 设备树给出 DRAM 范围；先保留内核和 Page 数组占用的空间。
    uint64_t mem_begin = get_memory_base();
    uint64_t mem_size = get_memory_size();
    uint64_t mem_end = mem_begin + mem_size;

    // npage is the absolute number of physical pages from address 0 to the end
    // of DRAM. pa2page()/KADDR() verify "PPN(pa) >= npage", and PPN() includes
    // the DRAM_BASE offset (nbase), so npage must also include it.
    npage = mem_end / PGSIZE;

    // The kernel is directly mapped: KVA = PA + va_pa_offset
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    // Place the Page array right after the kernel's bss, page-aligned
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);

    // The number of DRAM pages actually managed by pages[]
    size_t nr_dram_pages = mem_size / PGSIZE;

    // Reserve every managed page first, then hand the tail region to the pmm
    struct Page *p;
    for (p = pages; p < pages + nr_dram_pages; p++)
    {
        SetPageReserved(p);
    }

    // Page 数组之后、DRAM 末尾之前的完整物理页交给分配器。
    uintptr_t used_end = ROUNDUP(PADDR((uintptr_t)pages) +
                                     nr_dram_pages * sizeof(struct Page),
                                 PGSIZE);
    init_memmap(pa2page(used_end), (mem_end - used_end) / PGSIZE);
}

static void enable_paging(void)
{
    write_csr(satp, 0x8000000000000000 | (boot_pgdir_pa >> RISCV_PGSHIFT));
}

// boot_map_segment - setup&enable the paging mechanism
// parameters
//  la:   linear address of this memory need to map (after x86 segment map)
//  size: memory size
//  pa:   physical address of this memory
//  perm: permission of this memory
static void boot_map_segment(pde_t *pgdir, uintptr_t la, size_t size,
                             uintptr_t pa, uint32_t perm)
{
    assert(PGOFF(la) == PGOFF(pa));
    size_t n = ROUNDUP(size + PGOFF(la), PGSIZE) / PGSIZE;
    la = ROUNDDOWN(la, PGSIZE);
    pa = ROUNDDOWN(pa, PGSIZE);
    for (; n > 0; n--, la += PGSIZE, pa += PGSIZE)
    {
        pte_t *ptep = get_pte(pgdir, la, 1);
        assert(ptep != NULL);
        *ptep = pte_create(pa >> PGSHIFT, PTE_V | perm);
    }
}

// boot_alloc_page - allocate one page using pmm->alloc_pages(1)
// return value: the kernel virtual address of this allocated page
// note: this function is used to get the memory for PDT(Page Directory
// Table)&PT(Page Table)
static void *boot_alloc_page(void)
{
    struct Page *p = alloc_page();
    if (p == NULL)
    {
        panic("boot_alloc_page failed.\n");
    }
    return page2kva(p);
}

// pmm_init - setup a pmm to manage physical memory, build PDT&PT to setup
// paging mechanism
//         - check the correctness of pmm & paging mechanism, print PDT&PT
void pmm_init(void)
{
    // 先准备物理页分配器和空闲页，后续页表、PCB、内核栈都要用它。
    init_pmm_manager();
    page_init();

    check_alloc_page(); // 先检查物理页的分配和释放
    // 启动页表由 entry.S 提供；记录其虚拟、物理地址以便操作和切换。
    extern char boot_page_table_sv39[];
    boot_pgdir_va = (pte_t *)boot_page_table_sv39;
    boot_pgdir_pa = PADDR(boot_pgdir_va);

    check_pgdir();

    static_assert(KERNBASE % PTSIZE == 0 && KERNTOP % PTSIZE == 0);

    check_boot_pgdir();

    // kmalloc 用页分配器管理较小对象，后面用它分配 PCB 等结构。
    kmalloc_init();
}

// pgdir：根页表的内核虚拟地址；la：要查找的虚拟地址。
// create：中间页表不存在时，是否允许分配新页表；返回该地址对应的叶子表项指针。
// 返回非空只表示找到了表项位置，还要检查表项的 V 位才能知道是否有映射。
// 不允许创建或分配失败时返回 NULL；已创建的上级页表不会在此回滚。
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create)
{
    // Sv39 按 PDX1、PDX0、PTX 逐级查表；本函数按普通 4 KiB 页查找。
    pde_t *pdep1 = &pgdir[PDX1(la)]; // pdep1 指向根页表中选出的表项
    if (!(*pdep1 & PTE_V))            // V 为 0：下一级页表还不存在
    {
        struct Page *page; // 新页表所用物理页的描述符，不是页本身的地址
        // 不允许创建或内存不足时返回 NULL。
        if (!create || (page = alloc_page()) == NULL)
        {
            return NULL;
        }
        // 新页表页要清零并保留引用。
        set_page_ref(page, 1);          // 根页表将引用这个物理页
        uintptr_t pa = page2pa(page);   // pa 是新页表页的物理地址
        memset(KADDR(pa), 0, PGSIZE);  // 转成内核虚拟地址后清零，所有新表项初始无效
        // 非叶子项只置 V：U/A/D 是保留位；pte_create 已自动加 PTE_V。
        *pdep1 = pte_create(page2ppn(page), 0);
    }
    // 表项保存物理页号，先转为内核虚拟地址，再访问下一层页表。
    pde_t *pdep0 = &((pte_t *)KADDR(PDE_ADDR(*pdep1)))[PDX0(la)]; // pdep0 指向第二级表项
    if (!(*pdep0 & PTE_V))
    {
        struct Page *page; // 最后一级页表所用物理页的描述符
        if (!create || (page = alloc_page()) == NULL)
        {
            return NULL;
        }
        set_page_ref(page, 1);          // 第二级表项将引用这个物理页
        uintptr_t pa = page2pa(page);   // 新页表页的物理地址
        memset(KADDR(pa), 0, PGSIZE);  // 清空其中所有表项
        *pdep0 = pte_create(page2ppn(page), 0); // 填入物理页号并置 V，指向最后一级页表
    }
    // 返回叶子 PTE 的地址；该项是否已有映射仍需检查 PTE_V。
    return &((pte_t *)KADDR(PDE_ADDR(*pdep0)))[PTX(la)];
}

// pgdir 和 la 指定要查找的映射；ptep_store 非空时，用它带回叶子表项的地址。
// 返回对应的物理页描述符；表项不存在或 V 为 0 时返回 NULL。
// 这里按 PTE_V 判断，不检查 R/W/X 的硬件合法性。
struct Page *get_page(pde_t *pgdir, uintptr_t la, pte_t **ptep_store)
{
    pte_t *ptep = get_pte(pgdir, la, 0); // ptep 是表项地址；0 表示不创建新页表
    if (ptep_store != NULL)
    {
        *ptep_store = ptep; // 调用者可以借这个输出参数拿到表项地址
    }
    if (ptep != NULL && *ptep & PTE_V)
    {
        return pte2page(*ptep);
    }
    return NULL;
}

//LAB4 function1 2411938
// pgdir 是根页表，la 是虚拟地址，ptep 指向要撤销的叶子表项。
// 撤销有效映射并更新物理页的引用计数；只在最后一个引用消失时释放物理页。
static inline void page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep)
{
    if (*ptep & PTE_V)
    {
        // 一个页可被多个虚拟页引用；只在最后一个引用消失时释放。
        struct Page *page = pte2page(*ptep); // 找到旧映射所指物理页的描述符
        if (page_ref_dec(page) == 0)         // 去掉一个引用；没有引用后才能释放
        {
            free_page(page);
        }
        *ptep = 0;                    // 清除旧表项，虚拟页不再有这个映射
        tlb_invalidate(pgdir, la);   // 使 CPU 缓存的旧地址翻译失效
    }
}

// pgdir 是根页表，page 描述目标物理页，la 是要映射的虚拟地址，perm 是权限位。
// 成功返回 0；无法创建中间页表时返回 -E_NO_MEM。旧映射存在时更新权限或替换旧页。
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm)
{
    // 先定位叶子项；无法创建中间页表时，不写目标映射。
    pte_t *ptep = get_pte(pgdir, la, 1); // ptep 指向 la 对应的叶子表项；1 表示允许创建
    if (ptep == NULL)
    {
        return -E_NO_MEM;
    }
    // 先保护新页，避免旧映射指向同一页时被误释放。
    page_ref_inc(page);
    if (*ptep & PTE_V)
    {
        struct Page *p = pte2page(*ptep); // p 是原映射指向的物理页描述符
        if (p == page)
        {
            // 同一映射仅改权限，抵消刚才多加的引用。
            page_ref_dec(page);
        }
        else
        {
            // 换到另一物理页时，先撤销旧映射并减少旧页引用。
            page_remove_pte(pgdir, la, ptep);
        }
    }
    // 按传入的 perm 写物理页号并预置 A/D，再让旧地址翻译缓存失效。
    *ptep = pte_create(page2ppn(page), PTE_V | PTE_A | PTE_D | perm);
    tlb_invalidate(pgdir, la);
    return 0;
}

// 在 pgdir 指定的页表中撤销虚拟地址 la 的映射；目标不存在时安全返回。
void page_remove(pde_t *pgdir, uintptr_t la)
{
    // 删除时只查找，不创建页表；未映射时无操作，也不回收空的中间级页表。
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep != NULL)
    {
        page_remove_pte(pgdir, la, ptep);
    }
}



// 按虚拟地址刷新本 hart 的翻译缓存；当前实现不判断 pgdir 是否正被使用。
void tlb_invalidate(pde_t *pgdir, uintptr_t la)
{
    // flush_tlb();
    // The flush_tlb flush the entire TLB, is there any better way?
    asm volatile("sfence.vma %0" : : "r"(la));
}

static void check_alloc_page(void)
{
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}

static void check_pgdir(void)
{
    // assert(npage <= KMEMSIZE / PGSIZE);
    // The memory starts at 2GB in RISC-V
    // so npage is always larger than KMEMSIZE / PGSIZE
    size_t nr_free_store;

    nr_free_store = nr_free_pages();

    assert(npage <= KERNTOP / PGSIZE);
    assert(boot_pgdir_va != NULL && (uint32_t)PGOFF(boot_pgdir_va) == 0);
    assert(get_page(boot_pgdir_va, 0x0, NULL) == NULL);

    struct Page *p1, *p2;
    p1 = alloc_page();
    assert(page_insert(boot_pgdir_va, p1, 0x0, 0) == 0);

    pte_t *ptep;
    assert((ptep = get_pte(boot_pgdir_va, 0x0, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert(page_ref(p1) == 1);

    ptep = (pte_t *)KADDR(PDE_ADDR(boot_pgdir_va[0]));
    ptep = (pte_t *)KADDR(PDE_ADDR(ptep[0])) + 1;
    assert(get_pte(boot_pgdir_va, PGSIZE, 0) == ptep);

    p2 = alloc_page();
    assert(page_insert(boot_pgdir_va, p2, PGSIZE, PTE_U | PTE_W) == 0);
    assert((ptep = get_pte(boot_pgdir_va, PGSIZE, 0)) != NULL);
    assert(*ptep & PTE_U);
    assert(*ptep & PTE_W);
    assert(!(boot_pgdir_va[0] & PTE_U));
    assert(page_ref(p2) == 1);

    assert(page_insert(boot_pgdir_va, p1, PGSIZE, 0) == 0);
    assert(page_ref(p1) == 2);
    assert(page_ref(p2) == 0);
    assert((ptep = get_pte(boot_pgdir_va, PGSIZE, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert((*ptep & PTE_U) == 0);

    page_remove(boot_pgdir_va, 0x0);
    assert(page_ref(p1) == 1);
    assert(page_ref(p2) == 0);

    page_remove(boot_pgdir_va, PGSIZE);
    assert(page_ref(p1) == 0);
    assert(page_ref(p2) == 0);

    assert(page_ref(pde2page(boot_pgdir_va[0])) == 1);

    pde_t *pd1 = boot_pgdir_va, *pd0 = page2kva(pde2page(boot_pgdir_va[0]));
    free_page(pde2page(pd0[0]));
    free_page(pde2page(pd1[0]));
    boot_pgdir_va[0] = 0;
    flush_tlb();

    assert(nr_free_store == nr_free_pages());

    cprintf("check_pgdir() succeeded!\n");
}

static void check_boot_pgdir(void)
{
    size_t nr_free_store;
    pte_t *ptep;
    int i;

    nr_free_store = nr_free_pages();

    for (i = ROUNDDOWN(KERNBASE, PGSIZE); i < npage * PGSIZE; i += PGSIZE)
    {
        assert((ptep = get_pte(boot_pgdir_va, (uintptr_t)KADDR(i), 0)) != NULL);
        assert(PTE_ADDR(*ptep) == i);
    }

    assert(boot_pgdir_va[0] == 0);

    struct Page *p;
    p = alloc_page();
    // 两个虚拟页映射到同一物理页；页内偏移都为 0x100，引用计数为 2。
    assert(page_insert(boot_pgdir_va, p, 0x100, PTE_W | PTE_R) == 0);
    assert(page_ref(p) == 1);
    assert(page_insert(boot_pgdir_va, p, 0x100 + PGSIZE, PTE_W | PTE_R) == 0);
    assert(page_ref(p) == 2);

    const char *str = "ucore: Hello world!!";
    strcpy((void *)0x100, str);
    assert(strcmp((void *)0x100, (void *)(0x100 + PGSIZE)) == 0);

    *(char *)(page2kva(p) + 0x100) = '\0';
    assert(strlen((const char *)0x100) == 0);

    pde_t *pd1 = boot_pgdir_va, *pd0 = page2kva(pde2page(boot_pgdir_va[0]));
    free_page(p);
    free_page(pde2page(pd0[0]));
    free_page(pde2page(pd1[0]));
    boot_pgdir_va[0] = 0;
    flush_tlb();

    assert(nr_free_store == nr_free_pages());

    cprintf("check_boot_pgdir() succeeded!\n");
}

// perm2str - use string 'u,r,w,-' to present the permission
static const char *perm2str(int perm)
{
    static char str[4];
    str[0] = (perm & PTE_U) ? 'u' : '-';
    str[1] = 'r';
    str[2] = (perm & PTE_W) ? 'w' : '-';
    str[3] = '\0';
    return str;
}

// get_pgtable_items - In [left, right] range of PDT or PT, find a continuous
// linear addr space
//                  - (left_store*X_SIZE~right_store*X_SIZE) for PDT or PT
//                  - X_SIZE=PTSIZE=4M, if PDT; X_SIZE=PGSIZE=4K, if PT
// paramemters:
//  left:        no use ???
//  right:       the high side of table's range
//  start:       the low side of table's range
//  table:       the beginning addr of table
//  left_store:  the pointer of the high side of table's next range
//  right_store: the pointer of the low side of table's next range
//  return value: 0 - not a invalid item range, perm - a valid item range with
//  perm permission
static int get_pgtable_items(size_t left, size_t right, size_t start,
                             uintptr_t *table, size_t *left_store,
                             size_t *right_store)
{
    if (start >= right)
    {
        return 0;
    }
    while (start < right && !(table[start] & PTE_V))
    {
        start++;
    }
    if (start < right)
    {
        if (left_store != NULL)
        {
            *left_store = start;
        }
        int perm = (table[start++] & PTE_USER);
        while (start < right && (table[start] & PTE_USER) == perm)
        {
            start++;
        }
        if (right_store != NULL)
        {
            *right_store = start;
        }
        return perm;
    }
    return 0;
}
