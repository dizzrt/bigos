#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc > 1) {
        fprintf(stderr, "usage: %s\n", argv[0]);
        fprintf(stderr, "formats only the configured BigOS persistent /rw test disk\n");
        return 2;
    }
    if (bigos_mkfs_bigfs() != 0) {
        perror("mkfs_bigfs");
        return 1;
    }
    puts("mkfs_bigfs: formatted configured persistent /rw test disk");
    return 0;
}
