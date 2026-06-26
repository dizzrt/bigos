/* BigOS bounded mv: rename one path to another through the existing VFS contract. */
#include "tool_common.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 3) {
        tool_error("mv", "usage: mv OLD NEW");
        return 1;
    }
    if (rename(argv[1], argv[2]) != 0) {
        tool_write_all(2, "mv: ");
        tool_write_all(2, argv[1]);
        tool_write_all(2, " -> ");
        tool_write_all(2, argv[2]);
        tool_write_all(2, ": ");
        tool_write_all(2, strerror(errno));
        tool_write_all(2, "\n");
        return 1;
    }
    return 0;
}
