/*
 * entry.c — Guest entry point, crash handling, and __acrt_iob_func patching
 *
 * Forks a child process to run the guest PE. In the child:
 * - Sets up signal handlers and signal stack
 * - Generates syscall thunks and installs the SIGSYS dispatcher
 * - Patches __acrt_iob_func to bypass broken index math
 * - Jumps to the PE's entry point via run_guest
 *
 * In the parent: waits for the child and returns its exit code.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <asm/prctl.h>
#include <asm/unistd_64.h>
#include <fcntl.h>
#include <signal.h>
#include <ucontext.h>
#include <sys/syscall.h>
#include <sys/user.h>  // for REG_RIP, REG_RSP, etc.
#include <stdbool.h>

#include "include/pe.h"
#include "include/msvcrt.h"
#include "include/syscall/thunk_gen.h"
#include "include/syscall/signal_handler.h"
#include "include/syscall/dispatcher.h"
#include "loader_priv.h"

/* Guest entry trampoline (implemented in run_guest.S) */
extern void run_guest(void (*entry)(void), void *stack_top, void *peb,
                       char **guest_argv, char **guest_envp) __attribute__((noreturn));

/* Accessor for __wine_iob_data — used for patching __acrt_iob_func */
extern void *__wine_iob_data(void);

/**
 * SEH handler — called when an exception occurs in guest code.
 * On x86_64, SEH handlers receive (ExceptionRecord, EstablisherFrame,
 * ContextRecord, DispatcherContext) in RCX, RDX, R8, R9 per Microsoft x64 ABI.
 */
__attribute__((ms_abi, used))
static int seh_crash_handler(void *exception_record, void *establisher_frame,
                              void *context_record, void *dispatcher_context)
{
    (void)exception_record;
    (void)establisher_frame;
    (void)context_record;
    (void)dispatcher_context;

    /* Dump info via syscall (stderr) */
    { const char t[] = "SEV: SEH handler invoked (exception in guest code)\n";
      syscall(SYS_write, 2, t, sizeof(t)-1); }

    /* Extract exit code from exception record if possible, else use 0xC0000005 (ACCESS_VIOLATION) */
    uint64_t exit_code = 0xC0000005;

    /* Call NtTerminateProcess to exit cleanly */
    syscall(__NR_exit, (int)(exit_code & 0xFF));
    return 1; /* ExceptionContinueExecution (never reached) */
}

/**
 * POSIX signal crash handler. Dumps register state and exits.
 */
static void crash_handler(int sig, siginfo_t *info, void *ucontext)
{
    (void)info;
    const char *sig_name = "UNKNOWN";
    if (sig == SIGSEGV) sig_name = "SIGSEGV";
    else if (sig == SIGILL) sig_name = "SIGILL";
    else if (sig == SIGABRT) sig_name = "SIGABRT";
    else if (sig == SIGFPE) sig_name = "SIGFPE";
    else if (sig == SIGBUS) sig_name = "SIGBUS";
    else if (sig == SIGTRAP) sig_name = "SIGTRAP";

    ucontext_t *uc = (ucontext_t *)ucontext;
    if (uc) {
        greg_t *regs = uc->uc_mcontext.gregs;
        char hex_buf[256];
        int hlen;

        const char *hdr = "CRASH: ";
        syscall(SYS_write, 2, hdr, 7);
        syscall(SYS_write, 2, sig_name, strlen(sig_name));

        hlen = snprintf(hex_buf, sizeof(hex_buf),
            " RIP=0x%llx RSP=0x%llx EFL=0x%llx\n"
            " RAX=0x%llx RBX=0x%llx RCX=0x%llx RDX=0x%llx\n"
            " RSI=0x%llx RDI=0x%llx RBP=0x%llx R12=0x%llx\n"
            " R13=0x%llx R14=0x%llx R15=0x%llx\n",
            (unsigned long long)regs[REG_RIP], (unsigned long long)regs[REG_RSP], (unsigned long long)regs[REG_EFL],
            (unsigned long long)regs[REG_RAX], (unsigned long long)regs[REG_RBX], (unsigned long long)regs[REG_RCX], (unsigned long long)regs[REG_RDX],
            (unsigned long long)regs[REG_RSI], (unsigned long long)regs[REG_RDI], (unsigned long long)regs[REG_RBP], (unsigned long long)regs[REG_R12],
            (unsigned long long)regs[REG_R13], (unsigned long long)regs[REG_R14], (unsigned long long)regs[REG_R15]);
        syscall(SYS_write, 2, hex_buf, hlen);
    }

    _exit(139);
}

