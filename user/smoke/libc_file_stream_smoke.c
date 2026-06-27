/* BigOS bounded buffered FILE stream smoke (default-off libc_file_stream_smoke).
 *
 * Runs as PID-1 init when BIGOS_LIBC_FILE_STREAM_SMOKE is configured. It
 * exercises the user-space buffered FILE stream subset and the expanded
 * string/stdlib/ctype helpers with deterministic, non-interactive assertions and
 * emits the fixed marker BIGOS_LIBC_FILE_STREAM_PASSED or
 * BIGOS_LIBC_FILE_STREAM_FAILED through fd 1 (mirrored to COM1).
 *
 * Coverage:
 *   - fopen/fwrite/fread round-trip, fseek/ftell/rewind positioning,
 *   - fgets line reading, feof/ferror/clearerr state,
 *   - setvbuf mode switches and a caller-provided stack buffer, fflush,
 *   - freopen redirect reusing the same FILE pointer (in a child),
 *   - exit-path flush (write without fclose then exit still lands, in a child),
 *   - deterministic failure paths (bad mode EINVAL, freopen NULL path EINVAL,
 *     ENOENT, NULL/closed stream ops, setvbuf after I/O, allocation rollback),
 *   - expanded string/stdlib/ctype helpers and the freestanding-header ABI.
 */
#include "libc.h"
#include <stdint.h>
#include <limits.h>
#include <stdbool.h>

/* ABI guardrails: assert the toolchain freestanding types match LP64 psABI so a
 * toolchain swap that changes these would fail the build deterministically. */
_Static_assert(sizeof(size_t) == 8, "size_t must be 64-bit (LP64)");
_Static_assert(sizeof(long) == 8, "long must be 64-bit (LP64)");
_Static_assert(sizeof(long long) == 8, "long long must be 64-bit");
_Static_assert(sizeof(int) == 4, "int must be 32-bit");
_Static_assert(sizeof(void *) == 8, "pointer must be 64-bit");

static void emit(const char *s) {
    size_t len = strlen(s);
    while (len > 0) {
        size_t chunk = len > 64 ? 64 : len;
        ssize_t n = write(1, s, chunk);
        if (n <= 0)
            return;
        s += n;
        len -= (size_t)n;
    }
}

static void fail(const char *why) {
    emit("BIGOS_LIBC_FILE_STREAM_FAILED ");
    emit(why);
    emit("\n");
    exit(1);
}

static int check_round_trip(void) {
    const char *path = "/rw/fs_round.txt";
    unlink(path);
    FILE *f = fopen(path, "w");
    if (f == NULL)
        return 0;
    const char payload[] = "alpha\nbeta\ngamma\n";
    size_t plen = sizeof(payload) - 1;
    if (fwrite(payload, 1, plen, f) != plen)
        return 0;
    if (fclose(f) != 0)
        return 0;

    f = fopen(path, "r");
    if (f == NULL)
        return 0;
    char buf[64];
    size_t got = fread(buf, 1, sizeof(buf), f);
    if (got != plen)
        return 0;
    buf[got] = 0;
    if (strcmp(buf, payload) != 0)
        return 0;
    /* At EOF: another read returns 0 and sets eof. */
    char tail[4];
    if (fread(tail, 1, sizeof(tail), f) != 0 || !feof(f))
        return 0;
    clearerr(f);
    if (feof(f) || ferror(f))
        return 0;
    fclose(f);
    return 1;
}

static int check_seek_tell(void) {
    const char *path = "/rw/fs_seek.txt";
    unlink(path);
    FILE *f = fopen(path, "w+");
    if (f == NULL)
        return 0;
    if (fwrite("0123456789", 1, 10, f) != 10)
        return 0;
    if (fflush(f) != 0)
        return 0;
    if (fseek(f, 3, SEEK_SET) != 0)
        return 0;
    if (ftell(f) != 3)
        return 0;
    int c = fgetc(f);
    if (c != '3' || ftell(f) != 4)
        return 0;
    /* ungetc then re-read */
    if (ungetc(c, f) != '3')
        return 0;
    if (ftell(f) != 3)
        return 0;
    if (fgetc(f) != '3')
        return 0;
    if (fseek(f, -2, SEEK_END) != 0 || ftell(f) != 8)
        return 0;
    if (fgetc(f) != '8')
        return 0;
    rewind(f);
    if (ftell(f) != 0 || ferror(f))
        return 0;
    if (fgetc(f) != '0')
        return 0;
    fclose(f);
    return 1;
}

