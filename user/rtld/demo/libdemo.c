/* BigOS dynamic-link demo shared library (libdemo.so).
 *
 * Built -fPIC -shared. Exports a couple of function and data symbols that the
 * dyn_demo executable references across the module boundary, deliberately using
 * only the supported relocation subset (no TLS, no IFUNC). It avoids the user
 * libc so the example stays a self-contained shared object. */

/* Exported data symbol (resolved via R_X86_64_GLOB_DAT in the consumer). */
int demo_value = 41;

/* Exported function symbols (resolved via R_X86_64_JMP_SLOT in the consumer). */
int demo_add(int a, int b) {
    return a + b;
}

const char *demo_msg(void) {
    return "demo";
}
