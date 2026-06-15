/* BigOS user libc: minimal bounded heap allocator.
 *
 * brk-based bump allocator with a simple free list. Each block carries an 8-byte
 * size header; free pushes the block onto a singly linked free list and malloc
 * reuses a first-fit free block before extending the break. calloc zeroes the
 * allocated block after overflow checking; realloc preserves the old allocation
 * on failure. No coalescing this stage. */
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

#define BIGOS_LONG_MAX ((long)(((unsigned long)-1) >> 1))
#define BIGOS_LONG_MIN (-BIGOS_LONG_MAX - 1)

static int is_space_char(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int digit_value(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;
    return -1;
}

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

void *calloc(size_t nmemb, size_t size) {
    if (size != 0 && nmemb > (size_t)-1 / size)
        return NULL;
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr == NULL)
        return NULL;
    memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (ptr == NULL)
        return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    Block *blk = (Block *)((char *)ptr - HEADER_SIZE);
    if (blk->size >= size)
        return ptr;
    void *new_ptr = malloc(size);
    if (new_ptr == NULL)
        return NULL;
    memcpy(new_ptr, ptr, blk->size);
    free(ptr);
    return new_ptr;
}

long strtol(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    if (s == NULL) {
        if (endptr != NULL)
            *endptr = (char *)nptr;
        errno = EINVAL;
        return 0;
    }
    if (base != 0 && (base < 2 || base > 36)) {
        if (endptr != NULL)
            *endptr = (char *)nptr;
        errno = EINVAL;
        return 0;
    }
    while (is_space_char(*s))
        s++;
    int negative = 0;
    if (*s == '+' || *s == '-') {
        negative = *s == '-';
        s++;
    }
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    } else if (base == 0) {
        base = *s == '0' ? 8 : 10;
    }

    unsigned long limit = negative ? (unsigned long)BIGOS_LONG_MAX + 1ul : (unsigned long)BIGOS_LONG_MAX;
    unsigned long acc = 0;
    int any = 0;
    int overflow = 0;
    const char *last = s;
    for (;;) {
        int digit = digit_value(*s);
        if (digit < 0 || digit >= base)
            break;
        any = 1;
        last = s + 1;
        if (acc > (limit - (unsigned long)digit) / (unsigned long)base) {
            overflow = 1;
        } else {
            acc = acc * (unsigned long)base + (unsigned long)digit;
        }
        s++;
    }
    if (!any) {
        if (endptr != NULL)
            *endptr = (char *)nptr;
        return 0;
    }
    if (endptr != NULL)
        *endptr = (char *)last;
    if (overflow) {
        errno = ERANGE;
        return negative ? BIGOS_LONG_MIN : BIGOS_LONG_MAX;
    }
    if (negative) {
        if (acc == (unsigned long)BIGOS_LONG_MAX + 1ul)
            return BIGOS_LONG_MIN;
        return -(long)acc;
    }
    return (long)acc;
}

int atoi(const char *nptr) {
    return (int)strtol(nptr, NULL, 10);
}

void free(void *p) {
    if (p == NULL)
        return;
    Block *blk = (Block *)((char *)p - HEADER_SIZE);
    blk->next_free = g_free_list;
    g_free_list = blk;
}
