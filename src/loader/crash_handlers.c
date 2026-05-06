/*
 * crash_handlers.c — SEH + POSIX signal crash handlers
 *
 * Installs crash handlers for both Windows-style SEH exceptions
 * and POSIX signals (SIGSEGV, SIGILL, SIGABRT, SIGFPE, SIGBUS, SIGTRAP).
 * Also sets up an alternate signal stack via sigaltstack.
 */

#define _GNU_SOURCE

#include "../syscall/syscalls_inline.h"
#include "loader_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <ucontext.h>
#include <sys/syscall.h>
#include <asm/unistd_64.h>
#include <sys/mman.h>

#include "include/common.h"
#include "include/syscall/thunk_gen.h"
#include <sys/user.h>

/**
 * SEH handler — called when an exception occurs in guest code.
 * On x86_64, SEH handlers receive (ExceptionRecord, EstablisherFrame,
 * ContextRecord, DispatcherContext) in RCX, RDX, R8, R9 per Microsoft x64 ABI.
 */
__attribute__((ms_abi, used, noreturn))
void seh_crash_handler(void *exception_record, void *establisher_frame,
                       void *context_record, void *dispatcher_context)
{
    (void)exception_record;
    (void)establisher_frame;
    (void)context_record;
    (void)dispatcher_context;

    /* Dump info via inline syscall (post-GS safe) */
    { const char t[] = "SEV: SEH handler invoked (exception in guest code)\n";
      INLINE_SYSCALL_WRITE(2, t, sizeof(t)-1); }

    /* Extract exit code from exception record if possible, else use 0xC0000005 (ACCESS_VIOLATION) */
    uint64_t exit_code = 0xC0000005;

    /* Call sys_exit directly (post-GS safe) */
    INLINE_SYSCALL_EXIT((int)(exit_code & 0xFF));
}

/**
 * POSIX signal crash handler. Dumps register state and exits.
 */
static void crash_handler(int sig, siginfo_t *info, void *ucontext)
{
    (void)info;
    const char sig_sev[] = "CRASH: SIGSEGV";
    const char sig_ill[] = "CRASH: SIGILL";
    const char sig_abt[] = "CRASH: SIGABRT";
    const char sig_fpe[] = "CRASH: SIGFPE";
    const char sig_bus[] = "CRASH: SIGBUS";
    const char sig_trap[] = "CRASH: SIGTRAP";
    const char sig_unk[] = "CRASH: UNKNOWN";

    const char *sig_name = sig_unk;
    int sig_len = 13;
    if (sig == SIGSEGV) { sig_name = sig_sev; sig_len = 13; }
    else if (sig == SIGILL) { sig_name = sig_ill; sig_len = 12; }
    else if (sig == SIGABRT) { sig_name = sig_abt; sig_len = 13; }
    else if (sig == SIGFPE) { sig_name = sig_fpe; sig_len = 12; }
    else if (sig == SIGBUS) { sig_name = sig_bus; sig_len = 12; }
    else if (sig == SIGTRAP) { sig_name = sig_trap; sig_len = 13; }

    INLINE_SYSCALL_WRITE_ERR(sig_name, (size_t)sig_len);

    ucontext_t *uc = (ucontext_t *)ucontext;
    if (uc) {
        greg_t *regs = uc->uc_mcontext.gregs;
        char hex_buf[200];
        int off = 0;
        const char *labels[] = {" RIP=", " RSP=", " RAX="};
        int reg_indices[] = {REG_RIP, REG_RSP, REG_RAX};
        for (int j = 0; j < 3; j++) {
            for (int k = 0; labels[j][k]; k++) hex_buf[off++] = labels[j][k];
            uint64_t val = (uint64_t)regs[reg_indices[j]];
            format_hex(hex_buf + off, sizeof(hex_buf) - off, val);
            off += 16;
        }
        hex_buf[off++] = '\n';
        hex_buf[off] = '\0';
        INLINE_SYSCALL_WRITE_ERR(hex_buf, (size_t)off);
    }

    /*
     * Use INLINE_SYSCALL_EXIT (direct syscall) instead of _exit().
     * In the signal handler context, the guest stack may be corrupted
     * and GS base points to the TEB — glibc's _exit needs TLS and other
     * internal state that can segfault in this context.
     * A direct syscall is fully async-signal-safe and avoids this.
     */
    INLINE_SYSCALL_EXIT(139);
}

/**
 * Set up POSIX signal handlers and alternate signal stack.
 */
void setup_signal_handlers(void)
{
    /*
     * Set up the signal stack BEFORE installing handlers.
     * This ensures that if a SIGSEGV occurs during sigaction
     * (e.g., due to glibc TLS access hitting the wrong GS base),
     * the handler runs on the safe signal stack instead of the
     * potentially-corrupted current stack.
     */
    void *sigstack_mem = mmap(NULL, SIG_STACK_SIZE, PROT_READ|PROT_WRITE,
                              MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (sigstack_mem == MAP_FAILED) {
        /* Fallback: handlers will run on the current stack */
    } else {
        stack_t ss;
        ss.ss_sp = sigstack_mem;
        ss.ss_size = SIG_STACK_SIZE;
        ss.ss_flags = 0;
        sigaltstack(&ss, NULL);
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGTRAP, &sa, NULL);
    { const char t[] = "GUEST: all handlers set\n";
      syscall(__NR_write, 2, t, sizeof(t)-1); }
}
