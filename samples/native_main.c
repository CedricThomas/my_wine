/*
 * native_main.c — Entry point for native Wine samples.
 *
 * Provides main() which initializes the Wine environment (handle table,
 * CRT globals) before calling the user's entry point (wine_user_main).
 *
 * The user defines wine_user_main() in their own source file. When linked
 * against this object, the user does NOT need the WINE_NATIVE_ENTRY() macro.
 *
 * Link against the Wine stubs/loader objects to provide API implementations.
 */

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "wine_native.h"
#include "ntdll_priv.h"
#include "msvcrt_priv.h"
#include "loader_priv.h"

/*
 * wine_native_init — minimal initialization for native (non-PE) samples.
 *
 * Sets up the handle table (stdin/stdout/stderr) and CRT globals so that
 * Windows API calls (WriteFile, GetStdHandle, etc.) work from native ELF code.
 */
void wine_native_init(int argc, char *argv[], char *envp[])
{
    init_handle_table();
    g_guest_argv = argv;
    g_guest_envp = envp;
    _msvcrt_environ = envp;
    __msvcrt_app_type = 2;  /* console app */
    _commode = 0;
    _fmode = 0;

    if (argv && argv[0]) {
        strncpy(_cmdline_storage, argv[0], sizeof(_cmdline_storage) - 1);
        _cmdline_storage[sizeof(_cmdline_storage) - 1] = '\0';
        _acmdln = _cmdline_storage;
    } else {
        _acmdln = "";
    }
}

/* The real main() — wraps wine_native_init + wine_user_main */
extern int wine_user_main(int, char *[]);

int main(int argc, char *argv[], char *envp[])
{
    wine_native_init(argc, argv, envp);
    return wine_user_main(argc, argv);
}
