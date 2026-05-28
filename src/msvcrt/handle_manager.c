#include <stdint.h>
#include <stddef.h>

#include "handle_manager_priv.h"
#include "handler_abi.h"

static wine_handle_t *wine_handle_slot(wine_handle_manager_runtime *state,
                                       uint32_t handle)
{
    if (!state || handle >= HANDLE_TABLE_SIZE)
        return NULL;
    return &state->table[handle];
}

void HANDLER wine_handle_manager_init(void)
{
    wine_handle_manager_runtime_init(wine_handle_manager_state());
}

uint64_t HANDLER wine_handle_alloc(uint8_t type, void *object)
{
    wine_handle_manager_runtime *state = wine_handle_manager_state();
    uint64_t result = 0;

    wine_spinlock_lock(&state->lock);
    for (uint32_t i = 3; i < HANDLE_TABLE_SIZE; i++)
    {
        wine_handle_t *slot = &state->table[i];

        if (slot->ref_count == 0)
        {
            slot->id = i;
            slot->type = type;
            slot->object = object;
            slot->ref_count = 1;
            result = i;
            break;
        }
    }
    wine_spinlock_unlock(&state->lock);
    return result;
}

void *HANDLER wine_handle_get(uint32_t handle)
{
    wine_handle_manager_runtime *state = wine_handle_manager_state();
    wine_handle_t *slot = wine_handle_slot(state, handle);
    void *obj;

    if (!slot)
        return NULL;

    wine_spinlock_lock(&state->lock);
    if (slot->ref_count == 0)
        obj = NULL;
    else
        obj = slot->object;
    wine_spinlock_unlock(&state->lock);
    return obj;
}

uint8_t HANDLER wine_handle_get_type(uint32_t handle)
{
    wine_handle_manager_runtime *state = wine_handle_manager_state();
    wine_handle_t *slot = wine_handle_slot(state, handle);
    uint8_t t;

    if (!slot)
        return 0;

    wine_spinlock_lock(&state->lock);
    if (slot->ref_count == 0)
        t = 0;
    else
        t = slot->type;
    wine_spinlock_unlock(&state->lock);
    return t;
}

void HANDLER wine_handle_add_ref(uint32_t handle)
{
    wine_handle_manager_runtime *state = wine_handle_manager_state();
    wine_handle_t *slot = wine_handle_slot(state, handle);

    if (!slot)
        return;

    wine_spinlock_lock(&state->lock);
    if (slot->ref_count != 0)
    {
        if (slot->ref_count < 255)
            slot->ref_count++;
    }
    wine_spinlock_unlock(&state->lock);
}

void HANDLER wine_handle_free(uint32_t handle)
{
    if (handle <= 2)  /* stdin/stdout/stderr are never freed */
        return;

    wine_handle_manager_runtime *state = wine_handle_manager_state();
    wine_handle_t *slot = wine_handle_slot(state, handle);

    if (!slot)
        return;

    wine_spinlock_lock(&state->lock);
    if (slot->ref_count != 0)
    {
        slot->ref_count--;
        if (slot->ref_count == 0)
        {
            slot->id = 0;
            slot->type = 0;
            slot->object = NULL;
        }
    }
    wine_spinlock_unlock(&state->lock);
}

static __attribute__((constructor)) void handle_manager_ctor(void)
{
    wine_handle_manager_init();
}
