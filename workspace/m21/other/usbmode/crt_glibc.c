extern int main(int argc, char **argv, char **envp);
extern int __libc_start_main(
    int (*main)(int, char **, char **),
    int argc,
    char **ubp_av,
    void (*init)(void),
    void (*fini)(void),
    void (*rtld_fini)(void),
    void *stack_end);

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "mov r0, #0\n"
        "mov fp, r0\n"
        "mov lr, r0\n"
        "pop {r1}\n"          /* argc */
        "mov r2, sp\n"        /* argv */
        "bic sp, sp, #7\n"    /* Ensure 8-byte stack alignment before pushing 4 words (16 bytes) */
        "push {r2}\n"         /* padding word to keep 16-byte push (8-byte aligned) */
        "push {r2}\n"         /* stack_end */
        "push {r0}\n"         /* rtld_fini */
        "push {r0}\n"         /* fini */
        "ldr r0, =main\n"
        "mov r3, #0\n"        /* init */
        "bl __libc_start_main\n"
        "b .\n"
    );
}

extern int *__errno_location(void);
int *__errno(void) { return __errno_location(); }
