/* BigOS bounded default-terminal mode API.
 *
 * This is a BigOS-specific canonical/raw input mode subset for the single
 * default console terminal. It is not POSIX termios and intentionally omits
 * the unsupported POSIX terminal/device fields and complete job-control
 * semantics.
 */
#ifndef _BIGOS_USER_TERMINAL_H
#define _BIGOS_USER_TERMINAL_H

#include <sys/types.h>

#define BIGOS_TERMINAL_MODE_ABI_VERSION 1u
#define BIGOS_TERMINAL_MODE_FLAG_NONE   0u
#define BIGOS_TERMINAL_MODE_CANONICAL   0u
#define BIGOS_TERMINAL_MODE_RAW         1u

struct bigos_terminal_mode {
    unsigned int size;
    unsigned int version;
    unsigned int mode;
    unsigned int flags;
};

int bigos_tcgetmode(struct bigos_terminal_mode *out);
int bigos_tcsetmode(const struct bigos_terminal_mode *mode);

#endif /* _BIGOS_USER_TERMINAL_H */
