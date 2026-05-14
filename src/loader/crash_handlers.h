/*
 * crash_handlers.h — SEH + POSIX signal crash handlers
 *
 * Installs crash handlers for both Windows-style SEH exceptions
 * and POSIX signals.
 */

#ifndef MY_WINE_CRASH_HANDLERS_H
#define MY_WINE_CRASH_HANDLERS_H

void setup_signal_handlers(void);
#if !defined(__i386__)
__attribute__((ms_abi))
#endif
void seh_crash_handler(void *, void *, void *, void *);

#endif /* MY_WINE_CRASH_HANDLERS_H */
