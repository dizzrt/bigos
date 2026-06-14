/* BigOS minimal stdio declarations.
 *
 * Standard streams are opaque handles for fd 0/1/2 only. This header exposes
 * fd-backed putchar/puts/printf/fprintf with %s, %d, %x, %c, and %% support. It
 * intentionally omits fopen/fclose, buffering, locale, floating point formats,
 * wide characters, and full hosted FILE semantics. */
#ifndef _BIGOS_USER_STDIO_H
#define _BIGOS_USER_STDIO_H

/* If an editor accidentally pre-includes a host stdio.h, keep this header
 * parseable by reusing that opaque FILE typedef. The real BigOS build sees only
 * the forward declaration below because it uses -nostdlib with this include dir. */
#if !defined(_STDIO_H_) && !defined(__STDIO_H_)
typedef struct __bigos_FILE FILE;
#endif

#ifdef stdin
#undef stdin
#endif
#ifdef stdout
#undef stdout
#endif
#ifdef stderr
#undef stderr
#endif

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int putchar(int c);
int puts(const char *s);
int printf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
void perror(const char *s);

#endif /* _BIGOS_USER_STDIO_H */
