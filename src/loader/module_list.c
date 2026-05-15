/*
 * module_list.c — Module registry for tracking loaded PE images
 *
 * No glibc dependencies: all string/memory ops are hand-rolled.
 * Suitable for calling from WINE_STUB context on guest stack.
 */

#include "module_list.h"
#include "src/pe_priv.h"
#include "loader_utils.h"
#include "loader_state.h"

/* ── Helpers ───────────────────────────────────────────────────── */

/* Copy an ASCII string into an embedded LDR_UNICODE_STRING buffer */
static void module_make_unicode_string(LDR_UNICODE_STRING *us, const char *src)
{
    size_t len = 0;
    while (src[len]) len++;
    size_t i;

    dll_memset(us->Buffer, 0, sizeof(us->Buffer));

    for (i = 0; i < len && i < MAX_LDR_NAME_WCHAR; i++) {
        us->Buffer[i] = (uint16_t)(uint8_t)src[i];
    }

    us->Length = (uint16_t)(len * sizeof(uint16_t));
    us->MaximumLength = (uint16_t)((len + 1) * sizeof(uint16_t));
}

/* Initialize a LIST_ENTRY as a self-referencing head node */
static void module_list_entry_init(LIST_ENTRY *entry)
{
    entry->Flink = entry;
    entry->Blink = entry;
}

/* Populate the LDR_DATA_TABLE_ENTRY from module metadata */
static void module_init_ldr_entry(loaded_module_t *m, IMAGE_NT_HEADERS *nt)
{
    LDR_DATA_TABLE_ENTRY *entry = &m->ldr_entry;

    dll_memset(entry, 0, sizeof(LDR_DATA_TABLE_ENTRY));

    entry->DllBase = m->base;
    entry->EntryPoint = (char *)m->base + pe_entry_rva(nt);
    entry->SizeOfImage = pe_size_of_image(nt);
    entry->TimeDateStamp = pe_time_date_stamp(nt);
    entry->LoadCount = 1;

    module_make_unicode_string(&entry->FullDllName, m->name);
    module_make_unicode_string(&entry->BaseDllName, m->name);

    /* Initialize DoubleList nodes as self-referencing (not yet linked) */
    module_list_entry_init(&entry->DoubleList[0]);
    module_list_entry_init(&entry->DoubleList[1]);
    module_list_entry_init(&entry->DoubleList[2]);
    module_list_entry_init(&entry->HashTableEntry);
}

/* ── Public API ─────────────────────────────────────────────────── */

void init_module_list(void)
{
    dll_memset(g_loader.modules, 0, sizeof(g_loader.modules));
    g_loader.module_count = 0;
}

loaded_module_t *add_module(void *base, const char *name, IMAGE_NT_HEADERS *nt)
{
    int i;
    for (i = 0; i < MAX_MODULES; i++) {
        if (g_loader.modules[i].base == NULL) {
            loaded_module_t *m = &g_loader.modules[i];
            dll_memset(m, 0, sizeof(loaded_module_t));
            m->base = base;
            dll_copy_str(m->name, name, sizeof(m->name));
            m->nt = nt;
            m->load_count = 1;
            m->ldr_linked = 0;
            module_init_ldr_entry(m, nt);
            g_loader.module_count++;
            return m;
        }
    }
    return NULL;
}

loaded_module_t *find_module_by_name(const char *name)
{
    int i;
    for (i = 0; i < g_loader.module_count; i++) {
        if (g_loader.modules[i].base != NULL &&
            dll_strcasecmp(g_loader.modules[i].name, name) == 0) {
            return &g_loader.modules[i];
        }
    }
    return NULL;
}

/* Safe version — does not use strcasecmp (no glibc/vDSO access).
 * Same as find_module_by_name but guaranteed syscall-safe. */
loaded_module_t *find_module_by_name_safe(const char *name)
{
    int i;
    for (i = 0; i < g_loader.module_count; i++) {
        const char *a = g_loader.modules[i].name;
        const char *b = name;
        if (a == NULL || b == NULL) continue;
        while (*a && *b) {
            unsigned char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) break;
            a++; b++;
        }
        if (*a == '\0' && *b == '\0') return &g_loader.modules[i];
    }
    return NULL;
}

loaded_module_t *find_module_by_addr(void *addr)
{
    uintptr_t a = (uintptr_t)addr;
    int i;
    for (i = 0; i < g_loader.module_count; i++) {
        if (g_loader.modules[i].base != NULL && g_loader.modules[i].nt != NULL) {
            uintptr_t base = (uintptr_t)g_loader.modules[i].base;
            uintptr_t end = base + pe_size_of_image(g_loader.modules[i].nt);
            if (a >= base && a < end) {
                return &g_loader.modules[i];
            }
        }
    }
    return NULL;
}

void remove_module(loaded_module_t *mod)
{
    int i;
    for (i = 0; i < g_loader.module_count; i++) {
        if (&g_loader.modules[i] == mod) {
            dll_memset(&g_loader.modules[i], 0, sizeof(loaded_module_t));
            g_loader.module_count--;
            return;
        }
    }
}
