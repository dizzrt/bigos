/* BigOS user libc: minimal read-only environment access.
 *
 * environ is published by crt0 from the initial-stack envp. getenv scans it for
 * "name=value" and returns the value (after '='), or NULL when absent. Read-only
 * only: setenv/putenv/unsetenv are intentionally not implemented. */
#include "libc.h"

char **environ = NULL;

char *getenv(const char *name) {
    if (environ == NULL || name == NULL)
        return NULL;
    size_t name_len = strlen(name);
    for (char **e = environ; *e != NULL; e++) {
        const char *entry = *e;
        if (strncmp(entry, name, name_len) == 0 && entry[name_len] == '=')
            return (char *)(entry + name_len + 1);
    }
    return NULL;
}
