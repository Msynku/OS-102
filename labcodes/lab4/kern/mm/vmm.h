#ifndef __KERN_MM_VMM_H__
#define __KERN_MM_VMM_H__

#include <defs.h>
#include <list.h>
#include <memlayout.h>
#include <sync.h>

//pre define
struct mm_struct;

// VMA 描述一段用途和权限相同的连续虚拟地址，范围为 [vm_start, vm_end)。
struct vma_struct {
    struct mm_struct *vm_mm; // 所属地址空间
    uintptr_t vm_start;      // 起始地址，包含
    uintptr_t vm_end;        // 结束地址，不包含
    uint32_t vm_flags;       // 读、写、执行权限
    list_entry_t list_link;  // 接入按起始地址排序的 VMA 链表
};

#define le2vma(le, member)                  \
    to_struct((le), struct vma_struct, member)

#define VM_READ                 0x00000001
#define VM_WRITE                0x00000002
#define VM_EXEC                 0x00000004

// mm 描述一组 VMA 及其页表；Lab4 的内核线程没有独立的用户 mm。
struct mm_struct {
    list_entry_t mmap_list;        // 本地址空间的 VMA 链表
    struct vma_struct *mmap_cache; // 最近命中的 VMA，减少重复查找
    pde_t *pgdir;                  // 页表根指针
    int map_count;                 // VMA 数量
    void *sm_priv;                 // 页面换出管理器的私有数据
};

struct vma_struct *find_vma(struct mm_struct *mm, uintptr_t addr);
struct vma_struct *vma_create(uintptr_t vm_start, uintptr_t vm_end, uint32_t vm_flags);
void insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma);

struct mm_struct *mm_create(void);
void mm_destroy(struct mm_struct *mm);

void vmm_init(void);

int do_pgfault(struct mm_struct *mm, uint32_t error_code, uintptr_t addr);

extern volatile unsigned int pgfault_num;
extern struct mm_struct *check_mm_struct;
#endif /* !__KERN_MM_VMM_H__ */

