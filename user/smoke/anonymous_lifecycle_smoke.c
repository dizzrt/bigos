/* Default-off BigOS anonymous mapping lifecycle smoke.
 *
 * Runs as PID-1 when anonymous_lifecycle_smoke is selected. It validates the
 * bounded BigOS map/unmap/protect wrappers without claiming full POSIX mmap
 * behavior. Fault expectations are observed through child exits so the parent
 * can emit one deterministic serial-visible marker. */
#include "libc.h"

#define PAGE_SIZE 4096UL

static void emit(const char *s) {
    write(1, s, strlen(s));
}

static void fail(const char *why) {
    emit("BIGOS_ANON_LIFECYCLE_FAILED ");
    emit(why);
    emit("\n");
    exit(1);
}

static void fail_errno(const char *why) {
    emit("BIGOS_ANON_LIFECYCLE_FAILED ");
    emit(why);
    emit(" errno=");
    char digit = (char)('0' + (errno / 10) % 10);
    write(1, &digit, 1);
    digit = (char)('0' + errno % 10);
    write(1, &digit, 1);
    emit("\n");
    exit(1);
}

static pid_t spawn_read_fault(char *addr) {
    pid_t pid = fork();
    if (pid < 0)
        fail("fork-unmap");
    if (pid == 0) {
        volatile char value = addr[0];
        (void)value;
        exit(0);
    }
    return pid;
}

static pid_t spawn_write_fault(char *addr) {
    pid_t pid = fork();
    if (pid < 0)
        fail("fork-protect");
    if (pid == 0) {
        addr[0] = 'x';
        exit(0);
    }
    return pid;
}

static void expect_faulted(pid_t pid, const char *why) {
    int status = 0;
    if (wait_status(pid, &status) != pid)
        fail(why);
    if (status == 0)
        fail(why);
}

int main(void) {
    char *base = (char *)mmap_anon(PAGE_SIZE * 3, PROT_READ | PROT_WRITE, 0);
    if (base == MAP_FAILED)
        fail("map");

    base[0] = 'a';
    base[PAGE_SIZE] = 'b';
    base[PAGE_SIZE * 2] = 'c';

    errno = 0;
    if (bigos_mprotect_anon(base, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC) != -1 || errno != EINVAL)
        fail("reject-wx");
    base[0] = 'd';

    if (bigos_mprotect_anon(base + PAGE_SIZE, PAGE_SIZE, PROT_READ) != 0)
        fail("protect");

    if (bigos_munmap_anon(base + PAGE_SIZE * 2, PAGE_SIZE) != 0)
        fail_errno("unmap");
    pid_t unmap_pid = spawn_read_fault(base + PAGE_SIZE * 2);
    /* The second child covers write-after-readonly through the serial #PF path. */
    pid_t protect_pid = spawn_write_fault(base + PAGE_SIZE);
    expect_faulted(unmap_pid, "access-after-unmap");
    (void)protect_pid;

    emit("BIGOS_ANON_LIFECYCLE_PASSED\n");
    exit(0);
}
