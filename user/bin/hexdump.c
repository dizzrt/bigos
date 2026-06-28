/* BigOS bounded hexdump. */
#include "tool_common.h"

static int printable(unsigned char ch) {
    return ch >= 32 && ch < 127;
}

static void print_hex_digit(unsigned int value) {
    putchar(value < 10 ? (int)('0' + value) : (int)('a' + value - 10));
}

static void print_hex_byte(unsigned char value) {
    print_hex_digit((unsigned int)(value >> 4));
    print_hex_digit((unsigned int)(value & 0xf));
}

static void print_hex_offset(unsigned long value) {
    for (int shift = 28; shift >= 0; shift -= 4)
        print_hex_digit((unsigned int)((value >> shift) & 0xf));
}

static int dump_fd(int fd, const char *path) {
    unsigned char buf[16];
    unsigned long offset = 0;
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            tool_errno_error("hexdump", path, "read");
            return 1;
        }
        if (n == 0)
            return 0;
        print_hex_offset(offset);
        printf("  ");
        for (int i = 0; i < 16; i++) {
            if (i < n) {
                print_hex_byte(buf[i]);
                printf(" ");
            } else {
                printf("   ");
            }
            if (i == 7)
                printf(" ");
        }
        printf(" |");
        for (int i = 0; i < n; i++)
            putchar(printable(buf[i]) ? buf[i] : '.');
        printf("|\n");
        offset += (unsigned long)n;
    }
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc == 1)
        return dump_fd(0, NULL);
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (tool_reject_unsupported_option("hexdump", argv[i]) != 0) {
            rc = 1;
            continue;
        }
        int fd = open(argv[i], O_RDONLY, 0);
        if (fd < 0) {
            tool_errno_error("hexdump", argv[i], "open");
            rc = 1;
            continue;
        }
        if (dump_fd(fd, argv[i]) != 0)
            rc = 1;
        close(fd);
    }
    return rc;
}
