/*
 * user32_class_registry.c
 *
 * Owns the registered WNDCLASSA table used by RegisterClassA and window
 * creation. This keeps class bookkeeping separate from window lifecycle and
 * backend policy.
 */

#include "user32_priv.h"
#include "include/debug.h"
#ifdef MY_WINE32
#include "include/kernel32.h"
#endif

typedef struct {
    WNDCLASSA wndclass;
} user32_class_entry;

static user32_class_entry *g_class_registry = NULL;
static int g_class_count = 0;
static int g_class_capacity = 0;

static user32_class_entry *get_class_entry(int idx)
{
    if (idx < 0 || idx >= g_class_count)
        return NULL;
    return &g_class_registry[idx];
}

static void *user32_alloc(size_t size)
{
#ifdef MY_WINE32
    void *heap = GetProcessHeap();
    if (!heap)
        return NULL;
    return HeapAlloc(heap, 0, size);
#else
    return malloc(size);
#endif
}

static void user32_free(void *ptr)
{
#ifdef MY_WINE32
    void *heap = GetProcessHeap();
    if (!ptr)
        return;
    if (!heap)
        return;
    HeapFree(heap, 0, ptr);
#else
    free(ptr);
#endif
}

static char *user32_strdup(const char *src)
{
    size_t len;
    char *copy;

    if (!src)
        return NULL;

    len = user32_strlen(src) + 1;
    copy = user32_alloc(len);
    if (!copy)
        return NULL;

    user32_memcpy(copy, src, len);
    return copy;
}

static int ensure_class_capacity(int needed_count)
{
    user32_class_entry *new_registry;
    int new_capacity;

    if (needed_count <= g_class_capacity)
        return TRUE;

    new_capacity = g_class_capacity ? g_class_capacity * 2 : 16;
    while (new_capacity < needed_count)
        new_capacity *= 2;

    new_registry = user32_alloc((size_t)new_capacity * sizeof(*new_registry));
    if (!new_registry)
        return FALSE;

    if (g_class_registry && g_class_count > 0) {
        user32_memcpy(new_registry, g_class_registry,
                      (size_t)g_class_count * sizeof(*new_registry));
    }

    user32_free(g_class_registry);
    g_class_registry = new_registry;
    g_class_capacity = new_capacity;
    return TRUE;
}

static int find_class_index(const char *name)
{
    int i;
    for (i = 0; i < g_class_count; i++) {
        const char *class_name = g_class_registry[i].wndclass.lpszClassName;

        if (class_name && user32_strcmp(class_name, name) == 0)
            return i;
    }
    return -1;
}

KERNEL32_STUB
ATOM RegisterClassA(const WNDCLASSA *lpWndClass)
{
    char *class_name_copy;
    user32_class_entry *entry;
    int idx;

    DEBUG_LEVEL(1, "user32: RegisterClassA class=%s wndproc=%p",
                (lpWndClass && lpWndClass->lpszClassName) ? lpWndClass->lpszClassName : "(null)",
                lpWndClass ? lpWndClass->lpfnWndProc : NULL);
    if (!lpWndClass || !lpWndClass->lpszClassName)
        return 0;

    idx = find_class_index(lpWndClass->lpszClassName);
    if (idx >= 0)
        return (ATOM)(idx + 1);

    if (!ensure_class_capacity(g_class_count + 1))
        return 0;

    class_name_copy = user32_strdup(lpWndClass->lpszClassName);
    if (!class_name_copy)
        return 0;

    entry = &g_class_registry[g_class_count];
    user32_memcpy(&entry->wndclass, lpWndClass, sizeof(entry->wndclass));
    entry->wndclass.lpszClassName = class_name_copy;

    g_class_count++;
    DEBUG_LEVEL(1, "user32: RegisterClassA success atom=%d", g_class_count);
    return (ATOM)g_class_count;
}

const WNDCLASSA *user32_find_registered_class(const char *name, ATOM *atom_out)
{
    int idx = find_class_index(name);
    user32_class_entry *entry;

    if (idx < 0)
        return NULL;

    entry = get_class_entry(idx);
    if (!entry)
        return NULL;

    if (atom_out)
        *atom_out = (ATOM)(idx + 1);
    return &entry->wndclass;
}
