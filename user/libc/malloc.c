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
#define BIGOS_ULONG_MAX ((unsigned long)-1)

#define BIGOS_LLONG_MAX ((long long)(((unsigned long long)-1) >> 1))
#define BIGOS_LLONG_MIN (-BIGOS_LLONG_MAX - 1)
#define BIGOS_ULLONG_MAX ((unsigned long long)-1)

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

unsigned long strtoul(const char *nptr, char **endptr, int base) {
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
        if (acc > (BIGOS_ULONG_MAX - (unsigned long)digit) / (unsigned long)base) {
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
        return BIGOS_ULONG_MAX;
    }
    if (negative)
        return 0ul - acc;
    return acc;
}

int atoi(const char *nptr) {
    return (int)strtol(nptr, NULL, 10);
}

int abs(int v) {
    return v < 0 ? -v : v;
}

long labs(long v) {
    return v < 0 ? -v : v;
}

long long strtoll(const char *nptr, char **endptr, int base) {
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

    unsigned long long limit =
        negative ? (unsigned long long)BIGOS_LLONG_MAX + 1ull : (unsigned long long)BIGOS_LLONG_MAX;
    unsigned long long acc = 0;
    int any = 0;
    int overflow = 0;
    const char *last = s;
    for (;;) {
        int digit = digit_value(*s);
        if (digit < 0 || digit >= base)
            break;
        any = 1;
        last = s + 1;
        if (acc > (limit - (unsigned long long)digit) / (unsigned long long)base) {
            overflow = 1;
        } else {
            acc = acc * (unsigned long long)base + (unsigned long long)digit;
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
        return negative ? BIGOS_LLONG_MIN : BIGOS_LLONG_MAX;
    }
    if (negative) {
        if (acc == (unsigned long long)BIGOS_LLONG_MAX + 1ull)
            return BIGOS_LLONG_MIN;
        return -(long long)acc;
    }
    return (long long)acc;
}

unsigned long long strtoull(const char *nptr, char **endptr, int base) {
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

    unsigned long long acc = 0;
    int any = 0;
    int overflow = 0;
    const char *last = s;
    for (;;) {
        int digit = digit_value(*s);
        if (digit < 0 || digit >= base)
            break;
        any = 1;
        last = s + 1;
        if (acc > (BIGOS_ULLONG_MAX - (unsigned long long)digit) / (unsigned long long)base) {
            overflow = 1;
        } else {
            acc = acc * (unsigned long long)base + (unsigned long long)digit;
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
        return BIGOS_ULLONG_MAX;
    }
    if (negative)
        return 0ull - acc;
    return acc;
}

static void qsort_swap(char *a, char *b, size_t size) {
    for (size_t i = 0; i < size; i++) {
        char tmp = a[i];
        a[i] = b[i];
        b[i] = tmp;
    }
}

/* Bounded insertion sort: deterministic, no recursion, no extra allocation. */
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    if (base == NULL || compar == NULL || size == 0 || nmemb < 2)
        return;
    char *arr = (char *)base;
    for (size_t i = 1; i < nmemb; i++) {
        for (size_t j = i; j > 0; j--) {
            char *cur = arr + j * size;
            char *prev = arr + (j - 1) * size;
            if (compar(prev, cur) <= 0)
                break;
            qsort_swap(prev, cur, size);
        }
    }
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *)) {
    if (key == NULL || base == NULL || compar == NULL || size == 0)
        return NULL;
    const char *arr = (const char *)base;
    size_t lo = 0;
    size_t hi = nmemb;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const char *elem = arr + mid * size;
        int cmp = compar(key, elem);
        if (cmp == 0)
            return (void *)elem;
        if (cmp < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return NULL;
}

void free(void *p) {
    if (p == NULL)
        return;
    Block *blk = (Block *)((char *)p - HEADER_SIZE);
    blk->next_free = g_free_list;
    g_free_list = blk;
}
