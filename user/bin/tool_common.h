/* Shared helpers for bounded BigOS user tools. Header-only so each tool remains
 * a single C translation unit under the existing user program builder. */
#ifndef BIGOS_USER_TOOL_COMMON_H
#define BIGOS_USER_TOOL_COMMON_H

#include "libc.h"

#define TOOL_BUF_SIZE 256
#define TOOL_PATH_MAX 256
#define TOOL_MAX_DEPTH 8
#define TOOL_UNUSED __attribute__((unused))

static TOOL_UNUSED void tool_write_all(int fd, const char *s) {
    size_t len = strlen(s);
    while (len > 0) {
        ssize_t n = write(fd, s, len);
        if (n <= 0)
            return;
        s += n;
        len -= (size_t)n;
    }
}

static TOOL_UNUSED void tool_error(const char *tool, const char *detail) {
    tool_write_all(2, tool);
    tool_write_all(2, ": ");
    if (detail != NULL)
        tool_write_all(2, detail);
    tool_write_all(2, "\n");
}

static TOOL_UNUSED void tool_errno_error(const char *tool, const char *path, const char *op) {
    tool_write_all(2, tool);
    tool_write_all(2, ": ");
    if (path != NULL) {
        tool_write_all(2, path);
        tool_write_all(2, ": ");
    }
    tool_write_all(2, op);
    tool_write_all(2, ": ");
    tool_write_all(2, strerror(errno));
    tool_write_all(2, "\n");
}

static TOOL_UNUSED int tool_is_option(const char *s) {
    return s != NULL && s[0] == '-' && s[1] != 0;
}

static TOOL_UNUSED int tool_reject_unsupported_option(const char *tool, const char *arg) {
    if (!tool_is_option(arg))
        return 0;
    tool_write_all(2, tool);
    tool_write_all(2, ": unsupported option: ");
    tool_write_all(2, arg);
    tool_write_all(2, "\n");
    return 1;
}

static TOOL_UNUSED int tool_parse_ulong(const char *s, unsigned long *out) {
    if (s == NULL || *s == 0 || out == NULL)
        return -1;
    unsigned long value = 0;
    for (const char *p = s; *p != 0; p++) {
        if (*p < '0' || *p > '9')
            return -1;
        unsigned long digit = (unsigned long)(*p - '0');
        if (value > (~0ul - digit) / 10ul)
            return -1;
        value = value * 10ul + digit;
    }
    *out = value;
    return 0;
}

static TOOL_UNUSED int tool_copy_fd(int in_fd, int out_fd, const char *tool, const char *path) {
    char buf[TOOL_BUF_SIZE];
    for (;;) {
        ssize_t n = read(in_fd, buf, sizeof(buf));
        if (n < 0) {
            tool_errno_error(tool, path, "read");
            return 1;
        }
        if (n == 0)
            return 0;
        size_t off = 0;
        while (off < (size_t)n) {
            ssize_t w = write(out_fd, buf + off, (size_t)n - off);
            if (w <= 0) {
                tool_errno_error(tool, path, "write");
                return 1;
            }
            off += (size_t)w;
        }
    }
}

static TOOL_UNUSED int tool_join_path(char *out, size_t cap, const char *base, const char *name) {
    if (out == NULL || base == NULL || name == NULL || cap == 0)
        return -1;
    size_t base_len = strlen(base);
    size_t name_len = strlen(name);
    int need_slash = base_len > 0 && strcmp(base, "/") != 0 && base[base_len - 1] != '/';
    size_t total = base_len + (need_slash ? 1u : 0u) + name_len;
    if (total >= cap)
        return -1;
    memcpy(out, base, base_len);
    size_t pos = base_len;
    if (need_slash)
        out[pos++] = '/';
    memcpy(out + pos, name, name_len);
    out[pos + name_len] = 0;
    return 0;
}

#endif /* BIGOS_USER_TOOL_COMMON_H */
