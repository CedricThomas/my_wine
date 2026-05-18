#include "include/common.h"
#include "loader/loader_state.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>

/* Set from MY_WINE_DEBUG_LEVEL before guest handoff. */
int g_debug_level = 0;

wine_loader_state_t g_loader = { .dll_base_next = DLL_ALLOC_BASE };

/* Populated in main() before GS switch so find_dll_path is syscall-safe. */
char g_wine_dll_path[WINE_DLL_PATH_MAX] = {0};

void loader_set_pe_path(const char *path)
{
    if (path == NULL) {
        g_loader.pe_path[0] = '\0';
        return;
    }

    strncpy(g_loader.pe_path, path, sizeof(g_loader.pe_path) - 1);
    g_loader.pe_path[sizeof(g_loader.pe_path) - 1] = '\0';
}

void set_wine_dll_path(const char *path)
{
    if (path == NULL) {
        g_wine_dll_path[0] = '\0';
        return;
    }
    strncpy(g_wine_dll_path, path, sizeof(g_wine_dll_path) - 1);
    g_wine_dll_path[sizeof(g_wine_dll_path) - 1] = '\0';
}

static int debug_enabled(void) { return g_debug_level != 0; }
static int debug_level(void) { return g_debug_level; }
int (*debug_check_fn)(void) = &debug_enabled;
int (*debug_level_fn)(void) = &debug_level;

int parse_debug_level(const char *value)
{
    int level = 0;

    if (value == NULL) {
        return 0;
    }

    while (*value >= '0' && *value <= '9') {
        level = (level * 10) + (*value - '0');
        if (level > 9) {
            return 9;
        }
        value++;
    }

    return level;
}

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
