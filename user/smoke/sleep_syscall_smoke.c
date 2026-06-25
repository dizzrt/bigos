/* Blocking sleep syscall smoke.
 *
 * Runs as PID-1 when sleep_syscall_smoke is configured. It validates the
 * user-visible bounded sleep wrappers through monotonic tick observations and
 * emits deterministic markers mirrored to COM1 by fd 1.
 */
#include "libc.h"

#define SLEEP_SMOKE_MS 50ul
#define SLEEP_SMOKE_TIMER_HZ 100ul

static void emit(const char *s) {
    write(1, s, strlen(s));
}

static void fail(const char *why) {
    emit("BIGOS_SLEEP_SYSCALL_FAILED ");
    emit(why);
    emit("\n");
    exit(1);
}

static unsigned long expected_ticks(unsigned long milliseconds) {
    return (milliseconds * SLEEP_SMOKE_TIMER_HZ + 999ul) / 1000ul;
}

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;

    errno = 0;
    if (bigos_sleep_ms(0) != 0 || errno != 0)
        fail("zero");

    errno = 0;
    if (bigos_sleep_ms(~0ul) != -1 || errno != EINVAL)
        fail("range");

    const unsigned long start = get_tick();
    errno = 0;
    if (bigos_sleep_ms(SLEEP_SMOKE_MS) != 0 || errno != 0)
        fail("sleep-ms");
    const unsigned long end = get_tick();
    if (end - start < expected_ticks(SLEEP_SMOKE_MS))
        fail("tick-delta");

    if (sleep(0) != 0)
        fail("sleep-zero");

    emit("BIGOS_SLEEP_SYSCALL_PASSED\n");
    return 0;
}
