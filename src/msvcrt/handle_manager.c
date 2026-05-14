#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "include/handle_manager.h"
#include "handler_abi.h"

static wine_handle_t handle_table[HANDLE_TABLE_SIZE];
static wine_spinlock_t handle_table_lock = 0;

void HANDLER wine_handle_manager_init(void)
{
    memset(handle_table, 0, sizeof(handle_table));

    handle_table[0].id = 0;
    handle_table[0].type = HANDLE_TYPE_FILE;
    handle_table[0].object = (void *)(uintptr_t)STDIN_FILENO;
    handle_table[0].ref_count = 1;

    handle_table[1].id = 1;
    handle_table[1].type = HANDLE_TYPE_FILE;
    handle_table[1].object = (void *)(uintptr_t)STDOUT_FILENO;
    handle_table[1].ref_count = 1;

    handle_table[2].id = 2;
    handle_table[2].type = HANDLE_TYPE_FILE;
    handle_table[2].object = (void *)(uintptr_t)STDERR_FILENO;
    handle_table[2].ref_count = 1;
}

uint64_t HANDLER wine_handle_alloc(uint8_t type, void *object)
{
    uint64_t result = 0;
    wine_spinlock_lock(&handle_table_lock);
    for (uint32_t i = 3; i < HANDLE_TABLE_SIZE; i++)
    {
        if (handle_table[i].ref_count == 0)
        {
            handle_table[i].id = i;
            handle_table[i].type = type;
            handle_table[i].object = object;
            handle_table[i].ref_count = 1;
            result = i;
            break;
        }
    }
    wine_spinlock_unlock(&handle_table_lock);
    return result;
}

void *HANDLER wine_handle_get(uint32_t handle)
{
    void *obj;
    if (handle >= HANDLE_TABLE_SIZE)
        return NULL;
    wine_spinlock_lock(&handle_table_lock);
    if (handle_table[handle].ref_count == 0)
        obj = NULL;
    else
        obj = handle_table[handle].object;
    wine_spinlock_unlock(&handle_table_lock);
    return obj;
}

uint8_t HANDLER wine_handle_get_type(uint32_t handle)
{
    uint8_t t;
    if (handle >= HANDLE_TABLE_SIZE)
        return 0;
    wine_spinlock_lock(&handle_table_lock);
    if (handle_table[handle].ref_count == 0)
        t = 0;
    else
        t = handle_table[handle].type;
    wine_spinlock_unlock(&handle_table_lock);
    return t;
}

void HANDLER wine_handle_add_ref(uint32_t handle)
{
    if (handle >= HANDLE_TABLE_SIZE)
        return;
    wine_spinlock_lock(&handle_table_lock);
    if (handle_table[handle].ref_count != 0)
    {
        if (handle_table[handle].ref_count < 255)
            handle_table[handle].ref_count++;
    }
    wine_spinlock_unlock(&handle_table_lock);
}

void HANDLER wine_handle_free(uint32_t handle)
{
    if (handle >= HANDLE_TABLE_SIZE)
        return;
    if (handle <= 2)  /* stdin/stdout/stderr are never freed */
        return;
    wine_spinlock_lock(&handle_table_lock);
    if (handle_table[handle].ref_count != 0)
    {
        handle_table[handle].ref_count--;
        if (handle_table[handle].ref_count == 0)
        {
            handle_table[handle].id = 0;
            handle_table[handle].type = 0;
            handle_table[handle].object = NULL;
        }
    }
    wine_spinlock_unlock(&handle_table_lock);
}

static __attribute__((constructor)) void handle_manager_ctor(void)
{
    wine_handle_manager_init();
}
