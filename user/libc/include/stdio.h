/* BigOS bounded buffered FILE stream declarations.
 *
 * FILE is a libc-internal buffered stream object layered over the existing
 * fd/VFS read/write/lseek/close syscalls; all buffering lives in user space.
 * This header exposes a bounded buffered stdio subset: fopen/freopen/fclose,
 * buffered fread/fwrite, character/line helpers (fgetc/getc/fgets/fputc/putc/
 * fputs/ungetc), buffering control (setvbuf/setbuf with _IOFBF/_IOLBF/_IONBF),
 * stream state (fflush/feof/ferror/clearerr/fileno), bounded byte positioning
 * (fseek/ftell/rewind), fd-backed putchar/puts/printf/fprintf and bounded
 * snprintf with common integer/string/pointer formats.
 *
 * It intentionally omits the scanf family, wide streams, locale, floating-point
 * conversion, tmpfile/tmpnam, fmemopen/open_memstream, text/binary newline
 * translation, complete fpos_t/fgetpos/fsetpos positioning, thread-safe stream
 * locking, and full hosted FILE semantics. Text ("r") and binary ("rb") modes
 * behave identically: BigOS performs no newline translation. */
#ifndef _BIGOS_USER_STDIO_H
#define _BIGOS_USER_STDIO_H

#include <sys/types.h>

/* If an editor accidentally pre-includes a host stdio.h, keep this header
 * parseable by reusing that opaque FILE typedef. The real BigOS build sees only
 * the forward declaration below because it uses -nostdlib with this include dir. */
#if !defined(_STDIO_H_) && !defined(__STDIO_H_)
typedef struct __bigos_FILE FILE;
#endif

#ifndef EOF
#define EOF (-1)
#endif

#ifndef BUFSIZ
#define BUFSIZ 512
#endif

#define _IOFBF 0 /* fully buffered */
#define _IOLBF 1 /* line buffered */
#define _IONBF 2 /* unbuffered */

#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
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

/* Open / redirect / close. */
FILE *fopen(const char *path, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *stream);
int fclose(FILE *stream);

/* Buffered read / write. */
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fgetc(FILE *stream);
int getc(FILE *stream);
char *fgets(char *s, int n, FILE *stream);
int fputc(int c, FILE *stream);
int putc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);
int ungetc(int c, FILE *stream);

/* Buffering control. */
int setvbuf(FILE *stream, char *buf, int mode, size_t size);
void setbuf(FILE *stream, char *buf);

/* Stream state and flushing. */
int fflush(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int fileno(FILE *stream);

/* Bounded byte positioning (no text-stream translation, no fpos_t). */
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);

/* Convenience output on the standard streams and bounded formatter. */
int putchar(int c);
int puts(const char *s);
int printf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);
void perror(const char *s);

#endif /* _BIGOS_USER_STDIO_H */
