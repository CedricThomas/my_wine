/*
 * module_list.c — Module registry for tracking loaded PE images
 *
 * No glibc dependencies: all string/memory ops are hand-rolled.
 * Suitable for calling from WINE_STUB context on guest stack.
 */

#include "module_list.h"

loaded_module_t module_list[MAX_MODULES];
int module_count = 0;

/* ── Hand-rolled helpers (no glibc) ─────────────────────────────── */

static void dll_memset(void *ptr, int c, size_t n)
{
    uint8_t *p = (uint8_t *)ptr;
    size_t i;
    for (i = 0; i < n; i++)
        p[i] = (uint8_t)c;
}

static void dll_copy_str(char *dst, const char *src, size_t max_len)
{
    size_t i;
    for (i = 0; i < max_len - 1 && src[i] != '\0'; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static int dll_strcasecmp(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (int)ca - (int)cb;
        a++; b++;
    }
    unsigned char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca += 32;
    if (cb >= 'A' && cb <= 'Z') cb += 32;
    return (int)ca - (int)cb;
}

/* ── Public API ─────────────────────────────────────────────────── */

void init_module_list(void)
{
    dll_memset(module_list, 0, sizeof(module_list));
    module_count = 0;
}

loaded_module_t *add_module(void *base, const char *name, IMAGE_NT_HEADERS64 *nt)
{
    int i;
    for (i = 0; i < MAX_MODULES; i++) {
        if (module_list[i].base == NULL) {
            loaded_module_t *m = &module_list[i];
            dll_memset(m, 0, sizeof(loaded_module_t));
            m->base = base;
            dll_copy_str(m->name, name, sizeof(m->name));
            m->nt = nt;
            m->load_count = 1;
            m->ldr_linked = 0;
            module_count++;
            return m;
        }
    }
    return NULL;
}

loaded_module_t *find_module_by_name(const char *name)
{
    int i;
    for (i = 0; i < MAX_MODULES; i++) {
        if (module_list[i].base != NULL &&
            dll_strcasecmp(module_list[i].name, name) == 0) {
            return &module_list[i];
        }
    }
    return NULL;
}

/* Safe version — does not use strcasecmp (no glibc/vDSO access).
 * Same as find_module_by_name but guaranteed syscall-safe. */
loaded_module_t *find_module_by_name_safe(const char *name)
{
    int i;
    for (i = 0; i < MAX_MODULES; i++) {
        const char *a = module_list[i].name;
        const char *b = name;
        if (a == NULL || b == NULL) continue;
        while (*a && *b) {
            unsigned char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) break;
            a++; b++;
        }
        if (*a == '\0' && *b == '\0') return &module_list[i];
    }
    return NULL;
}

loaded_module_t *find_module_by_addr(void *addr)
{
    uintptr_t a = (uintptr_t)addr;
    int i;
    for (i = 0; i < MAX_MODULES; i++) {
        if (module_list[i].base != NULL && module_list[i].nt != NULL) {
            uintptr_t base = (uintptr_t)module_list[i].base;
            uintptr_t end = base + module_list[i].nt->OptionalHeader.SizeOfImage;
            if (a >= base && a < end) {
                return &module_list[i];
            }
        }
    }
    return NULL;
}

void remove_module(loaded_module_t *mod)
{
    int i;
    for (i = 0; i < MAX_MODULES; i++) {
        if (&module_list[i] == mod) {
            dll_memset(&module_list[i], 0, sizeof(loaded_module_t));
            module_count--;
            return;
        }
    }
}
