#include <string.h>
#include <unistd.h>

#include "handle_manager_priv.h"

static wine_handle_manager_runtime g_handle_manager_runtime;

wine_handle_manager_runtime *wine_handle_manager_state(void)
{
    return &g_handle_manager_runtime;
}

void wine_handle_manager_runtime_init(wine_handle_manager_runtime *state)
{
    if (!state)
        return;

    memset(state, 0, sizeof(*state));

    state->table[0].id = 0;
    state->table[0].type = HANDLE_TYPE_FILE;
    state->table[0].object = (void *)(uintptr_t)STDIN_FILENO;
    state->table[0].ref_count = 1;

    state->table[1].id = 1;
    state->table[1].type = HANDLE_TYPE_FILE;
    state->table[1].object = (void *)(uintptr_t)STDOUT_FILENO;
    state->table[1].ref_count = 1;

    state->table[2].id = 2;
    state->table[2].type = HANDLE_TYPE_FILE;
    state->table[2].object = (void *)(uintptr_t)STDERR_FILENO;
    state->table[2].ref_count = 1;
}