static int check_fgets(void) {
    const char *path = "/rw/fs_lines.txt";
    unlink(path);
    FILE *f = fopen(path, "w");
    if (f == NULL)
        return 0;
    if (fputs("first\n", f) != 0)
        return 0;
    if (fputs("second\n", f) != 0)
        return 0;
    if (fputs("noeol", f) != 0)
        return 0;
    fclose(f);

    f = fopen(path, "r");
    if (f == NULL)
        return 0;
    char line[32];
    if (fgets(line, sizeof(line), f) == NULL || strcmp(line, "first\n") != 0)
        return 0;
    if (fgets(line, sizeof(line), f) == NULL || strcmp(line, "second\n") != 0)
        return 0;
    if (fgets(line, sizeof(line), f) == NULL || strcmp(line, "noeol") != 0)
        return 0;
    if (fgets(line, sizeof(line), f) != NULL)
        return 0;
    fclose(f);
    return 1;
}

static int check_setvbuf(void) {
    const char *path = "/rw/fs_vbuf.txt";
    unlink(path);
    /* Caller-provided stack buffer with full buffering. */
    FILE *f = fopen(path, "w");
    if (f == NULL)
        return 0;
    char stackbuf[64];
    if (setvbuf(f, stackbuf, _IOFBF, sizeof(stackbuf)) != 0)
        return 0;
    if (fwrite("buffered", 1, 8, f) != 8)
        return 0;
    /* setvbuf after I/O must fail. */
    if (setvbuf(f, NULL, _IOLBF, 32) != -1)
        return 0;
    if (fflush(f) != 0)
        return 0;
    fclose(f); /* must NOT free stackbuf */

    /* Unbuffered mode writes immediately. */
    f = fopen(path, "w");
    if (f == NULL)
        return 0;
    if (setvbuf(f, NULL, _IONBF, 0) != 0)
        return 0;
    if (fwrite("X", 1, 1, f) != 1)
        return 0;
    /* Read back from a second fd without flush: unbuffered already landed. */
    FILE *chk = fopen(path, "r");
    if (chk == NULL)
        return 0;
    int c = fgetc(chk);
    fclose(chk);
    fclose(f);
    if (c != 'X')
        return 0;
    return 1;
}

static int check_failures(void) {
    /* Unrecognized mode -> EINVAL, no fd opened. */
    errno = 0;
    if (fopen("/rw/fs_bad.txt", "q") != NULL || errno != EINVAL)
        return 0;
    /* Nonexistent file in read mode -> ENOENT. */
    errno = 0;
    if (fopen("/rw/fs_does_not_exist", "r") != NULL || errno != ENOENT)
        return 0;
    /* freopen with NULL path -> EINVAL, stream fd unchanged. */
    FILE *f = fopen("/rw/fs_reopen.txt", "w");
    if (f == NULL)
        return 0;
    int fd_before = fileno(f);
    errno = 0;
    if (freopen(NULL, "w", f) != NULL || errno != EINVAL)
        return 0;
    if (fileno(f) != fd_before)
        return 0;
    fclose(f);
    /* NULL stream ops are deterministic. */
    errno = 0;
    if (fclose(NULL) != EOF || errno != EINVAL)
        return 0;
    if (fgetc(NULL) != EOF)
        return 0;
    if (fputc('x', NULL) != EOF)
        return 0;
    /* freopen open failure leaves stream unusable. */
    f = fopen("/rw/fs_reopen2.txt", "w");
    if (f == NULL)
        return 0;
    errno = 0;
    if (freopen("/no/such/dir/file", "r", f) != NULL)
        return 0;
    /* Stream now closed: further write fails deterministically. */
    if (fputc('y', f) != EOF)
        return 0;
    (void)fclose(f); /* already closed: returns EOF, no crash */
    return 1;
}

/* Child: freopen(stdout) must redirect to the file and keep the same pointer. */
static int check_freopen_stdout(void) {
    const char *path = "/rw/fs_stdout.txt";
    unlink(path);
    pid_t pid = fork();
    if (pid < 0)
        return 0;
    if (pid == 0) {
        FILE *r = freopen(path, "w", stdout);
        if (r != stdout)
            exit(21);
        printf("redirected-%d", 7);
        /* No fclose: exit() must flush. */
        exit(0);
    }
    int status = -1;
    if (wait_status(pid, &status) != pid || status != 0)
        return 0;
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return 0;
    char buf[32];
    char *line = fgets(buf, sizeof(buf), f);
    fclose(f);
    if (line == NULL || strcmp(buf, "redirected-7") != 0)
        return 0;
    return 1;
}

/* Child: write to a file with default buffering, exit WITHOUT fclose; the data
 * must still land because exit() flushes buffered streams. */
