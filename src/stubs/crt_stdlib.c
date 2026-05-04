/*
 * crt_stdlib.c — Stdlib stubs for MSVCRT.
 *
 * wine_* internal implementations + __msvcrt_* function pointer exports.
 * Also: _amsg_exit, _cexit.
 */

#define _GNU_SOURCE
#define CRT_STDLIB_C

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <asm/unistd_64.h>
#include "msvcrt_priv.h"
#include "include/abi_wrappers.h"

/* ── _amsg_exit / _cexit (called from CRT startup) ────────── */

WINE_STUB
void _amsg_exit(int msg)
{
    /* Trace: _amsg_exit called with msg=%d */
    char buf[64];
    int len = 0;
    const char p[] = "amsg_exit(msg=";
    for (int i = 0; p[i]; i++) buf[len++] = p[i];
    int tmp = msg < 0 ? -msg : msg;
    if (msg < 0) buf[len++] = '-';
    char digits[16];
    int di = 0;
    do { digits[di++] = '0' + (tmp % 10); tmp /= 10; } while (tmp > 0);
    while (di > 0) buf[len++] = digits[--di];
    buf[len++] = ')';
    buf[len++] = '\n';
    buf[len] = 0;
    long a = __NR_write;
    __asm__ volatile("syscall"
        : "+a"(a) : "D"(2), "S"(buf), "d"(len) : "rcx","r11","memory","cc");
    wine__exit(1);
}

WINE_STUB
void _cexit(void)
{
    wine__exit(0);
}

/* ── Internal implementations with unique names ────────────── */

WINE_STUB_STATIC
void wine__exit(int code)
{
    /* Call Linux sys_exit directly */
    syscall(__NR_exit, code);
    __builtin_unreachable();
}

/* Helper: format "LABEL=0xNNNNNNNNNNNNNNNN\n" and write to fd */
static void print_hex_val(int fd, const char *label, uintptr_t val)
{
    const char *hex = "0123456789abcdef";
    char buf[24];  /* "XXXX=0x0000000000000000\n\0" = 24 bytes */
    int p = 0;
    for (int i = 0; label[i]; i++) buf[p++] = label[i];
    buf[p++] = '=';
    buf[p++] = '0'; buf[p++] = 'x';
    for (int i = 60; i >= 0; i -= 4) buf[p++] = hex[(val >> i) & 0xf];
    buf[p++] = '\n';
    long a = __NR_write;
    __asm__ volatile("syscall" : "+a"(a) : "D"(fd), "S"(buf), "d"(p) : "rcx","r11","memory","cc");
}

WINE_STUB_STATIC
void wine_abort(void)
{
    /* Get RSP before we mess with anything */
    uintptr_t rsp_val;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp_val));

    /* Read return address from [rsp] */
    uintptr_t ret_addr;
    __asm__ volatile("movq (%%rsp), %0" : "=&r"(ret_addr));

    /* Read RBP */
    uintptr_t rbp_val;
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp_val));

    /* Read [rsp+8] and [rsp+16] */
    uintptr_t sp1, sp2;
    __asm__ volatile("movq 8(%%rsp), %0" : "=&r"(sp1));
    __asm__ volatile("movq 16(%%rsp), %0" : "=&r"(sp2));

    print_hex_val(2, "RET=", ret_addr);
    print_hex_val(2, "RSP=", rsp_val);
    print_hex_val(2, "RBP=", rbp_val);
    print_hex_val(2, "SP0=", ret_addr);
    print_hex_val(2, "SP1=", sp1);
    print_hex_val(2, "SP2=", sp2);

    wine__exit(134);
}

WINE_STUB_STATIC
int wine_exit(int code)
{
    wine__exit(code);
    __builtin_unreachable();
}

WINE_STUB_STATIC
void *wine_malloc(size_t size)
{
    return sysv_malloc(size);
}

WINE_STUB_STATIC
void *wine_calloc(size_t nmemb, size_t size)
{
    return sysv_calloc(nmemb, size);
}

WINE_STUB_STATIC
void wine_free(void *ptr)
{
    sysv_free(ptr);
}

WINE_STUB_STATIC
void *wine_memcpy(void *dest, const void *src, size_t n)
{
    return __builtin_memcpy(dest, src, n);
}

WINE_STUB_STATIC
size_t wine_strlen(const void *s)
{
    return __builtin_strlen(s);
}

WINE_STUB_STATIC
int wine_strncmp(const void *s1, const void *s2, size_t n)
{
    return strncmp(s1, s2, n);
}

WINE_STUB_STATIC
void wine_signal(int sig, void (*handler)(int))
{
    signal(sig, handler);
}

/* ── Expose function pointers for import table ─────────────── */

void *__msvcrt_malloc     = (void *)wine_malloc;
void *__msvcrt_calloc     = (void *)wine_calloc;
void *__msvcrt_free       = (void *)wine_free;
void *__msvcrt_memcpy     = (void *)wine_memcpy;
void *__msvcrt_strlen     = (void *)wine_strlen;
void *__msvcrt_strncmp    = (void *)wine_strncmp;
void *__msvcrt_exit       = (void *)wine_exit;
void *__msvcrt__exit      = (void *)wine__exit;
void *__msvcrt_abort      = (void *)wine_abort;
void *__msvcrt_signal     = (void *)wine_signal;
