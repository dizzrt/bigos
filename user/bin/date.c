/* BigOS bounded date: print current Unix seconds. */
#include "tool_common.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    (void)argv;
    if (argc != 1) {
        tool_error("date", "usage: date");
        return 1;
    }
    printf("%ld\n", (long)time(NULL));
    return 0;
}
