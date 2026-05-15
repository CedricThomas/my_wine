/*
 * rb_input.c
 *
 * Timer, joystick, keyboard, and cursor functions for the SDL2 backend.
 */

#include "rb_sdl2_priv.h"
#include <stdlib.h>
#include <string.h>

/* ---- Timer ---- */

uint32_t rb_timer_get_ticks(void)
{
    return SDL_GetTicks();
}

void rb_timer_delay(uint32_t ms)
{
    SDL_Delay(ms);
}

/* ---- Joystick ---- */

int rb_joy_count(void)
{
    return SDL_NumJoysticks();
}

int rb_joy_get_caps(int idx, char *name, int name_len,
                    int *n_axes, int *n_buttons,
                    uint16_t *min, uint16_t *max)
{
    if (idx < 0 || idx >= SDL_NumJoysticks())
        return RB_FAIL;

    SDL_Joystick *joy = SDL_JoystickOpen(idx);
    if (!joy)
        return RB_FAIL;

    const char *n = SDL_JoystickName(joy);
    if (name && n) {
        strncpy(name, n, name_len - 1);
        name[name_len - 1] = '\0';
    }
    if (n_axes)
        *n_axes = SDL_JoystickNumAxes(joy);
    if (n_buttons)
        *n_buttons = SDL_JoystickNumButtons(joy);
    if (min)
        *min = 0;      /* WINMM range */
    if (max)
        *max = 65535;  /* WINMM range */

    SDL_JoystickClose(joy);
    return RB_OK;
}

int rb_joy_get_state(int idx,
                     uint16_t *axes, int n_axes,
                     uint8_t *buttons, int n_buttons)
{
    if (idx < 0 || idx >= SDL_NumJoysticks())
        return RB_FAIL;

    SDL_Joystick *joy = SDL_JoystickOpen(idx);
    if (!joy)
        return RB_FAIL;

    for (int i = 0; i < n_axes; i++) {
        int16_t raw = SDL_JoystickGetAxis(joy, i);  // range -32768..32767
        axes[i] = (uint16_t)(raw + 32768);  // map to 0..65535 (WINMM range)
    }
    for (int i = 0; i < n_buttons; i++) {
        buttons[i] = SDL_JoystickGetButton(joy, i);
    }

    SDL_JoystickClose(joy);
    return RB_OK;
}

/* ---- Keyboard ---- */

int16_t rb_keyboard_get_async_state(int vk)
{
    int scancode = vk_to_scancode(vk);
    if (scancode < 0)
        return 0;  // not mapped = not pressed

    const Uint8 *state = SDL_GetKeyboardState(NULL);
    int pressed = state[scancode];
    return (int16_t)(pressed ? 0x8000 : 0);  // top bit = pressed, toggle bit = 0
}

/* ---- Cursor ---- */

static inline rb_cursor *get_cursor(rb_cursor_t cur)
{
    return (rb_cursor *)wine_handle_get((uint32_t)cur);
}

rb_cursor_t rb_cursor_create(int idc)
{
    SDL_SystemCursor type = SDL_SYSTEM_CURSOR_ARROW;

    switch (idc) {
        case 0: type = SDL_SYSTEM_CURSOR_ARROW;    break;  /* IDC_ARROW  */
        case 1: type = SDL_SYSTEM_CURSOR_CROSSHAIR; break;  /* IDC_CROSS  */
        case 2: type = SDL_SYSTEM_CURSOR_ARROW;     break;  /* IDC_UPARROW — no SDL_SYSTEM_CURSOR_UP */
        case 3: type = SDL_SYSTEM_CURSOR_WAIT;      break;  /* IDC_WAIT   */
        case 4: type = SDL_SYSTEM_CURSOR_IBEAM;     break;  /* IDC_IBEAM  */
        case 5: type = SDL_SYSTEM_CURSOR_SIZEALL;   break;  /* IDC_SIZE   */
        case 6: type = SDL_SYSTEM_CURSOR_HAND;      break;  /* IDC_ICON   */
        default: type = SDL_SYSTEM_CURSOR_ARROW;    break;
    }

    SDL_Cursor *cursor = SDL_CreateSystemCursor(type);
    if (!cursor)
        return 0;

    rb_cursor *c = malloc(sizeof(*c));
    if (!c) {
        SDL_FreeCursor(cursor);
        return 0;
    }
    c->cursor = cursor;
    return (rb_cursor_t)wine_handle_alloc(HANDLE_TYPE_HCURSOR, c);
}

int rb_cursor_destroy(rb_cursor_t cur)
{
    rb_cursor *c = get_cursor(cur);
    if (!c)
        return RB_FAIL;

    SDL_FreeCursor(c->cursor);
    free(c);
    wine_handle_free((uint32_t)cur);
    return RB_OK;
}

int rb_cursor_show(int show)
{
    SDL_ShowCursor(show ? 1 : 0);
    return RB_OK;
}

/*
 * NOTE: rb_event_set_active_window(win) should be called in rb_window_create().
 * That change belongs in rb_window.c, not here.
 */
