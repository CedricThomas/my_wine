#include "include/common.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>

// ── format_hex ──────────────────────────────────────────────────

void format_hex(char *buf, int buf_size, uint64_t val) {
    static const char hex_digits[] = "0123456789abcdef";
    int i;

    if (buf_size < 17) return;  /* silently skip — would only fire on programmer error */

    for (i = 15; i >= 0; i--) {
        buf[i] = hex_digits[val & 0xF];
        val >>= 4;
    }
    buf[16] = '\0';
}

// ── format_ptr ──────────────────────────────────────────────────

void format_ptr(char *buf, int buf_size, void *p) {
    if (p == NULL) {
        if (buf_size >= 6) {
            strcpy(buf, "(nil)");
        }
    } else {
        format_hex(buf, buf_size, (uint64_t)(uintptr_t)p);
    }
}

// ── with_mprotect_rw ────────────────────────────────────────────

int with_mprotect_rw(void *addr, size_t len, void (*cb)(void *), void *cb_arg, int restore_prot) {
    void *aligned_addr = (void *)((uintptr_t)addr & ~PAGE_MASK);
    size_t total = (((uintptr_t)addr + len + PAGE_MASK) & ~PAGE_MASK) - (uintptr_t)aligned_addr;

    if (mprotect(aligned_addr, total, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        return -1;
    }

    cb(cb_arg);

    if (mprotect(aligned_addr, total, restore_prot) != 0) {
        return -1;
    }

    return 0;
}
