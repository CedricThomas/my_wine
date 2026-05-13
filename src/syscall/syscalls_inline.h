/*
 * syscalls_inline.h - Inline syscall macros (x86_64 and i386)
 *
 * For 64-bit builds: uses "syscall" instruction, x86_64 register constraints,
 *   and 64-bit syscall numbers.
 * For 32-bit builds: uses "int $0x80", i386 register constraints,
 *   and 32-bit syscall numbers.
 *
 * The macro interface is identical across architectures - the only difference
 * is the inline assembly and syscall numbers.
 */

#ifndef MY_WINE_SYSCALLS_INLINE_H
#define MY_WINE_SYSCALLS_INLINE_H

#if defined(__i386__)

/* ────────────────────────────────────────────────────────────────
 * 32-bit x86 (int $0x80) syscall interface
 * ──────────────────────────────────────────────────────────────── */

#include <asm/unistd_32.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

/* mmap2 via dedicated assembly wrapper - avoids musl syscall() which needs gs:[0x10]
   (not initialized by our custom _start). Uses synct_mmap2() which does int $0x80
   directly and handles the ebp (page_offset) argument without clobbering GCC's frame. */
extern void *synct_mmap2(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
#define INLINE_SYSCALL_MMAP(addr, len, prot, flags, fd, offset) \
    synct_mmap2(addr, (size_t)(len), (prot), (flags), (fd), (off_t)(offset))

#define INLINE_SYSCALL_MPROTECT(addr, len, prot) \
    ({ \
        long _synct_eax; \
        __asm__ volatile("int $0x80" \
            : "=a"(_synct_eax) \
            : "a"(__NR_mprotect), "b"(addr), "c"((size_t)(len)), "d"(prot) \
            : "cc"); \
        _synct_eax; \
    })

#define INLINE_SYSCALL_MUNMAP(addr, len) \
    ({ \
        long _synct_eax; \
        __asm__ volatile("int $0x80" \
            : "=a"(_synct_eax) \
            : "a"(__NR_munmap), "b"(addr), "c"((size_t)(len)) \
            : "cc"); \
        _synct_eax; \
    })

#define INLINE_SYSCALL_WRITE(fd, buf, len) \
    ({ \
        long _synct_eax; \
        __asm__ volatile("int $0x80" \
            : "=a"(_synct_eax) \
            : "a"(__NR_write), "b"(fd), "c"(buf), "d"((size_t)(len)) \
            : "cc", "memory"); \
        _synct_eax; \
    })

#define INLINE_SYSCALL_WRITE_ERR(msg, len) \
    do { \
        long _synct_eax; \
        __asm__ volatile("int $0x80" \
            : "=a"(_synct_eax) \
            : "a"(__NR_write), "b"(2), "c"(msg), "d"((size_t)(len)) \
            : "cc", "memory"); \
    } while (0)

#define INLINE_SYSCALL_READ(fd, buf, len) \
    ({ \
        long _synct_eax; \
        __asm__ volatile("int $0x80" \
            : "=a"(_synct_eax) \
            : "a"(__NR_read), "b"(fd), "c"(buf), "d"((size_t)(len)) \
            : "cc", "memory"); \
        _synct_eax; \
    })

#define INLINE_SYSCALL_EXIT(code) \
    do { \
        __asm__ volatile("int $0x80" \
            : \
            : "a"(__NR_exit), "b"(code) \
            : "cc"); \
        __builtin_unreachable(); \
    } while (0)

#define INLINE_SYSCALL_EXIT_GROUP(code) \
    do { \
        __asm__ volatile("int $0x80" \
            : \
            : "a"(__NR_exit_group), "b"(code) \
            : "cc"); \
        __builtin_unreachable(); \
    } while (0)

#define INLINE_SYSCALL_CLOSE(fd) \
    ({ \
        long _synct_eax; \
        __asm__ volatile("int $0x80" \
            : "=a"(_synct_eax) \
            : "a"(__NR_close), "b"(fd) \
            : "cc"); \
        _synct_eax; \
    })

/* fstat64 (197) - uses stat64 struct layout matching musl's struct stat on i386.
   __NR_fstat (108 = _newfstat) uses a different layout that corrupts st_size. */
#define INLINE_SYSCALL_FSTAT(fd, buf) \
    ({ \
        long _synct_eax; \
        __asm__ volatile("int $0x80" \
            : "=a"(_synct_eax) \
            : "a"(__NR_fstat64), "b"(fd), "c"(buf) \
            : "cc", "memory"); \
        _synct_eax; \
    })

#define INLINE_SYSCALL_NANOSLEEP(rqtp, rmtp) \
    ({ \
        long _synct_eax; \
        __asm__ volatile("int $0x80" \
            : "=a"(_synct_eax) \
            : "a"(__NR_nanosleep), "b"(rqtp), "c"(rmtp) \
            : "cc", "memory"); \
        _synct_eax; \
    })

#define INLINE_SYSCALL_CLOCK_GETTIME(clk_id, tp) \
    ({ \
        long _synct_eax; \
        __asm__ volatile("int $0x80" \
            : "=a"(_synct_eax) \
            : "a"(__NR_clock_gettime), "b"(clk_id), "c"(tp) \
            : "cc", "memory"); \
        _synct_eax; \
    })

#define INLINE_SYSCALL_GETPID() \
    ({ \
        long _synct_eax; \
        __asm__ volatile("int $0x80" \
            : "=a"(_synct_eax) \
            : "a"(__NR_getpid) \
            : "cc"); \
        _synct_eax; \
    })

#define INLINE_SYSCALL_GETTID() \
    ({ \
        long _synct_eax; \
        __asm__ volatile("int $0x80" \
            : "=a"(_synct_eax) \
            : "a"(__NR_gettid) \
            : "cc"); \
        _synct_eax; \
    })

#define INLINE_SYSCALL_KILL(pid, sig) \
    ({ \
        long _synct_eax; \
        __asm__ volatile("int $0x80" \
            : "=a"(_synct_eax) \
            : "a"(__NR_kill), "b"(pid), "c"(sig) \
            : "cc"); \
        _synct_eax; \
    })

#define INLINE_SYSCALL_SIGACTION(signum, new_acts, old_acts) \
    ({ \
        long _synct_eax; \
        __asm__ volatile("int $0x80" \
            : "=a"(_synct_eax) \
            : "a"(__NR_sigaction), "b"(signum), "c"(new_acts), "d"(old_acts) \
            : "cc", "memory"); \
        _synct_eax; \
    })

#define INLINE_SYSCALL_SIGALTSTACK(ss, old_ss) \
    ({ \
        long _synct_eax; \
        __asm__ volatile("int $0x80" \
            : "=a"(_synct_eax) \
            : "a"(__NR_sigaltstack), "b"(ss), "c"(old_ss) \
            : "cc", "memory"); \
        _synct_eax; \
    })

/*
 * clone: 32-bit uses __NR_clone (59), args: flags, newsp (stack),
 * parent_tidptr, child_tidptr, tls
 * Implemented in clone.S as wine_clone().
 */
extern long wine_clone(int flags, void *child_stack, int *parent_tid, int *child_tid, void *fn, void *arg);
#define INLINE_SYSCALL_CLONE(flags, child_stack, parent_tid, child_tid, fn, arg) \
    wine_clone(flags, child_stack, parent_tid, child_tid, fn, arg)

/*
 * set_thread_area (syscall 243) - creates an LDT entry pointing to a base
 * address and returns the allocated selector. Used instead of arch_prctl
 * for FS/GS setup in 32-bit mode on a 64-bit kernel (where arch_prctl
 * returns EINVAL for ARCH_SET_FS/ARCH_SET_GS).
 *
 * ldt_desc points to a modify_ldt_ldt_s struct (see below).
 * Returns the LDT entry number on success, or -errno on failure.
 * On success, the selector is (entry_number << 3) | 3.
 */
struct modify_ldt_ldt_s {
    unsigned int entry_number;
    unsigned int base_addr;
    unsigned int limit;
    unsigned int seg_32bit      : 1;
    unsigned int contents        : 2;
    unsigned int read_exec_only  : 1;
    unsigned int limit_in_pages  : 1;
    unsigned int seg_not_present : 1;
    unsigned int usable          : 1;
    unsigned int garbage         : 25;
};

#define INLINE_SYSCALL_SET_THREAD_AREA(ldt_desc) \
    ({ \
        long _synct_eax; \
        __asm__ volatile("int $0x80" \
            : "=a"(_synct_eax) \
            : "a"(243), "b"(ldt_desc) \
            : "cc", "memory"); \
        _synct_eax; \
    })

/* arch_prctl: available in both 32-bit and 64-bit tables (kept for reference) */
#define INLINE_SYSCALL_ARCH_PRCTL(code, addr) \
    ({ \
        long _synct_eax; \
        __asm__ volatile("int $0x80" \
            : "=a"(_synct_eax) \
            : "a"(__NR_arch_prctl), "b"(code), "c"(addr) \
            : "cc"); \
        _synct_eax; \
    })

/* openat: 32-bit openat (313) uses __SYSCALL_64_i386 with mode as stack pointer.
   Since we always pass AT_FDCWD, use __NR_open (5) instead — same effect,
   takes mode directly in edx (standard 3-arg convention, no stack pointer needed). */
#define INLINE_SYSCALL_OPENAT(dirfd, pathname, flags, mode) \
    ({ \
        long _synct_eax; \
        __asm__ volatile("int $0x80" \
            : "=a"(_synct_eax) \
            : "a"(__NR_open), "b"(pathname), "c"(flags), "d"(mode) \
            : "cc", "memory"); \
        _synct_eax; \
    })

/* unlinkat (syscall 313 on i386) */
#define INLINE_SYSCALL_UNLINKAT(dirfd, pathname, flag) \
    ({ \
        long _synct_eax; \
        __asm__ volatile("int $0x80" \
            : "=a"(_synct_eax) \
            : "a"(__NR_unlinkat), "b"(dirfd), "c"(pathname), "d"(flag) \
            : "cc", "memory"); \
        _synct_eax; \
    })

/* mremap */
#define INLINE_SYSCALL_MREMAP(old_address, old_size, new_size, flags, new_address) \
    ({ \
        long _synct_eax; \
        __asm__ volatile("int $0x80" \
            : "=a"(_synct_eax) \
            : "a"(__NR_mremap), "b"(old_address), "c"(old_size), \
              "d"(new_size), "S"(flags), "D"(new_address) \
            : "cc"); \
        (void *)_synct_eax; \
    })

#else /* __i386__ */

/* ────────────────────────────────────────────────────────────────
 * 64-bit x86_64 (syscall) interface
 * ──────────────────────────────────────────────────────────────── */

#include <asm/unistd_64.h>
#include <stddef.h>
#include <sys/mman.h>
#include <unistd.h>

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

/* ── sys_exit (do-while) ────────────────────────────────────── */

#define INLINE_SYSCALL_EXIT(code) \
    do { \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_exit), "D"(code) \
            : "rcx", "r11", "cc"); \
        __builtin_unreachable(); \
    } while (0)

