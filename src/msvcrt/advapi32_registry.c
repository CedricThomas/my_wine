#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include "kernel32_priv.h"
#include "include/handle_manager.h"

#define HKEY_CURRENT_USER_WINE ((uintptr_t)0x80000001u)
#define REG_SZ 1

typedef struct {
    char path[256];
} wine_hkey;

typedef struct {
    char path[256];
    char name[128];
    uint32_t type;
    uint8_t data[512];
    uint32_t size;
    int used;
} wine_reg_value;

static wine_reg_value g_reg_values[64];

static void reg_copy_cstr(char *dst, size_t dst_size, const char *src)
{
    size_t len;

    if (!dst || dst_size == 0)
        return;
    if (!src)
        src = "";
    len = strlen(src);
    if (len >= dst_size)
        len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void reg_join_path(uintptr_t root, const char *subkey, char *out, size_t out_size)
{
    const char *prefix = "HKCU";
    size_t i = 0;
    size_t j = 0;

    if (root != HKEY_CURRENT_USER_WINE && wine_handle_get_type((uint32_t)root) == HANDLE_TYPE_HKEY) {
        wine_hkey *key = (wine_hkey *)wine_handle_get((uint32_t)root);
        if (key)
            prefix = key->path;
    }

    while (prefix[i] && i + 1 < out_size) {
        out[i] = prefix[i];
        i++;
    }
    if (subkey && subkey[0] != '\0' && i + 1 < out_size)
        out[i++] = '\\';
    while (subkey && subkey[j] && i + 1 < out_size) {
        out[i++] = subkey[j++];
    }
    out[i] = '\0';
}

static int reg_find_value(const char *path, const char *name)
{
    int i;
    for (i = 0; i < 64; i++) {
        if (!g_reg_values[i].used)
            continue;
        if (strcmp(g_reg_values[i].path, path) == 0 &&
            strcmp(g_reg_values[i].name, name ? name : "") == 0)
            return i;
    }
    return -1;
}

static int reg_ensure_value(const char *path, const char *name)
{
    int idx = reg_find_value(path, name);
    int i;

    if (idx >= 0)
        return idx;
    for (i = 0; i < 64; i++) {
        if (!g_reg_values[i].used) {
            memset(&g_reg_values[i], 0, sizeof(g_reg_values[i]));
            g_reg_values[i].used = 1;
            reg_copy_cstr(g_reg_values[i].path, sizeof(g_reg_values[i].path), path);
            reg_copy_cstr(g_reg_values[i].name, sizeof(g_reg_values[i].name), name ? name : "");
            return i;
        }
    }
    return -1;
}

KERNEL32_STUB
uint32_t RegCreateKeyA(void *hKey, const char *lpSubKey, void **phkResult)
{
    wine_hkey *key;
    char path[256];

    reg_join_path((uintptr_t)hKey, lpSubKey, path, sizeof(path));
    key = malloc(sizeof(*key));
    if (!key)
        return 8;
    memset(key, 0, sizeof(*key));
    reg_copy_cstr(key->path, sizeof(key->path), path);
    if (phkResult)
        *phkResult = (void *)(uintptr_t)wine_handle_alloc(HANDLE_TYPE_HKEY, key);
    return 0;
}

KERNEL32_STUB
uint32_t RegOpenKeyA(void *hKey, const char *lpSubKey, void **phkResult)
{
    return RegCreateKeyA(hKey, lpSubKey, phkResult);
}

KERNEL32_STUB
uint32_t RegCloseKey(void *hKey)
{
    uint32_t handle = (uint32_t)(uintptr_t)hKey;
    if (handle != 0 && (uintptr_t)hKey != HKEY_CURRENT_USER_WINE &&
        wine_handle_get_type(handle) == HANDLE_TYPE_HKEY) {
        free(wine_handle_get(handle));
        wine_handle_free(handle);
    }
    return 0;
}

KERNEL32_STUB
uint32_t RegQueryValueExA(void *hKey, const char *lpValueName, void *lpReserved,
                          uint32_t *lpType, uint8_t *lpData, uint32_t *lpcbData)
{
    wine_hkey *key;
    int idx;
    (void)lpReserved;

    if ((uintptr_t)hKey == HKEY_CURRENT_USER_WINE)
        return 2;
    key = (wine_hkey *)wine_handle_get((uint32_t)(uintptr_t)hKey);
    if (!key)
        return 2;

    idx = reg_find_value(key->path, lpValueName);
    if (idx < 0)
        return 2;

    if (lpType)
        *lpType = g_reg_values[idx].type;
    if (lpcbData) {
        if (!lpData || *lpcbData < g_reg_values[idx].size) {
            *lpcbData = g_reg_values[idx].size;
            return 234;
        }
        memcpy(lpData, g_reg_values[idx].data, g_reg_values[idx].size);
        *lpcbData = g_reg_values[idx].size;
    }
    return 0;
}

KERNEL32_STUB
uint32_t RegSetValueExA(void *hKey, const char *lpValueName, uint32_t Reserved,
                        uint32_t dwType, const uint8_t *lpData, uint32_t cbData)
{
    wine_hkey *key = (wine_hkey *)wine_handle_get((uint32_t)(uintptr_t)hKey);
    int idx;
    (void)Reserved;

    if (!key)
        return 2;
    idx = reg_ensure_value(key->path, lpValueName);
    if (idx < 0)
        return 8;

    g_reg_values[idx].type = dwType ? dwType : REG_SZ;
    if (cbData > sizeof(g_reg_values[idx].data))
        cbData = sizeof(g_reg_values[idx].data);
    if (lpData && cbData > 0)
        memcpy(g_reg_values[idx].data, lpData, cbData);
    g_reg_values[idx].size = cbData;
    return 0;
}

KERNEL32_STUB
uint32_t RegDeleteKeyA(void *hKey, const char *lpSubKey)
{
    char path[256];
    int i;

    reg_join_path((uintptr_t)hKey, lpSubKey, path, sizeof(path));
    for (i = 0; i < 64; i++) {
        if (!g_reg_values[i].used)
            continue;
        if (strcmp(g_reg_values[i].path, path) != 0)
            continue;
        memset(&g_reg_values[i], 0, sizeof(g_reg_values[i]));
    }
    return 0;
}

KERNEL32_STUB
uint32_t RegEnumKeyExA(void *hKey, uint32_t dwIndex, char *lpName, uint32_t *lpcchName,
                       uint32_t *lpReserved, char *lpClass, uint32_t *lpcchClass,
                       FILETIME *lpftLastWriteTime)
{
    (void)hKey;
    (void)dwIndex;
    (void)lpReserved;
    (void)lpClass;
    (void)lpcchClass;
    (void)lpftLastWriteTime;

    if (lpName && lpcchName && *lpcchName > 0)
        lpName[0] = '\0';
    if (lpcchName)
        *lpcchName = 0;
    return 259; /* ERROR_NO_MORE_ITEMS */
}

KERNEL32_STUB
uint32_t GetUserNameA(char *buffer, uint32_t *size)
{
    static const char user[] = "player";
    uint32_t needed = (uint32_t)sizeof(user);

    if (!size)
        return 0;
    if (!buffer || *size < needed) {
        *size = needed;
        return 0;
    }

    memcpy(buffer, user, needed);
    *size = needed - 1;
    return 1;
}
