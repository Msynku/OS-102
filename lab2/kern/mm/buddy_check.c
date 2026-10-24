#include <buddy_pmm.h>
#include <list.h>
#include <stdio.h>

void
buddy_check(void) {
    size_t before = nr_free_pages();

    /* 预留一个对齐的 64 页块作为测试区域。 */
    struct Page *arena = alloc_pages(64);

    assert(arena != NULL);
    assert((page2ppn(arena) & 63) == 0);

    /*
     * 暂时占用其他全部空闲页。
     * 后面的测试因此只能使用这 64 页，结果更明确。
     */
    list_entry_t held;
    list_init(&held);

    struct Page *p;
    size_t held_count = 0;

    while ((p = alloc_page()) != NULL) {
        list_add(&held, &p->page_link);
        held_count++;
    }

    assert(nr_free_pages() == 0);
    assert(held_count + 64 == before);

    free_pages(arena, 64);
    assert(nr_free_pages() == 64);

    /* 测试一：分配全部 64 个单页，检查是否重叠。 */
    uint64_t seen = 0;

    for (size_t i = 0; i < 64; i++) {
        p = alloc_page();

        assert(p != NULL && p >= arena && p < arena + 64);

        size_t offset = p - arena;

        assert((seen & ((uint64_t)1 << offset)) == 0);
        seen |= (uint64_t)1 << offset;

        assert(nr_free_pages() == 63 - i);
    }

    assert(seen == ~(uint64_t)0);
    assert(alloc_page() == NULL);

    /*
     * 测试二：只释放偶数位置。
     * 虽然空闲总数为 32，但没有连续的同阶伙伴。
     */
    for (size_t i = 0; i < 64; i += 2) {
        free_page(arena + i);
    }

    assert(nr_free_pages() == 32);
    assert(alloc_pages(2) == NULL);
    assert(nr_free_pages() == 32);

    cprintf("[Buddy] exhaustion and fragmentation: PASS\n");

    /* 测试三：倒序释放剩余页，应合并回完整的 64 页块。 */
    for (size_t i = 64; i > 0; i -= 2) {
        free_page(arena + i - 1);
    }

    assert(nr_free_pages() == 64);

    p = alloc_pages(64);
    assert(p == arena);

    free_pages(p, 64);

    cprintf("[Buddy] recursive merge: PASS\n");

    /*
     * 测试四：请求 1、2、3、5 页。
     * 实际分别占用 1、2、4、8 页。
     */
    const size_t request[4] = {1, 2, 3, 5};
    const size_t charge[4] = {1, 2, 4, 8};
    const unsigned release[4] = {2, 0, 3, 1};

    struct Page *block[4];

    size_t remaining = 64;
    seen = 0;

    for (unsigned i = 0; i < 4; i++) {
        block[i] = alloc_pages(request[i]);

        assert(block[i] != NULL);
        assert(block[i] >= arena &&
               block[i] + charge[i] <= arena + 64);
        assert((page2ppn(block[i]) & (charge[i] - 1)) == 0);

        for (size_t j = 0; j < charge[i]; j++) {
            struct Page *q = block[i] + j;

            assert(!PageReserved(q) && !PageProperty(q));
            assert(page_ref(q) == 0);

            size_t offset = q - arena;

            assert((seen & ((uint64_t)1 << offset)) == 0);
            seen |= (uint64_t)1 << offset;
        }

        remaining -= charge[i];
        assert(nr_free_pages() == remaining);
    }

    /* 乱序释放，并检查每次释放恢复的实际页数。 */
    for (unsigned i = 0; i < 4; i++) {
        unsigned k = release[i];

        free_pages(block[k], request[k]);

        remaining += charge[k];
        assert(nr_free_pages() == remaining);
    }

    assert(remaining == 64);

    cprintf("[Buddy] requests 1/2/3/5 and accounting: PASS\n");

    /* 测试五：无法满足的请求不能改变空闲状态。 */
    assert(alloc_pages(65) == NULL);
    assert(nr_free_pages() == 64);

    cprintf("[Buddy] failed allocation preserves state: PASS\n");

    /* 再次确认所有测试块已完整合并。 */
    p = alloc_pages(64);

    assert(p == arena);
    assert(nr_free_pages() == 0);

    /* 归还测试期间临时占用的其他页面。 */
    while (!list_empty(&held)) {
        list_entry_t *le = list_next(&held);

        list_del(le);
        free_page(le2page(le, page_link));
    }

    assert(nr_free_pages() == held_count);

    free_pages(p, 64);

    assert(nr_free_pages() == before);

    cprintf("[Buddy] all free pages restored: PASS\n");
    cprintf("Buddy System check passed!\n");
}