#ifndef MY_WINE_COMMON_H
#define MY_WINE_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include "debug.h"

/* Global debug flag: set from envp in main() */
extern int g_debug_enabled;

/* Cached WINE_DLL_PATH from environ, set in main() before GS switch */
#define WINE_DLL_PATH_MAX 512
extern char g_wine_dll_path[WINE_DLL_PATH_MAX];

// Memory page constants
#define PAGE_SIZE         4096
#define PAGE_MASK         (PAGE_SIZE - 1)
#define PAGE_ALIGN_MASK   (~(uint64_t)PAGE_MASK)

// x86_64 opcode constants
#define X86_JMP_RIP         0xFF
#define X86_MOD_RIP         0x25
#define X86_REX_W           0x48
#define X86_MOV_R64_RIP     0x8B
#define X86_MOV_RIP         0x05
#define X86_MOV_RAX_RAX     0x00
#define X86_CALL            0xE8
#define X86_JMP_REL         0xE9
#define X86_RET             0xC3
#define X86_NOP             0x90
#define X86_MOV_ABS         0xB8
#define X86_MOV_RM_R64      0x89
#define X86_MOV_RM_IMM       0xC7
#define X86_SYSCALL_BYTE1    0x0F
#define X86_SYSCALL_BYTE2    0x05

// CRT BSS offsets
#define CRT_BSS_INITENV     0x018
#define CRT_BSS_ARGC        0x028
#define CRT_BSS_ARGV        0x020

// Windows pseudo-handle values
#define HANDLE_CURRENT_PROCESS  0xFFFFFFFF
#define STD_INPUT_HANDLE_VALUE  0x7FFFFFFF
#define STD_OUTPUT_HANDLE_VALUE 0x7FFFFFFE
#define STD_ERROR_HANDLE_VALUE  0x7FFFFFFD

// Array size limits
#define MAX_THUNK_TARGETS   256
#define MAX_FLAT_IMPORTS    256

// Signal stack size (64KB, matching original value)
#define SIG_STACK_SIZE      65536

// Exit codes for signals (128 + signal number)
#define EXIT_SIGSEGV    139   /* 128 + SIGSEGV(11) */
#define EXIT_SIGABRT    134   /* 128 + SIGABRT(6) */

// ── Shared helpers ─────────────────────────────────────────────

void format_hex(char *buf, int buf_size, uint64_t val);
void format_ptr(char *buf, int buf_size, void *p);
int with_mprotect_rw(void *addr, size_t len, void (*cb)(void *), void *cb_arg, int restore_prot);

#endif // MY_WINE_COMMON_H
