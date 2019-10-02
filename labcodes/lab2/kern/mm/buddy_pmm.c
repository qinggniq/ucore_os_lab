#include <pmm.h>
#include <list.h>
#include <string.h>
#include <buddy_pmm.h>
#include <stdio.h>

struct buddy {
    size_t size;
    size_t longest[1];
};

struct buddy*  buddy_manager;
size_t manage_size; //size of manage_size; manage_size = page_num * 2 * sizeof(size_t)
size_t manage_page_cnt; //count of pages storing the buddy_manager; manage_pages_cnt = (manage_size-1) / 4096 + 1;
struct Page* manage_base; //manage_base = base; (alloc_base - managed) * PGSIZE store the  buddy manager。
struct Page* alloc_page_base;    //the base of  alloc pages

//index from 0
#define BUDDY_ROOT_SIZE (buddy_manager->longest[0])
#define LEFT(idx) ((idx << 1) + 1)
#define RIGHT(idx) (((idx) << 1) + 2)
#define PARENT(idx) (((idx) + 1) / 2 - 1)
#define IS_POWER_OF_2(x) (!((x) & ((x) - 1)))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
// Bitwise operate
#define UINT32_SHR_OR(a,n)      ((a)|((a)>>(n)))   
#define UINT32_MASK(a)          (UINT32_SHR_OR(UINT32_SHR_OR(UINT32_SHR_OR(UINT32_SHR_OR(UINT32_SHR_OR(a,1),2),4),8),16))    
#define UINT32_REMAINDER(a)     ((a)&(UINT32_MASK(a)>>1))
#define UINT32_ROUND_UP(a)      (UINT32_REMAINDER(a)?(((a)-UINT32_REMAINDER(a))<<1):(a))
#define UINT32_ROUND_DOWN(a)    (UINT32_REMAINDER(a)?((a)-UINT32_REMAINDER(a)):(a))



static void
buddy_init(void) {

}

static void
init_buddy_manager(struct Page* base, size_t n) {
    buddy_manager = page2kva(base);
    manage_base = base;
    
    buddy_manager->size = UINT32_ROUND_DOWN(n);
    manage_size = buddy_manager->size * 2 * sizeof(size_t); 
    manage_page_cnt = (manage_size - 1) / PGSIZE + 1; 
    alloc_page_base = base + manage_page_cnt;

    size_t node_size = buddy_manager->size * 2; 
    for (int i = 0; i < 2 * buddy_manager->size - 1; ++i) {
        if (IS_POWER_OF_2(i + 1))
            node_size >>= 1;
        buddy_manager->longest[i] = node_size;
    }
}

static void
buddy_init_memmap(struct Page* base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(PageReserved(p));
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }

    init_buddy_manager(base, n);
}

static struct Page *
buddy_alloc_pages(size_t n) {
    assert(n > 0);
    n = UINT32_ROUND_UP(n); // fix it to the pow of 2
    if (n > BUDDY_ROOT_SIZE) {  //no space
        return NULL;
    }
    
    struct Page *page = NULL;

    size_t idx = 0;
    size_t node_size;
    size_t offset = 0;
    size_t left_longest;
    size_t right_longest;
    for (node_size = buddy_manager->size; node_size != n; node_size >>= 1) {
        left_longest = buddy_manager->longest[LEFT(idx)] ;
        right_longest = buddy_manager->longest[RIGHT(idx)];
        //find the suitable node but not split the large block
        if (left_longest >= n && right_longest >= n) { 
            if (left_longest <= right_longest) {
                idx = LEFT(idx);
            }else{
                idx = RIGHT(idx);
            }
            continue;
        }else{
            if (left_longest >= n) {
                idx = LEFT(idx);
            }else{
                idx = RIGHT(idx);
            }
        }
    }
    buddy_manager->longest[idx] = 0;
    offset = (idx + 1) * node_size - buddy_manager->size; //to calculate the offset of the alloc_base

    while (idx) {
        idx = PARENT(idx);
        buddy_manager->longest[idx] = MAX(buddy_manager->longest[LEFT(idx)], buddy_manager->longest[RIGHT(idx)]);
    }
    return alloc_page_base + offset;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    n = UINT32_ROUND_UP(n);
    
    size_t offset = base - alloc_page_base;
    cprintf("free %08p --- %d\n", base, offset);
    assert(offset < buddy_manager->size); // ????
    
    size_t  idx = offset + buddy_manager->size - 1;

    size_t node_size = 1;
    //find the block which page belong
    for (; buddy_manager->longest[idx]; idx = PARENT(idx)) {
        node_size <<= 1;
        if (idx == 0) {
            return;
        }
    }
    size_t left_longest;
    size_t right_longest;
    buddy_manager->longest[idx] = node_size;
    while (idx) {
        idx = PARENT(idx);
        node_size <<= 1;

        left_longest = buddy_manager->longest[LEFT(idx)];
        right_longest = buddy_manager->longest[RIGHT(idx)];
        if (left_longest + right_longest == node_size) {
            cprintf("free %d %d\n", idx, node_size);
            buddy_manager->longest[idx] = node_size;
        }else{
             buddy_manager->longest[idx] = MAX(left_longest, right_longest);
        }
    }
}

