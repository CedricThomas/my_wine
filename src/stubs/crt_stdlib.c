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
#include "msvcrt_priv.h"

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
    long a = 1;
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
    syscall(60, code);
    __builtin_unreachable();
}

WINE_STUB_STATIC
void wine_abort(void)
{
    /* Dump registers to stack, then write them via syscall */
    const char *hex = "0123456789abcdef";

    /* Get RSP before we mess with anything */
    uintptr_t rsp_val;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp_val));

    /* Read return address from [rsp] */
    uintptr_t ret_addr;
    __asm__ volatile("movq (%%rsp), %0" : "=&r"(ret_addr));

    /* Read RBP */
    uintptr_t rbp_val;
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp_val));

    /* Return address */
    {
        char h[22] = "RET=";
        int p = 4;
        uintptr_t v = ret_addr;
        for (int i = 60; i >= 0; i -= 4) h[p++] = hex[(v >> i) & 0xf];
        h[p++] = '\n'; h[p] = '\0';
        long rr = 1;
        __asm__ volatile("syscall" : "+a"(rr) : "D"(2), "S"(h), "d"(p) : "rcx","r11","memory","cc");
    }

    /* RSP */
    {
        char h[22] = "RSP=";
        int p = 4;
        uintptr_t v = rsp_val;
        for (int i = 60; i >= 0; i -= 4) h[p++] = hex[(v >> i) & 0xf];
        h[p++] = '\n'; h[p] = '\0';
        long rr = 1;
        __asm__ volatile("syscall" : "+a"(rr) : "D"(2), "S"(h), "d"(p) : "rcx","r11","memory","cc");
    }

    /* RBP */
    {
        char h[22] = "RBP=";
        int p = 4;
        uintptr_t v = rbp_val;
        for (int i = 60; i >= 0; i -= 4) h[p++] = hex[(v >> i) & 0xf];
        h[p++] = '\n'; h[p] = '\0';
        long rr = 1;
        __asm__ volatile("syscall" : "+a"(rr) : "D"(2), "S"(h), "d"(p) : "rcx","r11","memory","cc");
    }

    /* Dump 8 stack values from rsp */
    {
        char h[22] = "SP0=";
        int p = 4;
        uintptr_t v = ret_addr;
        for (int i = 60; i >= 0; i -= 4) h[p++] = hex[(v >> i) & 0xf];
        h[p++] = '\n'; h[p] = '\0';
        long rr = 1;
        __asm__ volatile("syscall" : "+a"(rr) : "D"(2), "S"(h), "d"(p) : "rcx","r11","memory","cc");
    }

    /* Read [rsp+8] */
    { uintptr_t v2; __asm__ volatile("movq 8(%%rsp), %0" : "=&r"(v2));
      char h[22] = "SP1="; int p = 4;
      for (int i = 60; i >= 0; i -= 4) h[p++] = hex[(v2 >> i) & 0xf];
      h[p++] = '\n'; h[p] = '\0';
      long rr = 1;
      __asm__ volatile("syscall" : "+a"(rr) : "D"(2), "S"(h), "d"(p) : "rcx","r11","memory","cc");
    }

    /* Read [rsp+16] */
    { uintptr_t v2; __asm__ volatile("movq 16(%%rsp), %0" : "=&r"(v2));
      char h[22] = "SP2="; int p = 4;
      for (int i = 60; i >= 0; i -= 4) h[p++] = hex[(v2 >> i) & 0xf];
      h[p++] = '\n'; h[p] = '\0';
      long rr = 1;
      __asm__ volatile("syscall" : "+a"(rr) : "D"(2), "S"(h), "d"(p) : "rcx","r11","memory","cc");
    }

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
    return __builtin_malloc(size);
}

WINE_STUB_STATIC
void *wine_calloc(size_t nmemb, size_t size)
{
    return __builtin_calloc(nmemb, size);
}

WINE_STUB_STATIC
void wine_free(void *ptr)
{
    __builtin_free(ptr);
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