/* ── sys_exit_group (do-while) ──────────────────────────────── */

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
#define INLINE_SYSCALL_OPENAT(dirfd, pathname, flags, mode) \
    ({ \
        long _synct_rax; \
        register long _r10 asm("r10") = (long)(mode); \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_openat), "D"(dirfd), "S"(pathname), "d"(flags), "r"(_r10) \
            : "rcx", "r11", "memory", "cc"); \
        _synct_rax; \
    })


/* ── sys_mmap ───────────────────────────────────────────────── */

#define INLINE_SYSCALL_MMAP(addr, len, prot, flags, fd, offset) \
    ({ \
        long _synct_rax; \
        register long _r10 asm("r10") = (long)(flags); \
        register long _r8 asm("r8") = (long)(fd); \
        register long _r9 asm("r9") = (long)(offset); \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_mmap), "D"(addr), "S"((size_t)(len)), \
              "d"(prot), "r"(_r10), "r"(_r8), "r"(_r9) \
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

/* ── sys_clock_gettime ──────────────────────────────────────── */

#define INLINE_SYSCALL_CLOCK_GETTIME(clk_id, tp) \
    ({ \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_clock_gettime), "D"(clk_id), "S"(tp) \
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

/* ── sys_gettid ─────────────────────────────────────────────── */

#define INLINE_SYSCALL_GETTID() \
    ({ \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_gettid) \
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

/* ── sys_sigaction ──────────────────────────────────────────── */

#define INLINE_SYSCALL_SIGACTION(signum, new_acts, old_acts) \
    ({ \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_sigaction), "D"(signum), "S"(new_acts), "d"(old_acts) \
            : "rcx", "r11", "memory", "cc"); \
        _synct_rax; \
    })

/* ── sys_sigaltstack ────────────────────────────────────────── */

#define INLINE_SYSCALL_SIGALTSTACK(ss, old_ss) \
    ({ \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_sigaltstack), "D"(ss), "S"(old_ss) \
            : "rcx", "r11", "memory", "cc"); \
        _synct_rax; \
    })

