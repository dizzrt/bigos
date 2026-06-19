/* BigOS bounded assert declarations.
 *
 * Freestanding-safe assert macro. Enabled assertion failures print a
 * deterministic diagnostic to stderr and terminate through the BigOS user libc
 * exit path. NDEBUG disables expression evaluation. */
#ifndef _BIGOS_USER_ASSERT_H
#define _BIGOS_USER_ASSERT_H

#ifdef __cplusplus
extern "C" {
#endif

void __bigos_assert_fail(const char *expr, const char *file, int line, const char *func) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) ((expr) ? (void)0 : __bigos_assert_fail(#expr, __FILE__, __LINE__, __func__))
#endif

#endif /* _BIGOS_USER_ASSERT_H */
