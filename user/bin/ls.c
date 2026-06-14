/* BigOS bounded directory-listing utility. Not a complete POSIX ls. */
#include "libc.h"

static void write_all(int fd, const char *s) {
    size_t len = strlen(s);
    while (len > 0) {
        ssize_t n = write(fd, s, len);
        if (n <= 0)
            exit(1);
        s += n;
        len -= (size_t)n;
    }
}

static void write_errno_value(int e) {
    char digits[12];
    int n = 0;
    if (e == 0)
        digits[n++] = '0';
    while (e != 0) {
        digits[n++] = (char)('0' + (e % 10));
        e /= 10;
    }
    while (n > 0) {
        char ch = digits[--n];
        if (write(2, &ch, 1) != 1)
            exit(1);
    }
}

static void report_error(const char *path, const char *op) {
    write_all(2, "ls: ");
    write_all(2, path);
    write_all(2, ": ");
    write_all(2, op);
    write_all(2, " errno=");
    write_errno_value(errno);
    write_all(2, " ");
    write_all(2, strerror(errno));
    write_all(2, "\n");
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

    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        report_error(path, "open");
        return 1;
    }

    if (show_header) {
        write_all(1, path);
        write_all(1, ":\n");
    }

    int rc = 0;
    for (;;) {
        struct bigos_dirent entries[BIGOS_DIRENT_MAX_BATCH];
        ssize_t count = bigos_readdir(fd, entries, BIGOS_DIRENT_MAX_BATCH);
        if (count < 0) {
            report_error(path, "readdir");
            rc = 1;
            break;
        }
        if (count == 0)
            break;
        for (ssize_t i = 0; i < count; i++) {
            write_all(1, entry_type(entries[i].type));
            write_all(1, entries[i].name);
            write_all(1, "\n");
        }
    }

    if (close(fd) != 0 && rc == 0) {
        report_error(path, "close");
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
            write_all(1, "\n");
        if (list_path(argv[i], show_header) != 0)
            rc = 1;
    }
    return rc;
}