/* ── sys_clone ──────────────────────────────────────────────── */
/* Implemented in clone64.S as wine_clone(). */
extern long wine_clone(int flags, void *child_stack, int *parent_tid, int *child_tid, void *fn, void *arg);
#define INLINE_SYSCALL_CLONE(flags, child_stack, parent_tid, child_tid, fn, arg) \
    wine_clone(flags, child_stack, parent_tid, child_tid, fn, arg)

/* ── sys_mremap ─────────────────────────────────────────────── */

#define INLINE_SYSCALL_MREMAP(old_address, old_size, new_size, flags, new_address) \
    ({ \
        long _synct_rax; \
        register long _r10 asm("r10") = (long)(flags); \
        register long _r8 asm("r8") = (long)(new_address); \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_mremap), "D"(old_address), "S"(old_size), \
              "d"(new_size), "r"(_r10), "r"(_r8) \
            : "rcx", "r11", "cc"); \
        _synct_rax; \
    })

/* ── sys_arch_prctl ─────────────────────────────────────────── */

#define INLINE_SYSCALL_ARCH_PRCTL(code, addr) \
    ({ \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_arch_prctl), "D"(code), "S"(addr) \
            : "rcx", "r11", "cc"); \
        _synct_rax; \
    })

/* ── sys_unlinkat ───────────────────────────────────────────── */

#define INLINE_SYSCALL_UNLINKAT(dirfd, pathname, flag) \
    ({ \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_unlinkat), "D"(dirfd), "S"(pathname), "d"(flag) \
            : "rcx", "r11", "memory", "cc"); \
        _synct_rax; \
    })

#endif /* __i386__ */

#endif /* MY_WINE_SYSCALLS_INLINE_H */
