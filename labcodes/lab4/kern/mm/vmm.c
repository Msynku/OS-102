#include <vmm.h>
#include <sync.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <error.h>
#include <pmm.h>
#include <riscv.h>
#include <kmalloc.h>

/* mm 描述一个地址空间，VMA 描述其中一段连续地址和权限。
 * 当前实现用按起始地址排序的链表管理 VMA，并缓存最近一次命中的区域。 */

// szx func : print_vma and print_mm
void print_vma(char *name, struct vma_struct *vma)
{
    cprintf("-- %s print_vma --\n", name);
    cprintf("   mm_struct: %p\n", vma->vm_mm);
    cprintf("   vm_start,vm_end: %x,%x\n", vma->vm_start, vma->vm_end);
    cprintf("   vm_flags: %x\n", vma->vm_flags);
    cprintf("   list_entry_t: %p\n", &vma->list_link);
}

void print_mm(char *name, struct mm_struct *mm)
{
    cprintf("-- %s print_mm --\n", name);
    cprintf("   mmap_list: %p\n", &mm->mmap_list);
    cprintf("   map_count: %d\n", mm->map_count);
    list_entry_t *list = &mm->mmap_list;
    for (int i = 0; i < mm->map_count; i++)
    {
        list = list_next(list);
        print_vma(name, le2vma(list, list_link));
    }
}

static void check_vmm(void);
static void check_vma_struct(void);
static void check_pgfault(void);

// 创建一个地址空间描述符 mm；分配失败返回 NULL，成功时返回新 mm。
struct mm_struct *
mm_create(void)
{
    struct mm_struct *mm = kmalloc(sizeof(struct mm_struct)); // 为地址空间描述符分配内存

    if (mm != NULL)
    {
        list_init(&(mm->mmap_list)); // 初始化空的 VMA 链表
        mm->mmap_cache = NULL;      // 还没有最近命中的 VMA
        mm->pgdir = NULL;           // 还没有绑定页表
        mm->map_count = 0;          // 目前没有虚拟地址区域
        mm->sm_priv = NULL;         // 还没有页面换出管理器的私有数据
    }
    return mm;
}

// 创建一个虚拟地址区域：start 包含、end 不包含，flags 指定读写执行权限。
// 这里只创建描述符，之后还需要 insert_vma_struct 才会加入地址空间。
struct vma_struct *
vma_create(uintptr_t vm_start, uintptr_t vm_end, uint32_t vm_flags)
{
    struct vma_struct *vma = kmalloc(sizeof(struct vma_struct)); // vma 描述一段虚拟地址

    if (vma != NULL)
    {
        vma->vm_start = vm_start;
        vma->vm_end = vm_end;
        vma->vm_flags = vm_flags;
    }
    return vma;
}

// mm 是要搜索的地址空间，addr 是虚拟地址；返回包含该地址的 VMA，找不到返回 NULL。
// VMA 范围左闭右开；先查最近命中的缓存，缓存未命中再遍历链表。
struct vma_struct *
find_vma(struct mm_struct *mm, uintptr_t addr)
{
    struct vma_struct *vma = NULL; // 记录找到的地址区域，初始表示没找到
    if (mm != NULL)
    {
        vma = mm->mmap_cache;
        if (!(vma != NULL && vma->vm_start <= addr && vma->vm_end > addr))
        {
            bool found = 0; // 标记链表中是否找到包含 addr 的 VMA
            list_entry_t *list = &(mm->mmap_list), *le = list; // list 是表头，le 是遍历位置
            while ((le = list_next(le)) != list)
            {
                vma = le2vma(le, list_link); // 从链表节点找到所属的 VMA
                if (vma->vm_start <= addr && addr < vma->vm_end)
                {
                    found = 1;
                    break;
                }
            }
            if (!found)
            {
                vma = NULL;
            }
        }
        if (vma != NULL)
        {
            mm->mmap_cache = vma; // 记住本次结果，方便下次查找
        }
    }
    return vma;
}

// check_vma_overlap - check if vma1 overlaps vma2 ?
static inline void
check_vma_overlap(struct vma_struct *prev, struct vma_struct *next)
{
    assert(prev->vm_start < prev->vm_end);
    assert(prev->vm_end <= next->vm_start);
    assert(next->vm_start < next->vm_end);
}

