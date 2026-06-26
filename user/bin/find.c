/* BigOS bounded find: print paths under a supported directory tree. */
#include "tool_common.h"

static int find_path(const char *path, unsigned int depth) {
    tool_write_all(1, path);
    tool_write_all(1, "\n");
    if (depth >= TOOL_MAX_DEPTH)
        return 0;

    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    if (!S_ISDIR(st.st_mode) && st.type != BIGOS_METADATA_TYPE_DIRECTORY)
        return 0;

    DIR *dir = opendir(path);
    if (dir == NULL) {
        tool_errno_error("find", path, "opendir");
        return 1;
    }
    int rc = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (entry == NULL) {
            if (errno != 0) {
                tool_errno_error("find", path, "readdir");
                rc = 1;
            }
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char child[TOOL_PATH_MAX];
        if (tool_join_path(child, sizeof(child), path, entry->d_name) != 0) {
            tool_error("find", "path too long");
            rc = 1;
            continue;
        }
        if (find_path(child, depth + 1) != 0)
            rc = 1;
    }
    if (closedir(dir) != 0 && rc == 0) {
        tool_errno_error("find", path, "closedir");
        rc = 1;
    }
    return rc;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc > 2) {
        tool_error("find", "usage: find [PATH]");
        return 1;
    }
    return find_path(argc == 2 ? argv[1] : ".", 0);
}
