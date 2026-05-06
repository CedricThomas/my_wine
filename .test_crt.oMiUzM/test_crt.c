#include <stdio.h>
#include <stdlib.h>

// Force the CRT .refptr symbols into the binary
extern int __argc;
extern char **__argv;
extern char ***__initenv;

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("argc=%d, __argc=%d\n", argc, __argc);
    printf("__argv=%p\n", (void *)__argv);
    printf("__initenv=%p\n", (void *)__initenv);
    return 0;
}
