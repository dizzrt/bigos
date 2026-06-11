/* BigOS test binary: copy stdin to stdout until EOF.
 *
 * This tiny cat is intended for shell pipe/redirection smoke tests such as
 * `echo pipe-ok | /bin/cat`. */
#include "libc.h"

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;

    char buf[128];
    for (;;) {
        ssize_t n = read(0, buf, sizeof(buf));
        if (n < 0)
            return 1;
        if (n == 0)
            return 0;
        size_t written = 0;
        while (written < (size_t)n) {
            ssize_t w = write(1, buf + written, (size_t)n - written);
            if (w <= 0)
                return 1;
            written += (size_t)w;
        }
    }
}
