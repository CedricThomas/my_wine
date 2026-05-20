#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#include "resource_win32.h"
#include "kernel32_priv.h"
#include "include/handle_manager.h"
#include "../loader/loader_state.h"
#include "../loader/module_list.h"
#include "src/pe_priv.h"

#define IMAGE_DIRECTORY_ENTRY_RESOURCE 2

typedef struct {
    uint32_t Characteristics;
    uint32_t TimeDateStamp;
    uint16_t MajorVersion;
    uint16_t MinorVersion;
    uint16_t NumberOfNamedEntries;
    uint16_t NumberOfIdEntries;
} IMAGE_RESOURCE_DIRECTORY_WINE;

typedef struct {
    uint32_t Name;
    uint32_t OffsetToData;
} IMAGE_RESOURCE_DIRECTORY_ENTRY_WINE;

typedef struct {
    uint32_t OffsetToData;
    uint32_t Size;
    uint32_t CodePage;
    uint32_t Reserved;
} IMAGE_RESOURCE_DATA_ENTRY_WINE;

typedef struct {
    void *module_base;
    const void *data;
    uint32_t size;
} wine_resource_handle;

__attribute__((weak)) loaded_module_t *find_module_by_addr(void *addr)
{
    (void)addr;
    return NULL;
}

static int wine_resource_ascii_eq_utf16(const char *ascii, const uint16_t *wide, uint16_t len)
{
    uint16_t i;

    if (!ascii || !wide)
        return 0;

    for (i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)ascii[i];
        if (ch == '\0' || wide[i] > 0x7f || (uint16_t)ch != wide[i])
            return 0;
    }

    return ascii[len] == '\0';
}

static uint32_t wine_resource_builtin_type(const char *type)
{
    if (!type)
        return 0;
    if (type[0] == '#') {
        uint32_t value = 0;
        const char *p = type + 1;
        while (*p >= '0' && *p <= '9') {
            value = value * 10u + (uint32_t)(*p - '0');
            p++;
        }
        return value;
    }
    if (type[0] == 'D' && type[1] == 'I' && type[2] == 'A' && type[3] == 'L' &&
        type[4] == 'O' && type[5] == 'G' && type[6] == '\0')
        return 5;
    if (type[0] == 'S' && type[1] == 'T' && type[2] == 'R' && type[3] == 'I' &&
        type[4] == 'N' && type[5] == 'G' && type[6] == '\0')
        return 6;
    if (type[0] == 'G' && type[1] == 'R' && type[2] == 'O' && type[3] == 'U' &&
        type[4] == 'P' && type[5] == '_' && type[6] == 'I' && type[7] == 'C' &&
        type[8] == 'O' && type[9] == 'N' && type[10] == '\0')
        return 14;
    if (type[0] == 'V' && type[1] == 'E' && type[2] == 'R' && type[3] == 'S' &&
        type[4] == 'I' && type[5] == 'O' && type[6] == 'N' && type[7] == '\0')
        return 16;
    return 0;
}

static int wine_resource_name_matches(const char *query,
                                      const IMAGE_RESOURCE_DIRECTORY_ENTRY_WINE *entry,
                                      const uint8_t *root, size_t root_size)
{
    uint32_t name_rva;
    const uint8_t *name_ptr;
    uint16_t name_len;

    if (!query || !entry)
        return 0;

    if (((uintptr_t)query >> 16) == 0)
        return (entry->Name & 0x80000000u) == 0 &&
               (entry->Name & 0xffffu) == (uint32_t)(uintptr_t)query;

    if (!(entry->Name & 0x80000000u))
        return 0;

    name_rva = entry->Name & 0x7fffffffu;
    if (name_rva + sizeof(uint16_t) > root_size)
        return 0;

    name_ptr = root + name_rva;
    name_len = *(const uint16_t *)name_ptr;
    if (name_rva + sizeof(uint16_t) + (size_t)name_len * sizeof(uint16_t) > root_size)
        return 0;

    return wine_resource_ascii_eq_utf16(query, (const uint16_t *)(name_ptr + sizeof(uint16_t)),
                                        name_len);
}

static const IMAGE_RESOURCE_DIRECTORY_ENTRY_WINE *wine_resource_find_entry(
    const IMAGE_RESOURCE_DIRECTORY_WINE *dir, const char *query,
    const uint8_t *root, size_t root_size)
{
    uint16_t total;
    uint16_t i;
    const IMAGE_RESOURCE_DIRECTORY_ENTRY_WINE *entries;

    if (!dir)
        return NULL;

    total = (uint16_t)(dir->NumberOfNamedEntries + dir->NumberOfIdEntries);
    entries = (const IMAGE_RESOURCE_DIRECTORY_ENTRY_WINE *)(dir + 1);
    for (i = 0; i < total; i++) {
        if (wine_resource_name_matches(query, &entries[i], root, root_size))
            return &entries[i];
    }
    return NULL;
}

