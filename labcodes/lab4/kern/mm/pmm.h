#ifndef __KERN_MM_PMM_H__
#define __KERN_MM_PMM_H__

#include <defs.h>
#include <mmu.h>
#include <memlayout.h>
#include <atomic.h>
#include <assert.h>

/* fork flags used in do_fork*/
#define CLONE_VM 0x00000100     // set if VM shared between processes
#define CLONE_THREAD 0x00000200 // thread group

// pmm_manager is a physical memory management class. A special pmm manager - XXX_pmm_manager
// only needs to implement the methods in pmm_manager class, then XXX_pmm_manager can be used
// by ucore to manage the total physical memory space.
struct pmm_manager
{
    const char *name;                                 // XXX_pmm_manager's name
    void (*init)(void);                               // initialize internal description&management data structure
                                                      // (free block list, number of free block) of XXX_pmm_manager
    void (*init_memmap)(struct Page *base, size_t n); // setup description&management data structcure according to
                                                      // the initial free physical memory space
    struct Page *(*alloc_pages)(size_t n);            // allocate >=n pages, depend on the allocation algorithm
    void (*free_pages)(struct Page *base, size_t n);  // free >=n pages with "base" addr of Page descriptor structures(memlayout.h)
    size_t (*nr_free_pages)(void);                    // return the number of free pages
    void (*check)(void);                              // check the correctness of XXX_pmm_manager
};

extern const struct pmm_manager *pmm_manager;
extern pde_t *boot_pgdir_va;
extern const size_t nbase;
extern uintptr_t boot_pgdir_pa;

void pmm_init(void);

struct Page *alloc_pages(size_t n);
void free_pages(struct Page *base, size_t n);
size_t nr_free_pages(void);

#define alloc_page() alloc_pages(1)
#define free_page(page) free_pages(page, 1)

pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create);
struct Page *get_page(pde_t *pgdir, uintptr_t la, pte_t **ptep_store);
void page_remove(pde_t *pgdir, uintptr_t la);
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);

void tlb_invalidate(pde_t *pgdir, uintptr_t la);
struct Page *pgdir_alloc_page(pde_t *pgdir, uintptr_t la, uint32_t perm);

void print_pgdir(void);

// 内核直接映射区：PADDR 将内核虚拟地址减去偏移，得到物理地址。
#define PADDR(kva)                                                 \
    ({                                                             \
        uintptr_t __m_kva = (uintptr_t)(kva);                      \
        if (__m_kva < KERNBASE)                                    \
        {                                                          \
            panic("PADDR called with invalid kva %08lx", __m_kva); \
        }                                                          \
        __m_kva - va_pa_offset;                                    \
    })

// KADDR 做反向转换；解引用 PTE 中的下一级物理页前需要先转换。
#define KADDR(pa)                                                \
    ({                                                           \
        uintptr_t __m_pa = (pa);                                 \
        size_t __m_ppn = PPN(__m_pa);                            \
        if (__m_ppn >= npage)                                    \
        {                                                        \
            panic("KADDR called with invalid pa %08lx", __m_pa); \
        }                                                        \
        (void *)(__m_pa + va_pa_offset);                         \
    })

extern struct Page *pages;
extern size_t npage;
extern uint_t va_pa_offset;

static inline ppn_t
page2ppn(struct Page *page)
{
    // Page 数组从 DRAM 起点开始，nbase 补上物理页号的起始偏移。
    return page - pages + nbase;
}

static inline uintptr_t
page2pa(struct Page *page)
{
    return page2ppn(page) << PGSHIFT; // 物理页号左移 12 位，得到该页的物理起始地址
}

// pa 是物理地址：先求物理页号，再定位管理该物理页的 Page 描述符。
static inline struct Page *
pa2page(uintptr_t pa)
{
    if (PPN(pa) >= npage)
    {
        panic("pa2page called with invalid pa");
    }
    return &pages[PPN(pa) - nbase];
}

static inline void *
page2kva(struct Page *page)
{
    return KADDR(page2pa(page)); // 描述符 → 物理地址 → 内核可访问的虚拟地址
}

static inline struct Page *
kva2page(void *kva)
{
    return pa2page(PADDR(kva));
}

static inline struct Page *
pte2page(pte_t pte)
{
    if (!(pte & PTE_V))
    {
        panic("pte2page called with invalid pte");
    }
    return pa2page(PTE_ADDR(pte));
}

static inline struct Page *
pde2page(pde_t pde)
{
    return pa2page(PDE_ADDR(pde));
}

// ref 记录该物理页的引用数；页表建立映射时增加，删除映射时减少。
static inline int
page_ref(struct Page *page)
{
    return page->ref;
}

static inline void
set_page_ref(struct Page *page, int val)
{
    page->ref = val;
}

static inline int
page_ref_inc(struct Page *page)
{
    page->ref += 1;
    return page->ref;
}

static inline int
page_ref_dec(struct Page *page)
{
    page->ref -= 1;
    return page->ref;
}

static inline void flush_tlb()
{
    // 刷新当前 hart 的地址翻译缓存；不负责其他 hart 的同步。
    asm volatile("sfence.vma");
}

// ppn 是物理页号，type 是要加入的权限等标志；返回组合好的页表项。
// 即使 type=0 也会自动置有效位 V。
static inline pte_t pte_create(uintptr_t ppn, int type)
{
    return (ppn << PTE_PPN_SHIFT) | PTE_V | type;
}

static inline pte_t ptd_create(uintptr_t ppn)
{
    return pte_create(ppn, PTE_V);
}

extern char bootstack[], bootstacktop[];

#endif /* !__KERN_MM_PMM_H__ */
