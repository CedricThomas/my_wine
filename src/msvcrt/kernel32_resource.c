#include "kernel32_priv.h"
#include "resource_win32.h"
#include "include/handle_manager.h"

KERNEL32_STUB
void *FindResourceA(void *hModule, const char *lpName, const char *lpType)
{
    return FORCE_PTR_RETURN(wine_resource_find(hModule, lpType, lpName, NULL));
}

KERNEL32_STUB
uint32_t SizeofResource(void *hModule, void *hResInfo)
{
    (void)hModule;
    return wine_resource_size(hResInfo);
}

KERNEL32_STUB
void *LoadResource(void *hModule, void *hResInfo)
{
    uint32_t handle = (uint32_t)(uintptr_t)hResInfo;
    (void)hModule;

    if (wine_handle_get_type(handle) != HANDLE_TYPE_HRSRC)
        return FORCE_PTR_RETURN(NULL);
    if (!wine_handle_get(handle))
        return FORCE_PTR_RETURN(NULL);
    wine_handle_add_ref(handle);
    return FORCE_PTR_RETURN((void *)(uintptr_t)handle);
}

KERNEL32_STUB
void *LockResource(void *hResData)
{
    return FORCE_PTR_RETURN((void *)wine_resource_lock(hResData));
}