// 将 vma 插入地址空间 mm 的 VMA 链表；插入前检查区间有效且不与相邻区域重叠。
void insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma)
{
    assert(vma->vm_start < vma->vm_end);
    list_entry_t *list = &(mm->mmap_list); // 表头，不代表具体的 VMA
    list_entry_t *le_prev = list, *le_next; // 插入位置前后的链表节点

    // 找到按起始地址排序的插入位置，再检查左右相邻区域不重叠。
    list_entry_t *le = list; // le 是当前正在检查的节点
    while ((le = list_next(le)) != list)
    {
        struct vma_struct *mmap_prev = le2vma(le, list_link); // 该节点对应的已有区域
        if (mmap_prev->vm_start > vma->vm_start)
        {
            break;
        }
        le_prev = le;
    }

    le_next = list_next(le_prev);

    /* check overlap */
    if (le_prev != list)
    {
        check_vma_overlap(le2vma(le_prev, list_link), vma);
    }
    if (le_next != list)
    {
        check_vma_overlap(vma, le2vma(le_next, list_link));
    }

    vma->vm_mm = mm; // 记录该区域所属的地址空间
    list_add_after(le_prev, &(vma->list_link)); // 插入排序后的位置

    mm->map_count++; // 地址空间内的 VMA 数量增加
}

// mm_destroy - free mm and mm internal fields
void mm_destroy(struct mm_struct *mm)
{

    list_entry_t *list = &(mm->mmap_list), *le;
    while ((le = list_next(list)) != list)
    {
        list_del(le);
        kfree(le2vma(le, list_link)); // kfree vma
    }
    kfree(mm); // kfree mm
    mm = NULL;
}

// vmm_init - initialize virtual memory management
//          - now just call check_vmm to check correctness of vmm
void vmm_init(void)
{
    check_vmm();
}

// check_vmm - check correctness of vmm
static void
check_vmm(void)
{
    check_vma_struct();
    // 这里没有执行缺页异常测试，不能把本次通过等同于缺页处理已验证。
    // check_pgfault();

    cprintf("check_vmm() succeeded.\n");
}

static void
check_vma_struct(void)
{
    struct mm_struct *mm = mm_create();
    assert(mm != NULL);

    int step1 = 10, step2 = step1 * 10;

    int i;
    for (i = step1; i >= 1; i--)
    {
        struct vma_struct *vma = vma_create(i * 5, i * 5 + 2, 0);
        assert(vma != NULL);
        insert_vma_struct(mm, vma);
    }

    for (i = step1 + 1; i <= step2; i++)
    {
        struct vma_struct *vma = vma_create(i * 5, i * 5 + 2, 0);
        assert(vma != NULL);
        insert_vma_struct(mm, vma);
    }

    list_entry_t *le = list_next(&(mm->mmap_list));

    for (i = 1; i <= step2; i++)
    {
        assert(le != &(mm->mmap_list));
        struct vma_struct *mmap = le2vma(le, list_link);
        assert(mmap->vm_start == i * 5 && mmap->vm_end == i * 5 + 2);
        le = list_next(le);
    }

    for (i = 5; i <= 5 * step2; i += 5)
    {
        struct vma_struct *vma1 = find_vma(mm, i);
        assert(vma1 != NULL);
        struct vma_struct *vma2 = find_vma(mm, i + 1);
        assert(vma2 != NULL);
        struct vma_struct *vma3 = find_vma(mm, i + 2);
        assert(vma3 == NULL);
        struct vma_struct *vma4 = find_vma(mm, i + 3);
        assert(vma4 == NULL);
        struct vma_struct *vma5 = find_vma(mm, i + 4);
        assert(vma5 == NULL);

        assert(vma1->vm_start == i && vma1->vm_end == i + 2);
        assert(vma2->vm_start == i && vma2->vm_end == i + 2);
    }

    for (i = 4; i >= 0; i--)
    {
        struct vma_struct *vma_below_5 = find_vma(mm, i);
        if (vma_below_5 != NULL)
        {
            cprintf("vma_below_5: i %x, start %x, end %x\n", i, vma_below_5->vm_start, vma_below_5->vm_end);
        }
        assert(vma_below_5 == NULL);
    }

    mm_destroy(mm);

    cprintf("check_vma_struct() succeeded!\n");
}
