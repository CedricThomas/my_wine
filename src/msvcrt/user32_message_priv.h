#ifndef MY_WINE_USER32_MESSAGE_PRIV_H
#define MY_WINE_USER32_MESSAGE_PRIV_H

#include "user32_priv.h"

void user32_copy_rb_msg_to_msg(const rb_msg_t *src, MSG *dst);
int user32_is_keyboard_message(UINT message);
int user32_synthesize_quit_message(int remove, HWND hwnd_filter,
                                   UINT min_filter, UINT max_filter,
                                   rb_msg_t *msg);
int user32_fetch_queued_message(int blocking, int remove,
                                HWND hwnd_filter,
                                UINT min_filter, UINT max_filter,
                                rb_msg_t *msg);
HHOOK user32_set_keyboard_hook(void *proc);
void user32_clear_keyboard_hook(HHOOK hook);
int user32_call_keyboard_hook(const rb_msg_t *msg);
int user32_call_keyboard_hook_direct(uint32_t message, uint32_t wParam,
                                     intptr_t lParam);

#endif /* MY_WINE_USER32_MESSAGE_PRIV_H */
