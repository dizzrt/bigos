/* BigOS bounded metadata observer. Not a complete POSIX stat utility. */
#include "tool_common.h"

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
        tool_errno_error("stat", path, "stat");
        return 1;
    }
    tool_write_all(1, "path=");
    tool_write_all(1, path);
    tool_write_all(1, " type=");
    tool_write_all(1, type_name(st.type));
    tool_write_all(1, " size=");
    write_uint(st.st_size);
    tool_write_all(1, " mode=");
    write_uint(st.st_mode);
    tool_write_all(1, " uid=");
    write_uint(st.st_uid);
    tool_write_all(1, " gid=");
    write_uint(st.st_gid);
    tool_write_all(1, " object=");
    write_uint(st.st_object_id);
    tool_write_all(1, " atime=");
    write_uint(st.st_atime);
    tool_write_all(1, " mtime=");
    write_uint(st.st_mtime);
    tool_write_all(1, " ctime=");
    write_uint(st.st_ctime);
    tool_write_all(1, "\n");
    return 0;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc < 2) {
        tool_error("stat", "usage: stat PATH...");
        return 1;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (tool_reject_unsupported_option("stat", argv[i]) != 0) {
            rc = 1;
            continue;
        }
        if (show_path(argv[i]) != 0)
            rc = 1;
    }
    return rc;
}