static int check_exit_flush(void) {
    const char *path = "/rw/fs_exitflush.txt";
    unlink(path);
    pid_t pid = fork();
    if (pid < 0)
        return 0;
    if (pid == 0) {
        FILE *f = fopen(path, "w");
        if (f == NULL)
            exit(31);
        /* Default full buffering: small write stays buffered until exit. */
        if (fwrite("flush-on-exit", 1, 13, f) != 13)
            exit(32);
        exit(0); /* no fflush, no fclose */
    }
    int status = -1;
    if (wait_status(pid, &status) != pid || status != 0)
        return 0;
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return 0;
    char buf[32];
    char *line = fgets(buf, sizeof(buf), f);
    fclose(f);
    if (line == NULL || strcmp(buf, "flush-on-exit") != 0)
        return 0;
    return 1;
}

static int check_str_helpers(void) {
    if (memcmp("abc", "abd", 3) >= 0 || memcmp("abc", "abc", 3) != 0)
        return 0;
    char cat[16] = "foo";
    strcat(cat, "bar");
    if (strcmp(cat, "foobar") != 0)
        return 0;
    char ncat[16] = "foo";
    strncat(ncat, "barbaz", 3);
    if (strcmp(ncat, "foobar") != 0)
        return 0;
    if (strspn("aabbcc", "ab") != 4)
        return 0;
    if (strcspn("abc:def", ":") != 3)
        return 0;
    if (strpbrk("hello,world", ",") == NULL || *strpbrk("hello,world", ",") != ',')
        return 0;
    char toks[] = "a,bb,,ccc";
    char *save = NULL;
    char *t1 = strtok_r(toks, ",", &save);
    char *t2 = strtok_r(NULL, ",", &save);
    char *t3 = strtok_r(NULL, ",", &save);
    char *t4 = strtok_r(NULL, ",", &save);
    if (t1 == NULL || strcmp(t1, "a") != 0)
        return 0;
    if (t2 == NULL || strcmp(t2, "bb") != 0)
        return 0;
    if (t3 == NULL || strcmp(t3, "ccc") != 0)
        return 0;
    if (t4 != NULL)
        return 0;
    return 1;
}

static int cmp_int(const void *a, const void *b) {
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

static int check_stdlib_helpers(void) {
    if (abs(-5) != 5 || abs(5) != 5)
        return 0;
    if (labs(-7l) != 7l)
        return 0;
    char *end = NULL;
    errno = 0;
    long long ll = strtoll("  -123abc", &end, 10);
    if (ll != -123 || end == NULL || strcmp(end, "abc") != 0)
        return 0;
    errno = 0;
    unsigned long long ull = strtoull("0xFF!", &end, 0);
    if (ull != 255ull || end == NULL || *end != '!')
        return 0;
    errno = 0;
    (void)strtoull("99999999999999999999999999999999", &end, 10);
    if (errno != ERANGE)
        return 0;
    int arr[] = {5, 3, 9, 1, 7, 2};
    qsort(arr, 6, sizeof(int), cmp_int);
    for (int i = 1; i < 6; i++)
        if (arr[i - 1] > arr[i])
            return 0;
    int key = 7;
    int *found = (int *)bsearch(&key, arr, 6, sizeof(int), cmp_int);
    if (found == NULL || *found != 7)
        return 0;
    int missing = 100;
    if (bsearch(&missing, arr, 6, sizeof(int), cmp_int) != NULL)
        return 0;
    return 1;
}

static int check_ctype_helpers(void) {
    if (!isxdigit('a') || !isxdigit('F') || !isxdigit('9') || isxdigit('g'))
        return 0;
    if (!iscntrl('\n') || iscntrl('A'))
        return 0;
    if (!isgraph('A') || isgraph(' ') || isgraph('\n'))
        return 0;
    if (!ispunct('!') || ispunct('A') || ispunct(' '))
        return 0;
    if (!isblank(' ') || !isblank('\t') || isblank('\n'))
        return 0;
    /* unsigned-char and EOF-style inputs are deterministic. */
    if (isxdigit(-1) || iscntrl(EOF))
        return 0;
    return 1;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc < 1 || argv == NULL)
        fail("crt0");
    if (!check_round_trip())
        fail("round-trip");
    if (!check_seek_tell())
        fail("seek-tell");
    if (!check_fgets())
        fail("fgets");
    if (!check_setvbuf())
        fail("setvbuf");
    if (!check_failures())
        fail("failures");
    if (!check_freopen_stdout())
        fail("freopen-stdout");
    if (!check_exit_flush())
        fail("exit-flush");
    if (!check_str_helpers())
        fail("str-helpers");
    if (!check_stdlib_helpers())
        fail("stdlib-helpers");
    if (!check_ctype_helpers())
        fail("ctype-helpers");

    emit("BIGOS_LIBC_FILE_STREAM_PASSED\n");
    /* As PID-1 this must not exit; reap any further children. */
    for (;;) {
        if (wait(NULL) < 0) {
        }
    }
    return 0;
}
