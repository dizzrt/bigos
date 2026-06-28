/* BigOS bounded du: sum metadata sizes, not physical block usage. */
#include "tool_common.h"

static int du_sum(const char *path, unsigned int depth, unsigned long *total) {
    struct stat st;
    if (stat(path, &st) != 0) {
        tool_errno_error("du", path, "stat");
        return 1;
    }
    *total += st.st_size;
    if (depth >= TOOL_MAX_DEPTH || (!S_ISDIR(st.st_mode) && st.type != BIGOS_METADATA_TYPE_DIRECTORY))
        return 0;

    DIR *dir = opendir(path);
    if (dir == NULL) {
        tool_errno_error("du", path, "opendir");
        return 1;
    }
    int rc = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (entry == NULL) {
            if (errno != 0) {
                tool_errno_error("du", path, "readdir");
                rc = 1;
            }
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char child[TOOL_PATH_MAX];
        if (tool_join_path(child, sizeof(child), path, entry->d_name) != 0) {
            tool_error("du", "path too long");
            rc = 1;
            continue;
        }
        if (du_sum(child, depth + 1, total) != 0)
            rc = 1;
    }
    if (closedir(dir) != 0 && rc == 0) {
        tool_errno_error("du", path, "closedir");
        rc = 1;
    }
    return rc;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc < 2) {
        unsigned long total = 0;
        int rc = du_sum(".", 0, &total);
        printf("%lu .\n", total);
        return rc;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (tool_reject_unsupported_option("du", argv[i]) != 0) {
            rc = 1;
            continue;
        }
        unsigned long total = 0;
        if (du_sum(argv[i], 0, &total) != 0)
            rc = 1;
        printf("%lu %s\n", total, argv[i]);
    }
    return rc;
}
