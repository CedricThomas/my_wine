/*
 * import_lookup.c -- Loader import symbol lookup helpers
 *
 * This keeps stub-table and loaded-module export lookup separate from the
 * PE import walking logic in import_resolve.c.
 */

#include <stddef.h>
#include <stdint.h>

#include "include/common.h"
#include "include/syscall_safe_utils.h"
#include "../syscall/syscalls_inline.h"

#include "import_lookup.h"
#include "loader_priv.h"
#include "export_table.h"
#include "module_list.h"

#if defined(MY_WINE32)
static int import_name_equal(const char *a, const char *b)
{
    unsigned char ua, ub;

    if (a == NULL || b == NULL)
        return 0;

    while (*a != '\0' && *b != '\0') {
        ua = (unsigned char)*a;
        ub = (unsigned char)*b;
        if (ua >= 'A' && ua <= 'Z')
            ua = (unsigned char)(ua - 'A' + 'a');
        if (ub >= 'A' && ub <= 'Z')
            ub = (unsigned char)(ub - 'A' + 'a');
        if (ua != ub)
            return 0;
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}
#endif

static void debug_resolved_import(const char *dll_name,
                                  const char *func_name,
                                  uintptr_t addr)
{
    char buf[256];
    int i = 0;
    const char *p;

    if (g_debug_level < 2)
        return;

    for (p = "resolve_import: "; *p && i < 250; )
        buf[i++] = *p++;
    for (p = dll_name; *p && i < 250; )
        buf[i++] = *p++;
    if (i < 250)
        buf[i++] = '!';
    for (p = func_name; *p && i < 250; )
        buf[i++] = *p++;
    if (i < 250)
        buf[i++] = ' ';
    if (i < 250)
        buf[i++] = '-';
    if (i < 250)
        buf[i++] = '>';
    if (i < 250)
        buf[i++] = ' ';

    if (addr == 0) {
        static const char not_found[] = "(null)";

        for (p = not_found; *p && i < 255; )
            buf[i++] = *p++;
    } else {
        if (i < 250)
            buf[i++] = '0';
        if (i < 250)
            buf[i++] = 'x';
        syscall_safe_format_hex(buf + i, addr, 8);
        i += 8;
    }

    if (i < 255)
        buf[i++] = '\n';
    INLINE_SYSCALL_WRITE(2, buf, i);
}

void *resolve_loader_import(const char *dll_name, const char *func_name)
{
    import_entry_t *entry = NULL;

    /* Tier 1: lookup in our stub import table */
#if defined(MY_WINE32)
    /* 32-bit: linear scan (avoid sort/bsearch dependency issues).
     * Match both DLL and function name because many Win32 exports share the
     * same symbol across DLL spellings / duplicate table entries. */
    for (size_t i = 0; i < import_table_count; i++) {
        if (!import_name_equal(dll_name, import_table[i].dll_name))
            continue;
        if (import_cmp_by_name(func_name, &import_table[i]) == 0) {
            entry = &import_table[i];
            break;
        }
    }
#else
    /* 64-bit: hand-rolled binary search */
    size_t lo = 0, hi = import_table_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = import_cmp_by_name(func_name, &import_table[mid]);
        if (cmp < 0) {
            hi = mid;
        } else if (cmp > 0) {
            lo = mid + 1;
        } else {
            entry = &import_table[mid];
            break;
        }
    }
#endif
    if (entry != NULL && entry->address != NULL) {
        debug_resolved_import(dll_name, func_name, (uintptr_t)entry->address);
        return entry->address;
    }

    /* Tier 2: lookup in loaded module exports */
    loaded_module_t *mod = find_module_by_name(dll_name);
    if (mod != NULL && mod->export_cache.number_of_names > 0) {
        void *addr = lookup_export(mod, func_name);
        if (addr != NULL) {
            debug_resolved_import(dll_name, func_name, (uintptr_t)addr);
            return addr;
        }
    }

    /* Tier 3: not found */
    debug_resolved_import(dll_name, func_name, 0);
    return NULL;
}
