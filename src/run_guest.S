/*
 * run_guest.S - Naked entry trampoline
 *
 * Switches to the guest stack and jumps to the PE entry point
 * using the Microsoft x64 calling convention:
 *   rcx = argc, rdx = argv, r8 = envp
 *
 * After the entry function returns (exit code in RAX),
 * calls ExitProcess(exit_code) to terminate cleanly.
 *
 * C signature (sysv calling convention):
 *   rdi = entry, rsi = stack_top, rdx = peb, rcx = guest_argv,
 *   r8 = guest_envp, r9 = exit_process_fn
 *
 * Microsoft x64 ABI needs:
 *   rcx = 1 (argc), rdx = guest_argv, r8 = guest_envp
 *
 * After entry returns:
 *   rcx = RAX (exit code), call exit_process_fn
 *
 * Mapping:
 *   - stack_top (rsi) -> rsp (switch stack)
 *   - guest_argv (rcx) -> rdx (need temp, since rcx reused for argc)
 *   - guest_envp (r8)  -> r8 (already in place)
 *   - entry (rdi)      -> call target
 *   - exit_process_fn (r9) -> saved in callee-saved r15
 */

.section .text
.globl run_guest
.type run_guest, @function
.code64

run_guest:
    mov %rsi, %rsp           /* switch to guest stack */
    mov %r9, %r15            /* save exit_process_fn in r15 (callee-saved) */
    mov %rcx, %r9            /* save guest_argv (rcx will be overwritten) */
    mov $1, %rcx             /* argc = 1 (Microsoft arg1) */
    mov %r9, %rdx            /* guest_argv -> rdx (Microsoft arg2) */
    /* guest_envp already in r8 from sysv arg5 (Microsoft arg3) */
    call *%rdi               /* call entry() */
    /* RAX = return value from entry/main() */
    mov %rax, %rcx           /* uExitCode = main()'s return value */
    call *%r15               /* ExitProcess(uExitCode) */
    /* ExitProcess should not return, but just in case: */
    hlt
