/* BigOS bounded metadata observer. Not a complete POSIX stat utility. */
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

static void write_uint(unsigned long value) {
    char buf[32];
    int i = 0;
    if (value == 0)
        buf[i++] = '0';
    while (value != 0) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (i > 0) {
        char ch = buf[--i];
        if (write(1, &ch, 1) != 1)
            exit(1);
    }
}

static const char *type_name(unsigned int type) {
    if (type == BIGOS_METADATA_TYPE_REGULAR)
        return "file";
    if (type == BIGOS_METADATA_TYPE_DIRECTORY)
        return "dir";
    return "unknown";
}

static int show_path(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        write_all(2, "stat: ");
        write_all(2, path);
        write_all(2, ": errno=");
        char digits[12];
        int n = 0;
        int e = errno;
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
        write_all(2, " ");
        write_all(2, strerror(errno));
        write_all(2, "\n");
        return 1;
    }
    write_all(1, "path=");
    write_all(1, path);
    write_all(1, " type=");
    write_all(1, type_name(st.type));
    write_all(1, " size=");
    write_uint(st.st_size);
    write_all(1, " mode=");
    write_uint(st.st_mode);
    write_all(1, " uid=");
    write_uint(st.st_uid);
    write_all(1, " gid=");
    write_uint(st.st_gid);
    write_all(1, " object=");
    write_uint(st.st_object_id);
    write_all(1, "\n");
    return 0;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc < 2) {
        write_all(2, "usage: stat PATH...\n");
        return 1;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++)
        if (show_path(argv[i]) != 0)
            rc = 1;
    return rc;
}
