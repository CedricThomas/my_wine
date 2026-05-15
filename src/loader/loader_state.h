/*
 * loader_state.h — Consolidated loader global state
 *
 * All loader-wide scalar and small-array globals are collected into
 * a single struct to avoid scattered extern declarations across modules.
 * The struct is defined in common.c as `g_loader`.
 */

#ifndef MY_WINE_LOADER_STATE_H
#define MY_WINE_LOADER_STATE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#include "module_list.h"  /* loaded_module_t, MAX_MODULES */

/* DLL base allocator start address (1.5GB) to avoid GCC ms_abi truncation bug */
#define DLL_ALLOC_BASE 0x60000000

/* ── Consolidated loader state ─────────────────────────────────── */
typedef struct {
    void         *image_base;            /* Mapped base of the main PE image */
    char          pe_path[512];          /* Path to the loaded PE file */
    void         *peb_ldr;               /* PEB_LDR_DATA* (void* to avoid forward-dep cycle) */
    volatile uintptr_t dll_base_next;    /* Next DLL allocation base (atomic CAS) */
    loaded_module_t modules[MAX_MODULES]; /* Loaded module registry */
    int           module_count;           /* Number of loaded modules */
    int           is_32bit;               /* PE32 (32-bit) image flag */
    uintptr_t     host_gs_base;          /* Host GS base after switch (64-bit) */
    uint16_t      host_fs_selector;      /* Host FS selector before switch (32-bit) */
} wine_loader_state_t;

extern wine_loader_state_t g_loader;

/* ── Inline accessors ──────────────────────────────────────────── */

static inline void *loader_get_image_base(void) {
    return g_loader.image_base;
}

static inline void loader_set_image_base(void *base) {
    g_loader.image_base = base;
}

static inline const char *loader_get_pe_path(void) {
    return g_loader.pe_path;
}

static inline void loader_set_pe_path(const char *path) {
    if (path) {
        snprintf(g_loader.pe_path, sizeof(g_loader.pe_path), "%s", path);
    } else {
        g_loader.pe_path[0] = '\0';
    }
}

static inline void *loader_get_peb_ldr(void) {
    return g_loader.peb_ldr;
}

static inline void loader_set_peb_ldr(void *peb_ldr) {
    g_loader.peb_ldr = peb_ldr;
}

static inline volatile uintptr_t *loader_get_dll_base_next(void) {
    return &g_loader.dll_base_next;
}

static inline int loader_get_module_count(void) {
    return g_loader.module_count;
}

static inline void loader_set_module_count(int count) {
    g_loader.module_count = count;
}

static inline loaded_module_t *loader_get_module(int i) {
    return &g_loader.modules[i];
}

static inline loaded_module_t *loader_get_modules(void) {
    return g_loader.modules;
}

static inline bool loader_is_32bit(void) {
    return g_loader.is_32bit != 0;
}

static inline void loader_set_32bit(bool val) {
    g_loader.is_32bit = val;
}

static inline bool g_is_32bit_get(void) {
    return loader_is_32bit();
}

static inline void g_is_32bit_set(int val) {
    loader_set_32bit(val != 0);
}

static inline uintptr_t loader_get_host_gs_base(void) {
    return g_loader.host_gs_base;
}

static inline void loader_set_host_gs_base(uintptr_t base) {
    g_loader.host_gs_base = base;
}

static inline uint16_t loader_get_host_fs_selector(void) {
    return g_loader.host_fs_selector;
}

static inline void loader_set_host_fs_selector(uint16_t sel) {
    g_loader.host_fs_selector = sel;
}

#endif /* MY_WINE_LOADER_STATE_H */
