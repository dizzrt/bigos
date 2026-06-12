/* BigOS user libc: minimal bounded heap allocator.
 *
 * brk-based bump allocator with a simple free list. Each block carries an 8-byte
 * size header; free pushes the block onto a singly linked free list and malloc
 * reuses a first-fit free block before extending the break. Bounded and
 * deterministic: when brk cannot satisfy a request malloc returns NULL and the
 * existing blocks stay valid. No coalescing this stage. */
#include "libc.h"

#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#define HEADER_SIZE    16 /* keeps 16-byte payload alignment */

typedef struct Block {
    size_t size; /* payload size (excludes header) */
    struct Block *next_free;
} Block;

static char *g_heap_start = NULL;
static char *g_heap_end = NULL; /* current committed break */

static int heap_init(void) {
    if (g_heap_start != NULL)
        return 1;
    void *cur = brk_raw(NULL); /* query the current break */
    if ((long)cur < 0)
        return 0;
    g_heap_start = (char *)cur;
    g_heap_end = (char *)cur;
    return 1;
}

static Block *g_free_list = NULL;

static void *heap_extend(size_t bytes) {
    if (bytes > (size_t)-1 - (size_t)(unsigned long)g_heap_end)
        return NULL;
    char *old_end = g_heap_end;
    char *new_end = old_end + bytes;
    void *result = brk_raw(new_end);
    if ((long)result < 0 || (char *)result < new_end)
        return NULL;
    g_heap_end = new_end;
    return old_end;
}

void *malloc(size_t n) {
    if (n == 0)
        n = 1;
    if (!heap_init())
        return NULL;
    if (n > (size_t)-1 - 15)
        return NULL;
    size_t payload = ALIGN_UP(n, 16);
    if (payload > (size_t)-1 - HEADER_SIZE)
        return NULL;

    /* First-fit search of the free list. */
    Block **link = &g_free_list;
    while (*link != NULL) {
        Block *blk = *link;
        if (blk->size >= payload) {
            *link = blk->next_free;
            return (char *)blk + HEADER_SIZE;
        }
        link = &blk->next_free;
    }

    /* Extend the break by header + payload. */
    void *raw = heap_extend(HEADER_SIZE + payload);
    if (raw == NULL)
        return NULL;
    Block *blk = (Block *)raw;
    blk->size = payload;
    blk->next_free = NULL;
    return (char *)blk + HEADER_SIZE;
}

void free(void *p) {
    if (p == NULL)
        return;
    Block *blk = (Block *)((char *)p - HEADER_SIZE);
    blk->next_free = g_free_list;
    g_free_list = blk;
}
