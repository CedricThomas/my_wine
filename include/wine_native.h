#ifndef WINE_NATIVE_H
#define WINE_NATIVE_H

#include <stdint.h>
#include "kernel32.h"
#include "ntdll.h"
#include "msvcrt.h"

/* Initialize Wine native environment: handle table + CRT globals.
 * Call once at the start of main() before any Wine API. */
void wine_native_init(int argc, char *argv[], char *envp[]);

/* Convenience macro: define your entry as main() and it gets auto-wrapped */
#define WINE_NATIVE_ENTRY() \
    extern int wine_user_main(int, char *[]);          \
    int main(int argc, char *argv[], char *envp[])    \
    {                                                  \
        wine_native_init(argc, argv, envp);            \
        return wine_user_main(argc, argv);             \
    }

#endif /* WINE_NATIVE_H */
