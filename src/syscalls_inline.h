/*
 * syscalls_inline.h — Inline syscall macros
 *
 * Replaces all __asm__ volatile("syscall"...) occurrences across the
 * codebase. Each macro exactly replicates the original register constraints
 * and clobber lists to preserve behavior.
 *
 * Macros that return values use GCC statement expressions: ({ ... })
 *   long res = INLINE_SYSCALL_WRITE(fd, buf, len);
 * Macros that are noreturn use do-while(0):
 *   INLINE_SYSCALL_EXIT(code);
 * Macros used as statements discard the result:
 *   INLINE_SYSCALL_WRITE_ERR(msg, len);
 */

#ifndef MY_WINE_SYSCALLS_INLINE_H
#define MY_WINE_SYSCALLS_INLINE_H

#include <asm/unistd_64.h>
#include <stddef.h>
#include <sys/mman.h>

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
    ({ \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_write), "D"(fd), "S"(buf), "d"((size_t)(len)) \
            : "rcx", "r11", "memory", "cc"); \
        _synct_rax; \
    })

/* ── Read from fd ───────────────────────────────────────────── */

#define INLINE_SYSCALL_READ(fd, buf, len) \
    ({ \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_read), "D"(fd), "S"(buf), "d"((size_t)(len)) \
            : "rcx", "r11", "memory", "cc"); \
        _synct_rax; \
    })

/* ── sys_exit (noreturn) ────────────────────────────────────── */

#define INLINE_SYSCALL_EXIT(code) \
    do { \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_exit), "D"(code) \
            : "rcx", "r11", "cc"); \
        __builtin_unreachable(); \
    } while (0)

/* ── sys_exit_group (noreturn) ──────────────────────────────── */

#define INLINE_SYSCALL_EXIT_GROUP(code) \
    do { \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_exit_group), "D"(code) \
            : "rcx", "r11", "cc"); \
        __builtin_unreachable(); \
    } while (0)

/* ── sys_close ──────────────────────────────────────────────── */

#define INLINE_SYSCALL_CLOSE(fd) \
    ({ \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_close), "D"(fd) \
            : "rcx", "r11", "cc"); \
        _synct_rax; \
    })

/* ── sys_openat ─────────────────────────────────────────────── */

#define INLINE_SYSCALL_OPENAT(dirfd, pathname, flags) \
    ({ \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_openat), "D"(dirfd), "S"(pathname), "d"(flags) \
            : "rcx", "r11", "memory", "cc"); \
        _synct_rax; \
    })

/* ── sys_mmap ────────────────────────────────────────────────── */

#define INLINE_SYSCALL_MMAP(addr, len, prot, flags, fd, offset) \
    ({ \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_mmap), "D"(addr), "S"((size_t)(len)), \
              "d"(prot), "r"(flags), "r"(fd), "r"((off_t)(offset)) \
            : "rcx", "r11", "cc"); \
        _synct_rax < 0 ? MAP_FAILED : (void *)_synct_rax; \
    })

/* ── sys_mprotect ───────────────────────────────────────────── */

#define INLINE_SYSCALL_MPROTECT(addr, len, prot) \
    ({ \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_mprotect), "D"(addr), "S"((size_t)(len)), "d"(prot) \
            : "rcx", "r11", "cc"); \
        _synct_rax; \
    })

/* ── sys_munmap ─────────────────────────────────────────────── */

#define INLINE_SYSCALL_MUNMAP(addr, len) \
    ({ \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_munmap), "D"(addr), "S"((size_t)(len)) \
            : "rcx", "r11", "cc"); \
        _synct_rax; \
    })

/* ── sys_fstat ──────────────────────────────────────────────── */

#define INLINE_SYSCALL_FSTAT(fd, buf) \
    ({ \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_fstat), "D"(fd), "S"(buf) \
            : "rcx", "r11", "memory", "cc"); \
        _synct_rax; \
    })

/* ── sys_nanosleep ──────────────────────────────────────────── */

#define INLINE_SYSCALL_NANOSLEEP(rqtp, rmtp) \
    ({ \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_nanosleep), "D"(rqtp), "S"(rmtp) \
            : "rcx", "r11", "memory", "cc"); \
        _synct_rax; \
    })

/* ── sys_getpid ─────────────────────────────────────────────── */

#define INLINE_SYSCALL_GETPID() \
    ({ \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_getpid) \
            : "rcx", "r11", "cc"); \
        _synct_rax; \
    })

/* ── sys_kill ───────────────────────────────────────────────── */

#define INLINE_SYSCALL_KILL(pid, sig) \
    ({ \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_kill), "D"(pid), "S"(sig) \
            : "rcx", "r11", "cc"); \
        _synct_rax; \
    })

/* ── sys_clone ──────────────────────────────────────────────── */

#define INLINE_SYSCALL_CLONE(flags, child_stack, parent_tid, child_tid, fn, arg) \
    ({ \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_clone), "D"(flags), "S"(child_stack), \
              "d"(parent_tid), "r"(child_tid), "r"(fn), "r"(arg) \
            : "rcx", "r11", "cc", "memory"); \
        _synct_rax; \
    })

#endif /* MY_WINE_SYSCALLS_INLINE_H */
