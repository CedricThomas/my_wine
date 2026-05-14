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
#ifdef __x86_64__
#include <asm/unistd_64.h>
#endif
#include <sys/mman.h>

#include "include/common.h"
#include "include/nt_constants.h"
#include "include/syscall/thunk_gen.h"
#include <sys/user.h>

static int g_alt_stack_available = 1;  /* Flipped to 0 if signal stack mmap fails */
static volatile int g_in_crash_handler = 0;  /* Recursion guard */

/*
 * SEH handler — called when an exception occurs in guest code.
 * On x86_64: args in RCX/RDX/R8/R9 (ms_abi).
 * On i386:   args on stack (cdecl, no ms_abi).
 */
#if defined(__i386__)
__attribute__((used, noreturn))
void seh_crash_handler(void *exception_record, void *establisher_frame,
                       void *context_record, void *dispatcher_context)
#else
__attribute__((ms_abi, used, noreturn))
void seh_crash_handler(void *exception_record, void *establisher_frame,
                       void *context_record, void *dispatcher_context)
#endif
{
    (void)exception_record;
    (void)establisher_frame;
    (void)context_record;
    (void)dispatcher_context;

    /* Dump info via inline syscall (post-GS safe) */
    { const char t[] = "SEV: SEH handler invoked (exception in guest code)\n";
      INLINE_SYSCALL_WRITE(2, t, sizeof(t)-1); }

    /* Extract exit code from exception record if possible, else use STATUS_ACCESS_VIOLATION */
    uint64_t exit_code = STATUS_ACCESS_VIOLATION;

    /* Call sys_exit directly (post-GS safe) */
    INLINE_SYSCALL_EXIT((int)(exit_code & 0xFF));
}

/**
 * POSIX signal crash handler. Dumps register state and exits.
 */