static size_t
buddy_nr_free_pages(void) {
    return buddy_manager->size;
}

static void
macro_check(void) {

    // Bitwise operate check
    assert(UINT32_SHR_OR(0xCC, 2) == 0xFF);
    assert(UINT32_MASK(0x4000) == 0x7FFF);
    assert(UINT32_REMAINDER(0x4321) == 0x321);
    assert(UINT32_ROUND_UP(0x2321) == 0x4000);
    assert(UINT32_ROUND_UP(0x2000) == 0x2000);
    assert(UINT32_ROUND_DOWN(0x4321) == 0x4000);
    assert(UINT32_ROUND_DOWN(0x4000) == 0x4000);

    assert(PARENT(1) == 0);
    assert(PARENT(2) == 0);
    assert(PARENT(3) == 1);
    assert(PARENT(4) == 1);
    assert(PARENT(5) == 2);
}

static void
size_check(void) {

    size_t buddy_alloc_size = buddy_manager->size;

    init_buddy_manager(manage_base, 1024);
    assert(buddy_manager->size == 1024);
    init_buddy_manager(manage_base, 1026);
    assert(buddy_manager->size == 1024);
    init_buddy_manager(manage_base, 1028);    
    assert(buddy_manager->size == 1024);
    init_buddy_manager(manage_base, buddy_alloc_size);   

}

static void
alloc_check(void) {

    // Build buddy system for test
    size_t buddy_alloc_size = buddy_manager->size;
    for (struct Page *p = manage_base; p < manage_base + 1026; p++)
        SetPageReserved(p);
    buddy_init();
    buddy_init_memmap(manage_base, 1026);

    // Check allocation
    struct Page *p0, *p1, *p2, *p3;
    p0 = p1 = p2 = NULL;
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);
    assert((p3 = alloc_page()) != NULL);
    cprintf("p0 - base = %d, p1 - base = %d. p2 - base = %d, p3 - base = %d\n", \
    p0 - alloc_page_base, p1 - alloc_page_base, p2 - alloc_page_base, p3 - alloc_page_base);
    assert(p0 + 1 == p1);
    assert(p1 + 1 == p2);
    assert(p2 + 1 == p3);
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0 && page_ref(p3) == 0);

    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);
    assert(page2pa(p3) < npage * PGSIZE);


    // Check release
    free_page(p0);
    free_page(p1);
    free_page(p2);
    cprintf("p3 - base = %d\n",  p3 - alloc_page_base);
    assert((p1 = alloc_page()) != NULL);
    assert((p0 = alloc_pages(2)) != NULL);
    cprintf("p0 - base = %d, p1 - base = %d\n", p0 - alloc_page_base, p1 - alloc_page_base);
    assert(p0 + 2 == p1);

    free_pages(p0, 2);
    free_page(p1);
    free_page(p3);

    struct Page *p;
    assert((p = alloc_pages(4)) == p0);


    // Restore buddy system
    for (struct Page *p = manage_base; p < manage_base + buddy_alloc_size; p++)
        SetPageReserved(p);
    buddy_init();
    buddy_init_memmap(manage_base, buddy_alloc_size);

}
static void 
buddy_check(void) {
    macro_check();
    size_check();
    alloc_check();
}
const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};