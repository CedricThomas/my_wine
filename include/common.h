#ifndef MY_WINE_COMMON_H
#define MY_WINE_COMMON_H

#if !defined(__x86_64__) && !defined(__i386__)
#error "my_wine only supports x86_64 and x86 architectures"
#endif

#include <stdint.h>
#include <stddef.h>
#include "debug.h"

/* Global debug flag: set from envp in main() */
extern int g_debug_enabled;

/* PE32 (32-bit) flag: set by image_mapper, read by loader/heap/CRT modules */
extern int g_is_32bit;

/* Cached WINE_DLL_PATH from environ, set in main() before GS switch */
#define WINE_DLL_PATH_MAX 512
extern char g_wine_dll_path[WINE_DLL_PATH_MAX];
void set_wine_dll_path(const char *path);

// Memory page constants
#define PAGE_SIZE         4096
#define PAGE_MASK         (PAGE_SIZE - 1)
#define PAGE_ALIGN_MASK   (~(uint64_t)PAGE_MASK)

// x86_64 opcode constants
#define X86_JMP_RIP         0xFF
#define X86_MOD_RIP         0x25   // jmp *disp32(%rip) (PE32+)
#define X86_MOD_ABS         0x15   // jmp *disp32 (PE32 absolute indirect)
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

// Windows pseudo-handle values
// Note: These must use unsigned literals so they zero-extend to uint64_t
// on 32-bit (where the dispatcher zero-extends the 32-bit stack value).
// 0xFFFFFFFF as int = -1, sign-extends to 0xFFFFFFFFFFFFFFFF as uint64_t.
// 0xFFFFFFFFU is unsigned int = 0x00000000FFFFFFFF when promoted to uint64_t.
#define HANDLE_CURRENT_PROCESS  ((uint64_t)0xFFFFFFFF)
#define STD_INPUT_HANDLE_VALUE  ((uint64_t)0x7FFFFFFF)
#define STD_OUTPUT_HANDLE_VALUE ((uint64_t)0x7FFFFFFE)
#define STD_ERROR_HANDLE_VALUE  ((uint64_t)0x7FFFFFFD)

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