static void crash_handler(int sig, siginfo_t *info, void *ucontext)
{
    /* Recursion guard: if we're already in the handler, just exit immediately */
    if (g_in_crash_handler) {
        INLINE_SYSCALL_EXIT(EXIT_SIGSEGV);
    }
    g_in_crash_handler = 1;

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

    /* Dump diagnostic info: si_addr, ucontext ptr */
    {
        uintptr_t uc_ptr = (uintptr_t)ucontext;
        uintptr_t fault_addr = 0;
        /* Try to read si_addr safely */
        if (info != NULL && (uintptr_t)info >= 0x1000 && (uintptr_t)info < 0xFFFFC000UL) {
            fault_addr = (uintptr_t)info->si_addr;
        }
        char buf[128];
        int n = 0;
        const char *p;
        for (p = "CRASH: si_addr=0x"; *p && n < 120; ) buf[n++] = *p++;
        for (int h = 7; h >= 0; h--) {
            buf[n++] = "0123456789abcdef"[(fault_addr >> (h*4)) & 0xf];
        }
        for (p = ", ucontext=0x"; *p && n < 120; ) buf[n++] = *p++;
        for (int h = 7; h >= 0; h--) {
            buf[n++] = "0123456789abcdef"[(uc_ptr >> (h*4)) & 0xf];
        }
        buf[n++] = '\n';
        INLINE_SYSCALL_WRITE(2, buf, n);
    }

    if (!g_alt_stack_available) {
        const char stack_warn[] = "WARNING: running on guest stack — crash may be unrecoverable\n";
        INLINE_SYSCALL_WRITE(2, stack_warn, sizeof(stack_warn) - 1);
    }

    /*
     * SAFETY: The ucontext pointer is provided by the kernel on the signal stack.
     * However, in our environment (FS→TEB switch, custom stack setup), the
     * ucontext may be at an invalid location or the signal handler may be
     * running on a corrupted guest stack instead of the alt stack.
     *
     * We validate the pointer before accessing it. If it's clearly invalid,
     * we skip the register dump entirely to avoid a SECOND crash that would
     * obscure the original crash diagnostics.
     *
     * On Linux x86, the ucontext is always placed on the signal stack (either
     * the alt stack or the current stack). It should be in user-space.
     * We check: non-NULL, aligned, in a reasonable user-space range.
     */
    {
        uintptr_t uc_ptr = (uintptr_t)ucontext;
        int uc_valid = 1;

        if (uc_ptr == 0) {
            uc_valid = 0;
        } else if (uc_ptr & 3) {
            uc_valid = 0;  /* misaligned */
        }
#if defined(__i386__)
        else if (uc_ptr > 0xFFFFC000UL) {
            uc_valid = 0;  /* kernel space or too high */
        } else if (uc_ptr < 0x1000) {
            uc_valid = 0;  /* null page / too low */
        }
#else
        else if (uc_ptr > 0xfffffffffffe0000UL) {
            uc_valid = 0;
        }
#endif

        if (uc_valid) {
            /* Try to read EIP/RIP from ucontext with a protection:
             * we attempt the read in a bounded way. If this crashes,
             * the kernel will deliver SIGSEGV again, but since our
             * handler is already running, it will terminate the process
             * (default SIG_DFL behavior for nested signal on same handler).
             * This is acceptable — we get the signal name printed at least. */
            ucontext_t *uc = (ucontext_t *)ucontext;
            greg_t *regs = uc->uc_mcontext.gregs;
            char hex_buf[200];
            int off = 0;
            const char *labels[] = {
#if defined(__i386__)
                " EIP=", " ESP=", " EAX="
#else
                " RIP=", " RSP=", " RAX="
#endif
            };
            int reg_indices[] = {
#if defined(__i386__)
                REG_EIP, REG_ESP, REG_EAX
#else
                REG_RIP, REG_RSP, REG_RAX
#endif
            };
            for (int j = 0; j < 3; j++) {
                for (int k = 0; labels[j][k]; k++) hex_buf[off++] = labels[j][k];
                uint64_t val = (uint64_t)regs[reg_indices[j]];
                format_hex(hex_buf + off, sizeof(hex_buf) - off, val);
                off += 16;
            }
            hex_buf[off++] = '\n';
            hex_buf[off] = '\0';
            INLINE_SYSCALL_WRITE_ERR(hex_buf, (size_t)off);
        } else {
            const char warn[] = "CRASH: ucontext invalid, skipping register dump\n";
            INLINE_SYSCALL_WRITE_ERR(warn, sizeof(warn) - 1);
            /* Fallback: read ESP + EBP, derive signal frame return address */
#if defined(__i386__)
            {
                uintptr_t esp, ebp;
                __asm__ volatile("movl %%esp, %0" : "=r"(esp));
                __asm__ volatile("movl %%ebp, %0" : "=r"(ebp));
                char buf[200]; int n = 0;
                const char *p;

                /* ESP gives current stack position */
                p = "CRASH: fallback ESP=0x";
                while (*p && n < 200) buf[n++] = *p++;
                for (int h = 7; h >= 0; h--)
                    buf[n++] = "0123456789abcdef"[(esp>>(h*4))&0xf];

                /* EBP points to saved frame pointer; return address (signal trampoline)
                 * at [EBP+4]. This is the kernel's sigreturn address, useful for
                 * confirming the signal handler frame is intact. */
                p = ", EBP=0x";
                while (*p && n < 200) buf[n++] = *p++;
                for (int h = 7; h >= 0; h--)
                    buf[n++] = "0123456789abcdef"[(ebp>>(h*4))&0xf];

                /* Read return address from signal handler frame: [EBP+4] */
                uintptr_t sig_ret = 0;
                if (ebp > 0x1000 && ebp < 0xFFFFC000UL)
                    sig_ret = *(uintptr_t *)(ebp + 4);

                p = ", signal_ret=0x";
                while (*p && n < 200) buf[n++] = *p++;
                for (int h = 7; h >= 0; h--)
                    buf[n++] = "0123456789abcdef"[(sig_ret>>(h*4))&0xf];

                /* Dump a few words at current ESP for additional context */
                p = ", stack=[";
                while (*p && n < 200) buf[n++] = *p++;
                uintptr_t *sp = (uintptr_t *)esp;
                for (int i = 0; i < 4 && n < 200; i++) {
                    if (i > 0) buf[n++] = ' ';
                    for (int h = 7; h >= 0; h--)
                        buf[n++] = "0123456789abcdef"[(sp[i]>>(h*4))&0xf];
                }
                buf[n++] = ']';
                buf[n++] = '\n';
                INLINE_SYSCALL_WRITE(2, buf, n);
            }
#endif
        }
    }

    /*
     * Use INLINE_SYSCALL_EXIT (direct syscall) instead of _exit().
     * In the signal handler context, the guest stack may be corrupted
     * and GS base points to the TEB — glibc's _exit needs TLS and other
     * internal state that can segfault in this context.
     * A direct syscall is fully async-signal-safe and avoids this.
     */
    INLINE_SYSCALL_EXIT(EXIT_SIGSEGV);
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
#ifdef MY_WINE32
    void *sigstack_mem = INLINE_SYSCALL_MMAP(NULL, SIG_STACK_SIZE, PROT_READ|PROT_WRITE,
                              MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
#else
    void *sigstack_mem = mmap(NULL, SIG_STACK_SIZE, PROT_READ|PROT_WRITE,
                              MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
#endif
    if (sigstack_mem == MAP_FAILED) {
        /*
         * mmap for SIG_STACK_SIZE (~64KB) failing means the system is critically
         * out of memory. At that point, even a hard abort via syscall cannot be
         * guaranteed to succeed. We choose graceful degradation over hard error:
         *   - The crash handler still runs (on the guest stack) and can emit
         *     diagnostics + exit via direct syscall.
         *   - The g_alt_stack_available flag ensures crash_handler logs a warning
         *     so the operator knows the dump may be unreliable.
         *   - The project is single-threaded, so no concurrent stack corruption risk.
         *   - A catastrophic OOM during init means a crash will likely fail anyway,
         *     so hard-stop would add no value over the degraded path.
         *
         * Verdict: warning + flag is the correct tradeoff for resilience.
         */
        g_alt_stack_available = 0;
        const char warn_msg[] = "WARNING: alt signal stack mmap failed — crash handlers will run on guest stack (crash may be unrecoverable)\n";
        INLINE_SYSCALL_WRITE(2, warn_msg, sizeof(warn_msg) - 1);
    } else {
        stack_t ss;
        ss.ss_sp = sigstack_mem;
        ss.ss_size = SIG_STACK_SIZE;
        ss.ss_flags = 0;
#ifdef MY_WINE32
        INLINE_SYSCALL_SIGALTSTACK(&ss, NULL);
#else
        sigaltstack(&ss, NULL);
#endif
    }

    struct sigaction sa;
#ifdef MY_WINE32
    __builtin_memset(&sa, 0, sizeof(sa));
#else
    memset(&sa, 0, sizeof(sa));
#endif
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
#ifdef MY_WINE32
    for (int _si = 0; _si < (int)(sizeof(sa.sa_mask.__val)/sizeof(sa.sa_mask.__val[0])); _si++)
        sa.sa_mask.__val[_si] = 0;
#else
    sigemptyset(&sa.sa_mask);
#endif
#ifdef MY_WINE32
    INLINE_SYSCALL_SIGACTION(SIGSEGV, &sa, NULL);
    INLINE_SYSCALL_SIGACTION(SIGILL, &sa, NULL);
    INLINE_SYSCALL_SIGACTION(SIGABRT, &sa, NULL);
    INLINE_SYSCALL_SIGACTION(SIGFPE, &sa, NULL);
    INLINE_SYSCALL_SIGACTION(SIGBUS, &sa, NULL);
    INLINE_SYSCALL_SIGACTION(SIGTRAP, &sa, NULL);
#else
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGTRAP, &sa, NULL);
#endif
    DEBUG("GUEST: all handlers set");
}
