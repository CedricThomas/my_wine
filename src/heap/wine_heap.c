/*
 * wine_heap.c — Heap management stub implementations
 *
 * Stub implementations for HeapCreate, HeapAlloc, HeapFree, HeapReAlloc,
 * GetProcessHeap, HeapDestroy, HeapSize.
 *
 * All functions return NULL/0 as placeholders. Full implementation
 * will be provided later with dlmalloc integration.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>

#include "../stubs/kernel32_priv.h"

/* ── HeapCreate ─────────────────────────────────────────────── */
WINE_STUB
void *HeapCreate(uint32_t flOptions, uint64_t dwInitialSize, uint64_t dwMaximumSize)
{
    (void)flOptions;
    (void)dwInitialSize;
    (void)dwMaximumSize;
    return NULL;
}

/* ── HeapAlloc ──────────────────────────────────────────────── */
WINE_STUB
void *HeapAlloc(void *hHeap, uint32_t dwFlags, uint64_t dwBytes)
{
    (void)hHeap;
    (void)dwFlags;
    (void)dwBytes;
    return NULL;
}

/* ── HeapFree ───────────────────────────────────────────────── */
WINE_STUB
int HeapFree(void *hHeap, uint32_t dwFlags, void *lpMem)
{
    (void)hHeap;
    (void)dwFlags;
    (void)lpMem;
    return 0;
}

/* ── HeapReAlloc ────────────────────────────────────────────── */
WINE_STUB
void *HeapReAlloc(void *hHeap, uint32_t dwFlags, void *lpMem, uint64_t dwBytes)
{
    (void)hHeap;
    (void)dwFlags;
    (void)lpMem;
    (void)dwBytes;
    return NULL;
}

/* ── GetProcessHeap ─────────────────────────────────────────── */
WINE_STUB
void *GetProcessHeap(void)
{
    return NULL;
}

/* ── HeapDestroy ────────────────────────────────────────────── */
WINE_STUB
int HeapDestroy(void *hHeap)
{
    (void)hHeap;
    return 0;
}

/* ── HeapSize ───────────────────────────────────────────────── */
WINE_STUB
uint64_t HeapSize(void *hHeap, uint32_t dwFlags, const void *lpMem)
{
    (void)hHeap;
    (void)dwFlags;
    (void)lpMem;
    return 0;
}
