#ifndef MY_WINE_HANDLE_MANAGER_PRIV_H
#define MY_WINE_HANDLE_MANAGER_PRIV_H

#include "include/handle_manager.h"

typedef struct {
    wine_handle_t table[HANDLE_TABLE_SIZE];
    wine_spinlock_t lock;
} wine_handle_manager_runtime;

wine_handle_manager_runtime *wine_handle_manager_state(void);
void wine_handle_manager_runtime_init(wine_handle_manager_runtime *state);

#endif /* MY_WINE_HANDLE_MANAGER_PRIV_H */