/**
 * Fork and jump to the PE entry point.
 *
 * In the child process:
 *   - Install signal handlers (SIGSEGV, SIGILL, etc.)
 *   - Set up alternate signal stack
 *   - Generate syscall thunks and install SIGSYS dispatcher
 *   - Re-set GS base to TEB
 *   - Patch __acrt_iob_func thunk to return __wine_iob_data directly
 *   - Jump to the PE's entry point via run_guest
 *
 * In the parent process:
 *   - Wait for child and return its exit code.
 *
 * @param  entry_abs   absolute virtual address of the PE entry point
 * @param  stack_top   top of the guest stack
 * @param  stack_base  base of the guest stack (unused, kept for ABI)
 * @param  teb         TEB pointer (GS base)
 * @param  guest_argv  argument vector for the guest
 * @param  guest_envp  environment pointer for the guest
 * @return  exit code of the child, or -1 on fork failure
 */
int jump_to_entry(uint64_t entry_abs, void *stack_top, void *stack_base, void *teb,
                  char **guest_argv, char **guest_envp)
{
    (void)stack_base;  /* suppress unused warning */
    void *peb = *(void **)((char *)teb + 0x60);
    (void)peb;  /* not passed to run_guest anymore */
    (void)guest_argv;  /* used in child below */
    (void)guest_envp;  /* used in child below */
    pid_t pid = fork();

    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {
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
        { const char t[] = "CHILD: all handlers set\n";
          syscall(SYS_write, 2, t, sizeof(t)-1); }

        /* Set up signal stack for reliable signal handling */
        {
            void *sigstack_mem = mmap(NULL, 65536, PROT_READ|PROT_WRITE,
                                      MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
            if (sigstack_mem != MAP_FAILED) {
                stack_t ss;
                ss.ss_sp = sigstack_mem;
                ss.ss_size = 65536;
                ss.ss_flags = 0;
                sigaltstack(&ss, NULL);
            }
        }

        /* Wire up SEH handler in the child's SEH frame */
        g_seh_frame[1] = (uint64_t)(uintptr_t)&seh_crash_handler;

        generate_all_thunks();
        setup_sigsys_handler(handle_syscall);
        /* setup_seccomp(); */

        fprintf(stderr, "my_wine: jumping to entry 0x%lx via inline asm\n",
                (unsigned long)entry_abs);
        fflush(stderr);

        /* Re-set GS base in child (inherited from parent but let's be sure) */
        if (syscall(__NR_arch_prctl, ARCH_SET_GS, (unsigned long)teb) != 0) {
            perror("ARCH_SET_GS");
            _exit(1);
        }
        /*
         * Patch __acrt_iob_func to return __wine_iob_data directly.
         *
         * WHY: The PE's __acrt_iob_func (msvcrt import wrapper) does:
         *   1. mov %ecx,%ebx     ; save index
         *   2. call __iob_func   ; our stub (ms_abi) may clobber rcx upper bits
         *   3. mov %ebx,%ecx     ; restore lower 32 bits (upper 32 remain garbage)
         *   4. lea (%rcx,%rcx,2),%rdx  ; rdx = rcx*3 (GARBAGE from upper bits)
         *   5. shl $4,%rdx       ; rdx *= 16 = rcx*48 (still GARBAGE)
         *   6. add %rdx,%rax     ; rax = base + garbage → wrong FILE*
         *
         * We cannot fix this from __iob_func alone because the garbage is in
         * rcx which our function doesn't control. Patching the wrapper to return
         * the base directly (bypassing the broken index math) is the cleanest fix.
         *
         * Patch: movabs $<addr>,%rax; ret; 4x NOP (15 bytes total)
         * This replaces the jmp thunk or wrapper with a direct return.
         */
        {
            uint64_t image_base = entry_abs & ~0xFFFFFUL;
            void *base = (void *)(uintptr_t)image_base;

            /* Reconstruct NT headers and section table from the live image */
            const IMAGE_DOS_HEADER *img_dos = (const IMAGE_DOS_HEADER *)base;
            uint32_t pe_off = img_dos->e_lfanew;
            IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)((char *)base + pe_off);
            uint32_t sec_off = pe_off + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                               nt->FileHeader.SizeOfOptionalHeader;
            IMAGE_SECTION_HEADER *sections =
                (IMAGE_SECTION_HEADER *)((char *)base + sec_off);

            /* Compute .text section end for bounds checking */
            uint64_t text_end = 0;
            for (uint16_t i = 0; i < nt->FileHeader.NumberOfSections; i++) {
                if (memcmp(sections[i].Name, ".text", 5) == 0) {
                    text_end = sections[i].VirtualAddress + sections[i].Misc.VirtualSize;
                    if (text_end < sections[i].VirtualAddress ||
                        sections[i].SizeOfRawData > sections[i].Misc.VirtualSize)
                        text_end = sections[i].VirtualAddress + sections[i].SizeOfRawData;
                    break;
                }
            }

            /*
             * Find the .text jmp-thunk whose IAT target resolves to __iob_func.
             * The helper scans "ff 25 disp32" instructions and checks the
             * dereferenced IAT pointer.
             */
            {
                /* Locate .text section */
                uint64_t text_start = 0, text_end2 = 0;
                for (uint16_t i = 0; i < nt->FileHeader.NumberOfSections; i++) {
                    if (memcmp(sections[i].Name, ".text", 5) == 0) {
                        text_start = sections[i].VirtualAddress;
                        text_end2   = text_start + sections[i].Misc.VirtualSize;
                        if (text_end2 < text_start || sections[i].SizeOfRawData > sections[i].Misc.VirtualSize)
                            text_end2 = text_start + sections[i].SizeOfRawData;
                        break;
                    }
                }

                uint8_t *text_base2 = (uint8_t *)base + text_start;
                uint64_t text_size2 = text_end2 - text_start;
                uint64_t target_val = (uint64_t)(uintptr_t)__iob_func;
                void *thunk = NULL;

                for (uint64_t off = 0; off + 6 <= text_size2; off++) {
                    if (text_base2[off] == 0xff && text_base2[off + 1] == 0x25) {
                        int32_t disp = *(int32_t *)(text_base2 + off + 2);
                        uint64_t instr_addr = text_start + off;
                        uint64_t target_rva = instr_addr + 6 + disp;
                        uint64_t *target_ptr = (uint64_t *)((char *)base + target_rva);
                        if (*target_ptr == target_val) {
                            thunk = (void *)((char *)base + instr_addr);
                            break;
                        }
                    }
                }

                if (thunk == NULL) {
                    fprintf(stderr, "WARNING: __acrt_iob_func thunk not found, skipping patch\n");
                } else {
                    uint8_t *code = (uint8_t *)thunk;
                    uintptr_t thunk_abs = (uintptr_t)thunk;
                    uintptr_t page_addr = thunk_abs & ~(uintptr_t)4095;
                    void *page = (void *)page_addr;

                    /* Validate: first two bytes should be ff 25 (jmp *disp32(%rip)) */
                    bool is_jmp_thunk = (code[0] == 0xff && code[1] == 0x25);
                    if (!is_jmp_thunk) {
                        fprintf(stderr, "WARNING: __acrt_iob_func at 0x%lx has unexpected opcode 0x%02x 0x%02x, skipping patch\n",
                                thunk_abs, code[0], code[1]);
                    } else {
                        /* Bounds check: ensure 15-byte patch won't exceed .text section */
                        uint64_t thunk_off = (uint64_t)thunk - (uint64_t)base;
                        if (text_end == 0 || thunk_off + 15 > text_end) {
                            fprintf(stderr, "WARNING: __acrt_iob_func thunk at 0x%lx is too close to .text end (need 15 bytes, have %ld), skipping patch\n",
                                    (unsigned long)thunk_off, (long)(text_end > thunk_off ? text_end - thunk_off : 0));
                        } else {
                            if (mprotect(page, 4096, PROT_READ|PROT_WRITE|PROT_EXEC) == 0) {
                                /* movabs $imm64, %rax */
                                code[0] = 0x48;                /* REX.W */
                                code[1] = 0xb8;                /* movabs rax, imm64 */
                                *(uint64_t *)(code + 2) = (uint64_t)(uintptr_t)__wine_iob_data();
                                /* ret */
                                code[10] = 0xc3;
                                /* NOP padding to fill 15 bytes */
                                for (int k = 11; k < 15; k++) code[k] = 0x90;

                                if (mprotect(page, 4096, PROT_READ|PROT_EXEC) != 0) {
                                    perror("mprotect restore __acrt_iob_func");
                                }
                                fprintf(stderr, "patched __acrt_iob_func at 0x%lx -> returns __wine_iob_data\n",
                                        thunk_abs);
                            } else {
                                perror("mprotect __acrt_iob_func");
                            }
                        }
                    }
                }
            }
        }

        /* Jump to the PE's AddressOfEntryPoint (mainCRTStartup) */
        {
            void (*entry)(void) = (void (*)(void))(void *)(uintptr_t)entry_abs;
            run_guest(entry, stack_top, NULL, guest_argv, guest_envp);
        }

        fprintf(stderr, "my_wine: inline jump returned\n");
        fflush(stderr);
        _exit(1);
    }

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        fprintf(stderr, "my_wine: child exited with code %d\n", code);
        return code;
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        fprintf(stderr, "my_wine: child killed by signal %d\n", sig);
        return 128 + sig;
    }

    return 0;
}
