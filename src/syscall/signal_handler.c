#define _GNU_SOURCE
#include <signal.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <seccomp.h>
#include <ucontext.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <linux/seccomp.h>
#include <linux/filter.h>

/* ------------------------------------------------------------------ */
/*  Types                                                             */
/* ------------------------------------------------------------------ */

/* Dispatcher function: receives the (elevated) syscall number and the
 * full ucontext.  Returns 0 on success (handler modified the context
 * to produce a result) or non-zero on failure.                       */
typedef int (*dispatcher_func_t)(uint64_t syscall_num, ucontext_t *ctx);

/* ------------------------------------------------------------------ */
/*  Registered thunk addresses (dynamic array)                        */
/* ------------------------------------------------------------------ */

#define MAX_THUNKS 16

static void *thunk_addrs[MAX_THUNKS];
static int thunk_count = 0;

/*
 * register_thunk_addr — add a generated thunk address to the list.
 * Called from syscall_gen.c after each thunk is mmap'd.
 */
void register_thunk_addr(void *addr)
{
    if (thunk_count < MAX_THUNKS) {
        thunk_addrs[thunk_count++] = addr;
    }
}

/* ------------------------------------------------------------------ */
/*  Global dispatcher reference                                       */
/* ------------------------------------------------------------------ */

static dispatcher_func_t g_dispatcher = NULL;

/* ------------------------------------------------------------------ */
/*  SIGSYS signal handler                                             */
/* ------------------------------------------------------------------ */

static void sigsys_handler(int sig, siginfo_t *info, void *ucontext)
{
    ucontext_t *uctx = (ucontext_t *)ucontext;
    uint64_t syscall_num = (uint64_t)info->si_syscall;

    (void)sig;  /* keep compiler happy */

    /* --- Validate call address is within ±4096 of a registered thunk */
    if (thunk_count > 0) {
        uint8_t *call_addr = (uint8_t *)info->si_call_addr;
        int matched = 0;
        for (int i = 0; i < thunk_count; i++) {
            uint8_t *base = (uint8_t *)thunk_addrs[i];
            if (call_addr >= base - 4096 && call_addr <= base + 4096) {
                matched = 1;
                break;
            }
        }
        if (!matched) {
            /* Call site is not one of our generated thunks – fatal */
            raise(SIGSEGV);
            return;
        }
    }

    /* --- Dispatch -------------------------------------------------- */
    if (g_dispatcher == NULL) {
        fprintf(stderr, "my_wine: SIGSYS with no dispatcher installed\n");
        raise(SIGSEGV);
        return;
    }

    if (g_dispatcher(syscall_num, uctx) != 0) {
        fprintf(stderr, "my_wine: dispatcher failed for syscall 0x%lx\n",
                syscall_num);
        raise(SIGSEGV);
        return;
    }

    /* --- Success: advance RIP past the `syscall` instruction (2 bytes:
     *             0x0f 0x05) so execution continues with the handler's
     *             result in RAX.                                      */
    uctx->uc_mcontext.gregs[REG_RIP] += 2;
}

/* ------------------------------------------------------------------ */
/*  setup_sigsys_handler                                              */
/* ------------------------------------------------------------------ */

int setup_sigsys_handler(dispatcher_func_t dispatcher)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigsys_handler;
    sa.sa_flags     = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);

    g_dispatcher = dispatcher;

    if (sigaction(SIGSYS, &sa, NULL) != 0) {
        perror("sigaction(SIGSYS)");
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  setup_seccomp                                                     */
/* ------------------------------------------------------------------ */

/*
 * Install a raw seccomp-BPF filter that:
 *   - ALLOWs every syscall with number < 0xF000 (native Linux syscalls)
 *   - TRAPs  (sends SIGSYS) every syscall with number >= 0xF000
 *
 * CRITICAL:  The SIGSYS handler must be installed via
 * setup_sigsys_handler() BEFORE calling this function.
 */
int setup_seccomp(void)
{
    struct sock_filter filter[] = {
        /* Load the syscall number (32-bit word at offset 0 in the
         * seccomp data structure).                                       */
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 0),

        /* If syscall >= 0xF000 → jump to TRAP (skip 1 instruction)
         *   JGE true (>= 0xF000): jt=1 → skip ALLOW → hit TRAP
         *   JGE false (<  0xF000): jf=0 → no skip → hit ALLOW    */
        BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0xF000, 1, 0),

        /* syscall < 0xF000 → ALLOW */
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),

        /* syscall >= 0xF000 → TRAP (sends SIGSYS) */
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
    };

    struct sock_fprog prog = {
        .len    = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
        .filter = filter,
    };

    /* prctl(PR_SET_NO_NEW_PRIVS) is required before loading seccomp */
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        perror("prctl(PR_SET_NO_NEW_PRIVS)");
        return -1;
    }

    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) != 0) {
        perror("prctl(PR_SET_SECCOMP)");
        return -1;
    }

    return 0;
}
