/*
 * module_list.c — Module registry for tracking loaded PE images
 */

#include <string.h>
#include "module_list.h"

loaded_module_t module_list[MAX_MODULES];
int module_count = 0;

void init_module_list(void)
{
    memset(module_list, 0, sizeof(module_list));
    module_count = 0;
}

loaded_module_t *add_module(void *base, const char *name, IMAGE_NT_HEADERS64 *nt)
{
    for (int i = 0; i < MAX_MODULES; i++) {
        if (module_list[i].base == NULL) {
            memset(&module_list[i], 0, sizeof(loaded_module_t));
            module_list[i].base = base;
            strncpy(module_list[i].name, name, sizeof(module_list[i].name) - 1);
            module_list[i].name[sizeof(module_list[i].name) - 1] = '\0';
            module_list[i].nt = nt;
            module_list[i].export_cache = NULL;
            module_list[i].ldr_entry = NULL;
            module_list[i].load_count = 1;
            module_count++;
            return &module_list[i];
        }
    }
    return NULL;
}

loaded_module_t *find_module_by_name(const char *name)
{
    for (int i = 0; i < MAX_MODULES; i++) {
        if (module_list[i].base != NULL &&
            strcmp(module_list[i].name, name) == 0) {
            return &module_list[i];
        }
    }
    return NULL;
}

loaded_module_t *find_module_by_addr(void *addr)
{
    uintptr_t a = (uintptr_t)addr;
    for (int i = 0; i < MAX_MODULES; i++) {
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
    for (int i = 0; i < MAX_MODULES; i++) {
        if (&module_list[i] == mod) {
            memset(&module_list[i], 0, sizeof(loaded_module_t));
            module_count--;
            return;
        }
    }
}
