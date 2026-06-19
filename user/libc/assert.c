/* BigOS user libc: deterministic freestanding assertion failure path. */
#include "libc.h"

void __bigos_assert_fail(const char *expr, const char *file, int line, const char *func) {
    if (func == NULL)
        func = "?";
    fprintf(stderr, "assertion failed: %s at %s:%d in %s\n", expr != NULL ? expr : "?", file != NULL ? file : "?", line, func);
    exit(127);
}
