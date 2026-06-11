/* BigOS user_elf_smoke program: minimal C user program that writes a marker
 * and exits. Linked as /boot/user/init.elf only for the user_elf_smoke /
 * user_program_smoke builds so their BIGOS_USER_ENTER/BIGOS_USER_EXIT markers
 * keep their original "print then exit" semantics (the default build instead
 * links the resident C init that never exits). */
#include "libc.h"

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;
    const char *msg = "BIGOS_USER_ELF_WRITE\n";
    write(1, msg, strlen(msg));
    return 0;
}
