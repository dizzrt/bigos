/* BigOS bounded directory-listing utility. Not a complete POSIX ls. */
#include "libc.h"

static void report_error(const char *path, const char *op) {
    fprintf(stderr, "ls: %s: %s errno=%d %s\n", path, op, errno, strerror(errno));
}

static const char *entry_type(unsigned int type) {
    if (type == BIGOS_DIRENT_TYPE_DIRECTORY)
        return "dir ";
    if (type == BIGOS_DIRENT_TYPE_FILE)
        return "file ";
    return "unk ";
}

static int list_path(const char *path, int show_header) {
    struct stat st;
    if (stat(path, &st) != 0) {
        report_error(path, "stat");
        return 1;
    }
    if (!S_ISDIR(st.st_mode) && st.type != BIGOS_METADATA_TYPE_DIRECTORY) {
        errno = ENOTDIR;
        report_error(path, "not directory");
        return 1;
    }

    DIR *dir = opendir(path);
    if (dir == NULL) {
        report_error(path, "opendir");
        return 1;
    }

    if (show_header)
        printf("%s:\n", path);

    int rc = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (entry == NULL) {
            if (errno != 0) {
                report_error(path, "readdir");
                rc = 1;
            }
            break;
        }
        if (printf("%s%s\n", entry_type(entry->d_type), entry->d_name) < 0) {
            report_error(path, "readdir");
            rc = 1;
            break;
        }
    }

    if (closedir(dir) != 0 && rc == 0) {
        report_error(path, "closedir");
        rc = 1;
    }
    return rc;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;

    if (argc < 2)
        return list_path(".", 0);

    int rc = 0;
    const int show_header = argc > 2;
    for (int i = 1; i < argc; i++) {
        if (i > 1 && show_header)
            putchar('\n');
        if (list_path(argv[i], show_header) != 0)
            rc = 1;
    }
    return rc;
}