static int wine_resource_get_root(void *hModule, void **out_base, IMAGE_NT_HEADERS **out_nt,
                                  const uint8_t **out_root, size_t *out_root_size)
{
    void *base = hModule;
    IMAGE_NT_HEADERS *nt = NULL;
    IMAGE_DATA_DIRECTORY dir;

    if (!base) {
        base = g_loader.image_base;
        if (g_loader.module_count > 0)
            nt = g_loader.modules[0].nt;
    } else {
        loaded_module_t *mod = find_module_by_addr(base);
        if (mod)
            nt = mod->nt;
        else if (base == g_loader.image_base && g_loader.module_count > 0)
            nt = g_loader.modules[0].nt;
    }

    if (!base || !nt)
        return 0;

    if (pe_is_pe32(nt))
        dir = nt->u.nt32.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE];
    else
        dir = nt->u.nt64.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE];

    if (dir.VirtualAddress == 0 || dir.Size < sizeof(IMAGE_RESOURCE_DIRECTORY_WINE))
        return 0;

    *out_base = base;
    *out_nt = nt;
    *out_root = (const uint8_t *)pe_rva_to_ptr(base, nt, dir.VirtualAddress, dir.Size);
    *out_root_size = dir.Size;
    return *out_root != NULL;
}

void *wine_resource_find(void *hModule, const char *type, const char *name,
                         uint32_t *out_size)
{
    void *base = NULL;
    IMAGE_NT_HEADERS *nt = NULL;
    const uint8_t *root = NULL;
    size_t root_size = 0;
    const IMAGE_RESOURCE_DIRECTORY_WINE *type_dir;
    const IMAGE_RESOURCE_DIRECTORY_WINE *name_dir;
    const IMAGE_RESOURCE_DIRECTORY_WINE *lang_dir;
    const IMAGE_RESOURCE_DIRECTORY_ENTRY_WINE *type_entry;
    const IMAGE_RESOURCE_DIRECTORY_ENTRY_WINE *name_entry;
    const IMAGE_RESOURCE_DIRECTORY_ENTRY_WINE *lang_entry;
    const IMAGE_RESOURCE_DATA_ENTRY_WINE *data_entry;
    wine_resource_handle *handle;
    const char *type_query = type;

    if (out_size)
        *out_size = 0;

    if (type && ((uintptr_t)type >> 16) != 0) {
        uint32_t type_id = wine_resource_builtin_type(type);
        if (type_id != 0)
            type_query = (const char *)(uintptr_t)type_id;
    }

    if (!wine_resource_get_root(hModule, &base, &nt, &root, &root_size))
        return NULL;

    type_dir = (const IMAGE_RESOURCE_DIRECTORY_WINE *)root;
    type_entry = wine_resource_find_entry(type_dir, type_query, root, root_size);
    if (!type_entry || !(type_entry->OffsetToData & 0x80000000u))
        return NULL;

    name_dir = (const IMAGE_RESOURCE_DIRECTORY_WINE *)(root + (type_entry->OffsetToData & 0x7fffffffu));
    name_entry = wine_resource_find_entry(name_dir, name, root, root_size);
    if (!name_entry || !(name_entry->OffsetToData & 0x80000000u))
        return NULL;

    lang_dir = (const IMAGE_RESOURCE_DIRECTORY_WINE *)(root + (name_entry->OffsetToData & 0x7fffffffu));
    lang_entry = (const IMAGE_RESOURCE_DIRECTORY_ENTRY_WINE *)(lang_dir + 1);
    if ((const uint8_t *)(lang_entry + 1) > root + root_size)
        return NULL;
    if (lang_dir->NumberOfNamedEntries + lang_dir->NumberOfIdEntries == 0)
        return NULL;
    if (lang_entry->OffsetToData & 0x80000000u)
        return NULL;

    data_entry = (const IMAGE_RESOURCE_DATA_ENTRY_WINE *)(root + lang_entry->OffsetToData);
    if ((const uint8_t *)(data_entry + 1) > root + root_size)
        return NULL;

    handle = malloc(sizeof(*handle));
    if (!handle)
        return NULL;

    handle->module_base = base;
    handle->data = pe_rva_to_ptr(base, nt, data_entry->OffsetToData, data_entry->Size);
    handle->size = data_entry->Size;
    if (!handle->data) {
        free(handle);
        return NULL;
    }

    if (out_size)
        *out_size = handle->size;
    return (void *)(uintptr_t)wine_handle_alloc(HANDLE_TYPE_HRSRC, handle);
}

const void *wine_resource_lock(void *resource_handle)
{
    uint32_t handle = (uint32_t)(uintptr_t)resource_handle;
    wine_resource_handle *res;
    uint8_t type = wine_handle_get_type(handle);

    if (type != HANDLE_TYPE_HRSRC && type != HANDLE_TYPE_HGLOBAL)
        return NULL;

    res = (wine_resource_handle *)wine_handle_get(handle);
    return res ? res->data : NULL;
}

uint32_t wine_resource_size(void *resource_handle)
{
    uint32_t handle = (uint32_t)(uintptr_t)resource_handle;
    wine_resource_handle *res;
    uint8_t type = wine_handle_get_type(handle);

    if (type != HANDLE_TYPE_HRSRC && type != HANDLE_TYPE_HGLOBAL)
        return 0;

    res = (wine_resource_handle *)wine_handle_get(handle);
    return res ? res->size : 0;
}
