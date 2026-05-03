/*
 * syscall_registry.h — Declarative syscall registration macros
 *
 * Provides DEFINE_SYSCALL* macros that generate dispatcher switch-case
 * entries.  Use inside the switch body of handle_syscall() to turn
 * the raw "case 0xNN: handler(); break;" boilerplate into a one-liner.
 *
 * Full auto-generation would require a build-time code generator that
 * introspects the handler function signatures and inserts the
 * argument-decoding / guest-ptr validation logic.  These macros are a
 * lightweight alternative that still eliminates the repetitive switch
 * structure while keeping the hand-tuned validation in the caller.
 *
 * Usage example (inside the switch in handle_syscall()):
 *
 *     switch (nt_nr) {
 *         DEFINE_SYSCALL(0x05, handler_NtCallbackReturn)
 *         DEFINE_SYSCALL_ARGS1(0x0F, handler_NtClose, arg1)
 *         DEFINE_SYSCALL_ARGS2(0x30, handler_NtTerminateProcess, arg1, arg2)
 *         ...
 *     }
 *
 * For handlers that need guest-space pointer validation (the majority),
 * the full hand-written case block is still required.  The macro variants
 * are intended for the simple subset where no validation is needed.
 */

#ifndef SYSCALL_REGISTRY_H
#define SYSCALL_REGISTRY_H

/* ── Simple no-argument handler ─────────────────────────────── */

/*
 * DEFINE_SYSCALL(nr, handler) — call handler with no arguments.
 *   nr      — NT syscall number (after stripping 0xF000 offset)
 *   handler — function pointer or function name returning uint64_t
 */
#define DEFINE_SYSCALL(nr, handler) \
    case (nr): { result = (handler)(); break; }

/* ── 1-argument handler ─────────────────────────────────────── */

/*
 * DEFINE_SYSCALL_ARGS1(nr, handler, a1) — call handler with one argument.
 *   nr      — NT syscall number
 *   handler — function returning uint64_t
 *   a1      — expression for the first argument (e.g. arg1)
 */
#define DEFINE_SYSCALL_ARGS1(nr, handler, a1) \
    case (nr): { result = (handler)(a1); break; }

/* ── 2-argument handler ─────────────────────────────────────── */

/*
 * DEFINE_SYSCALL_ARGS2(nr, handler, a1, a2) — call handler with two arguments.
 */
#define DEFINE_SYSCALL_ARGS2(nr, handler, a1, a2) \
    case (nr): { result = (handler)(a1, a2); break; }

/* ── 3-argument handler ─────────────────────────────────────── */

/*
 * DEFINE_SYSCALL_ARGS3(nr, handler, a1, a2, a3) — call handler with three arguments.
 */
#define DEFINE_SYSCALL_ARGS3(nr, handler, a1, a2, a3) \
    case (nr): { result = (handler)(a1, a2, a3); break; }

/* ── 4-argument handler ─────────────────────────────────────── */

/*
 * DEFINE_SYSCALL_ARGS4(nr, handler, a1, a2, a3, a4) — call handler with four arguments.
 */
#define DEFINE_SYSCALL_ARGS4(nr, handler, a1, a2, a3, a4) \
    case (nr): { result = (handler)(a1, a2, a3, a4); break; }

/* ── 5-argument handler ─────────────────────────────────────── */

/*
 * DEFINE_SYSCALL_ARGS5(nr, handler, a1, a2, a3, a4, a5) — call handler with five arguments.
 */
#define DEFINE_SYSCALL_ARGS5(nr, handler, a1, a2, a3, a4, a5) \
    case (nr): { result = (handler)(a1, a2, a3, a4, a5); break; }

/* ── 6-argument handler ─────────────────────────────────────── */

/*
 * DEFINE_SYSCALL_ARGS6(nr, handler, a1, a2, a3, a4, a5, a6) — call handler with six arguments.
 */
#define DEFINE_SYSCALL_ARGS6(nr, handler, a1, a2, a3, a4, a5, a6) \
    case (nr): { result = (handler)(a1, a2, a3, a4, a5, a6); break; }

/* ── 7-argument handler ─────────────────────────────────────── */

/*
 * DEFINE_SYSCALL_ARGS7(nr, handler, a1, a2, a3, a4, a5, a6, a7) — call handler with seven arguments.
 */
#define DEFINE_SYSCALL_ARGS7(nr, handler, a1, a2, a3, a4, a5, a6, a7) \
    case (nr): { result = (handler)(a1, a2, a3, a4, a5, a6, a7); break; }

/* ── 8-argument handler ─────────────────────────────────────── */

/*
 * DEFINE_SYSCALL_ARGS8(nr, handler, a1, a2, a3, a4, a5, a6, a7, a8) — call handler with eight arguments.
 */
#define DEFINE_SYSCALL_ARGS8(nr, handler, a1, a2, a3, a4, a5, a6, a7, a8) \
    case (nr): { result = (handler)(a1, a2, a3, a4, a5, a6, a7, a8); break; }

#endif /* SYSCALL_REGISTRY_H */
