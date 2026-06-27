/* BigOS dynamic-link demo executable (dyn_demo).
 *
 * Built -fPIC -pie with DT_NEEDED libdemo.so and PT_INTERP = /lib/ld-bigos.so.
 * The kernel loads it plus ld.so; ld.so loads libdemo.so and binds the
 * cross-module symbols below. main calls them, checks the results, and emits the
 * deterministic BIGOS_DYNLINK marker. Self-contained syscall stubs keep it off
 * the static user libc. */

typedef unsigned long size_t;

#define SYS_WRITE 2
#define SYS_EXIT  3

static long sys3(long n, long a0, long a1, long a2) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a0), "S"(a1), "d"(a2) : "rcx", "r11", "memory");
    return r;
}
static long sys1(long n, long a0) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a0) : "rcx", "r11", "memory");
    return r;
}

static size_t d_strlen(const char *s) {
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}
static void d_write(const char *s) { (void)sys3(SYS_WRITE, 1, (long)s, (long)d_strlen(s)); }

/* Cross-module references: function + data symbols exported by libdemo.so. */
extern int demo_add(int a, int b);
extern const char *demo_msg(void);
extern int demo_value;

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;

    int sum = demo_add(1, 2);                  /* JMP_SLOT cross-module call */
    const char *msg = demo_msg();              /* JMP_SLOT cross-module call */
    int v = demo_value;                        /* GLOB_DAT cross-module data */

    int ok = (sum == 3) && (v == 41) && msg != 0 && msg[0] == 'd' && msg[1] == 'e' && msg[2] == 'm' &&
             msg[3] == 'o' && msg[4] == 0;
    if (ok)
        d_write("BIGOS_DYNLINK_PASSED\n");
    else
        d_write("BIGOS_DYNLINK_FAILED demo-mismatch\n");
    (void)sys1(SYS_EXIT, ok ? 0 : 1);
    for (;;) {}
    return 0;
}
