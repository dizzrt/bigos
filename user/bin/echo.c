/* BigOS test binary: prints its argv[1..] separated by spaces plus a newline,
 * mirroring a tiny echo. Used by the shell external-command and pipe tests. */
#include "libc.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    for (int i = 1; i < argc; i++) {
        if (i > 1)
            putchar(' ');
        size_t len = strlen(argv[i]);
        if (len != 0)
            write(1, argv[i], len);
    }
    putchar('\n');
    return 0;
}
