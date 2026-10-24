#include <buddy_pmm.h>
#include <list.h>

/* 最大块为 2^15 页，对应当前框架的 128 MiB 上限。 */
#define MAX_ORDER 15

/* flags 的第 2 位，用于标记已分配块的首个描述符。 */
#define ALLOC_HEAD (1UL << 2)

static list_entry_t free_lists[MAX_ORDER + 1];
static size_t nr_free;

/* 求能容纳 n 页的最小阶数。 */
static unsigned
request_order(size_t n) {
    unsigned order = 0;

    while (((size_t)1 << order) < n) {
        if (order == MAX_ORDER) {
            return MAX_ORDER + 1;
        }
        order++;
    }

    return order;
}

/* 放回空闲块，并递归合并同阶的空闲伙伴。 */
static void
put_block(struct Page *base, unsigned order) {
    size_t index = base - pages;
    size_t total = npage - nbase;

    while (order < MAX_ORDER) {
        size_t count = (size_t)1 << order;

        /* 伙伴位置：翻转当前阶对应的地址位。 */
        size_t other = index ^ count;

        if (other >= total || count > total - other) {
            break;
        }

        struct Page *buddy = pages + other;

        if (PageReserved(buddy) ||
            !PageProperty(buddy) ||
            buddy->property != count) {
            break;
        }

        list_del(&buddy->page_link);
        ClearPageProperty(buddy);
        buddy->property = 0;

        if (other < index) {
            index = other;
        }

        order++;
    }

    base = pages + index;
    base->property = (size_t)1 << order;
    SetPageProperty(base);

    list_add(&free_lists[order], &base->page_link);
}

/* 初始化各阶空闲链表。 */
static void
buddy_init(void) {
    for (unsigned i = 0; i <= MAX_ORDER; i++) {
        list_init(&free_lists[i]);
    }

    nr_free = 0;
}

/* 将任意起点和长度的空闲区拆成对齐的伙伴块。 */
static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);

    size_t index = base - pages;

    assert(index < npage - nbase);
    assert(n <= npage - nbase - index);

    for (struct Page *p = base; p != base + n; p++) {
        assert(PageReserved(p));

        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }

    nr_free += n;

    while (n != 0) {
        unsigned order = MAX_ORDER;

        /*
         * 同时满足：
         * 1. 块不能超过剩余长度；
         * 2. 块起点按块大小对齐。
         */
        while (((size_t)1 << order) > n ||
               (index & (((size_t)1 << order) - 1)) != 0) {
            order--;
        }

        size_t count = (size_t)1 << order;

        put_block(pages + index, order);

        index += count;
        n -= count;
    }
}

/* 分配至少 n 页，实际大小向上取整到 2 的幂。 */
static struct Page *
buddy_alloc_pages(size_t n) {
    assert(n > 0);

    unsigned order = request_order(n);

    if (order > MAX_ORDER) {
        return NULL;
    }

    size_t count = (size_t)1 << order;

    if (count > nr_free) {
        return NULL;
    }

    /* 找到当前阶或更高阶的可用块。 */
    unsigned found = order;

    while (found <= MAX_ORDER &&
           list_empty(&free_lists[found])) {
        found++;
    }

    if (found > MAX_ORDER) {
        return NULL;
    }

    struct Page *base =
        le2page(list_next(&free_lists[found]), page_link);

    list_del(&base->page_link);
    ClearPageProperty(base);
    base->property = 0;

    /* 不断对半拆分，保留左半块，归还右半块。 */
    while (found > order) {
        found--;

        struct Page *right = base + ((size_t)1 << found);

        right->property = (size_t)1 << found;
        SetPageProperty(right);

        list_add(&free_lists[found], &right->page_link);
    }

    for (struct Page *p = base; p != base + count; p++) {
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }

    /* 记录实际分配大小，供释放时检查。 */
    base->flags |= ALLOC_HEAD;
    base->property = count;

    nr_free -= count;

    return base;
}

/* 释放原始分配的整块内存，不支持部分释放。 */
static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);

    unsigned order = request_order(n);
    assert(order <= MAX_ORDER);

    size_t count = (size_t)1 << order;
    size_t index = base - pages;

    assert(index < npage - nbase);
    assert(count <= npage - nbase - index);
    assert((index & (count - 1)) == 0);

    /* 检查首地址与实际块大小，防止重复或错误释放。 */
    assert((base->flags & ALLOC_HEAD) != 0);
    assert(base->property == count);

    for (struct Page *p = base; p != base + count; p++) {
        assert(!PageReserved(p) && !PageProperty(p));

        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }

    nr_free += count;
    put_block(base, order);
}

/* 返回空闲页总数。 */
static size_t
buddy_nr_free_pages(void) {
    return nr_free;
}

/* 全局管理器注册，必须放在函数外面。 */
const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};