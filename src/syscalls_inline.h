/*
 * syscalls_inline.h — Inline syscall macros
 *
 * Replaces all __asm__ volatile("syscall"...) occurrences across the
 * codebase. Each macro exactly replicates the original register constraints
 * and clobber lists to preserve behavior.
 *
 * Each macro expands to a do-while(0) block that declares a unique local
 * variable for the RAX output, so callers can capture the return value:
 *   long res = INLINE_SYSCALL_WRITE(fd, buf, len);
 * Or discard it:
 *   INLINE_SYSCALL_WRITE_ERR(msg, len);
 */

#ifndef MY_WINE_SYSCALLS_INLINE_H
#define MY_WINE_SYSCALLS_INLINE_H

#include <asm/unistd_64.h>
#include <stddef.h>

/* ── Write to stderr (fd 2) ─────────────────────────────────── */

#define INLINE_SYSCALL_WRITE_ERR(msg, len) \
    do { \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_write), "D"(2), "S"(msg), "d"((size_t)(len)) \
            : "rcx", "r11", "memory", "cc"); \
    } while (0)

/* ── Write to arbitrary fd ──────────────────────────────────── */

#define INLINE_SYSCALL_WRITE(fd, buf, len) \
    do { \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_write), "D"(fd), "S"(buf), "d"((size_t)(len)) \
            : "rcx", "r11", "memory", "cc"); \
        _synct_rax; \
    } while (0)

/* ── Read from fd ───────────────────────────────────────────── */

#define INLINE_SYSCALL_READ(fd, buf, len) \
    do { \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_read), "D"(fd), "S"(buf), "d"((size_t)(len)) \
            : "rcx", "r11", "memory", "cc"); \
        _synct_rax; \
    } while (0)

/* ── sys_exit (noreturn) ────────────────────────────────────── */

#define INLINE_SYSCALL_EXIT(code) \
    do { \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_exit), "D"(code) \
            : "rcx", "r11", "cc"); \
        __builtin_unreachable(); \
    } while (0)

/* ── sys_exit_group (noreturn) ──────────────────────────────── */

#define INLINE_SYSCALL_EXIT_GROUP(code) \
    do { \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_exit_group), "D"(code) \
            : "rcx", "r11", "cc"); \
        __builtin_unreachable(); \
    } while (0)

/* ── sys_close ──────────────────────────────────────────────── */

#define INLINE_SYSCALL_CLOSE(fd) \
    do { \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_close), "D"(fd) \
            : "rcx", "r11", "cc"); \
        _synct_rax; \
    } while (0)

/* ── sys_openat ─────────────────────────────────────────────── */

#define INLINE_SYSCALL_OPENAT(dirfd, pathname, flags) \
    do { \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_openat), "D"(dirfd), "S"(pathname), "d"(flags) \
            : "rcx", "r11", "memory", "cc"); \
        _synct_rax; \
    } while (0)

/* ── sys_munmap ─────────────────────────────────────────────── */

#define INLINE_SYSCALL_MUNMAP(addr, len) \
    do { \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_munmap), "D"(addr), "S"((size_t)(len)) \
            : "rcx", "r11", "cc"); \
        _synct_rax; \
    } while (0)

/* ── sys_fstat ──────────────────────────────────────────────── */

#define INLINE_SYSCALL_FSTAT(fd, buf) \
    do { \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_fstat), "D"(fd), "S"(buf) \
            : "rcx", "r11", "memory", "cc"); \
        _synct_rax; \
    } while (0)

/* ── sys_nanosleep ──────────────────────────────────────────── */

#define INLINE_SYSCALL_NANOSLEEP(rqtp, rmtp) \
    do { \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_nanosleep), "D"(rqtp), "S"(rmtp) \
            : "rcx", "r11", "memory", "cc"); \
        _synct_rax; \
    } while (0)

/* ── sys_getpid ─────────────────────────────────────────────── */

#define INLINE_SYSCALL_GETPID() \
    do { \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_getpid) \
            : "rcx", "r11", "cc"); \
        _synct_rax; \
    } while (0)

#endif /* MY_WINE_SYSCALLS_INLINE_H */
