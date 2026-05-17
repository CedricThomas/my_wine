/*
 * rb_input.c
 *
 * Timer, joystick, keyboard, and cursor functions for the SDL2 backend.
 */

#include "rb_sdl2_priv.h"
#include <stdlib.h>
#include <string.h>

/* ---- Timer ---- */

static uintptr_t rb_sdl_get_ticks_call(void *arg)
{
    (void)arg;
    return (uintptr_t)SDL_GetTicks();
}

static uintptr_t rb_sdl_delay_call(void *arg)
{
    SDL_Delay(*(uint32_t *)arg);
    return 0;
}

uint32_t rb_timer_get_ticks(void)
{
    return (uint32_t)rb_call_on_host_stack(rb_sdl_get_ticks_call, NULL);
}

void rb_timer_delay(uint32_t ms)
{
    rb_call_on_host_stack(rb_sdl_delay_call, &ms);
}

/* ---- Joystick ---- */

static uintptr_t rb_sdl_num_joysticks_call(void *arg)
{
    (void)arg;
    return (uintptr_t)SDL_NumJoysticks();
}

typedef struct {
    int idx;
    SDL_Joystick *joy;
} rb_sdl_joystick_open_args;

static uintptr_t rb_sdl_joystick_open_call(void *arg)
{
    rb_sdl_joystick_open_args *a = arg;
    a->joy = SDL_JoystickOpen(a->idx);
    return (uintptr_t)a->joy;
}

static uintptr_t rb_sdl_joystick_close_call(void *arg)
{
    SDL_JoystickClose((SDL_Joystick *)arg);
    return 0;
}

typedef struct {
    SDL_Joystick *joy;
    char *name;
    int name_len;
    int *n_axes;
    int *n_buttons;
    uint16_t *min;
    uint16_t *max;
} rb_sdl_joystick_caps_args;

static uintptr_t rb_sdl_joystick_caps_call(void *arg)
{
    rb_sdl_joystick_caps_args *a = arg;
    const char *n = SDL_JoystickName(a->joy);
    if (a->name && a->name_len > 0 && n) {
        strncpy(a->name, n, (size_t)a->name_len - 1);
        a->name[a->name_len - 1] = '\0';
    }
    if (a->n_axes)
        *a->n_axes = SDL_JoystickNumAxes(a->joy);
    if (a->n_buttons)
        *a->n_buttons = SDL_JoystickNumButtons(a->joy);
    if (a->min)
        *a->min = 0;
    if (a->max)
        *a->max = 65535;
    return 0;
}

typedef struct {
    SDL_Joystick *joy;
    uint16_t *axes;
    int n_axes;
    uint8_t *buttons;
    int n_buttons;
} rb_sdl_joystick_state_args;

static uintptr_t rb_sdl_joystick_state_call(void *arg)
{
    rb_sdl_joystick_state_args *a = arg;
    for (int i = 0; i < a->n_axes; i++) {
        int16_t raw = SDL_JoystickGetAxis(a->joy, i);
        a->axes[i] = (uint16_t)(raw + 32768);
    }
    for (int i = 0; i < a->n_buttons; i++) {
        a->buttons[i] = SDL_JoystickGetButton(a->joy, i);
    }
    return 0;
}

int rb_joy_count(void)
{
    return (int)rb_call_on_host_stack(rb_sdl_num_joysticks_call, NULL);
}

int rb_joy_get_caps(int idx, char *name, int name_len,
                    int *n_axes, int *n_buttons,
                    uint16_t *min, uint16_t *max)
{
    int joystick_count = rb_joy_count();
    if (idx < 0 || idx >= joystick_count)
        return RB_FAIL;

    rb_sdl_joystick_open_args open_args = { .idx = idx, .joy = NULL };
    SDL_Joystick *joy = (SDL_Joystick *)rb_call_on_host_stack(rb_sdl_joystick_open_call, &open_args);
    if (!joy)
        return RB_FAIL;

    rb_sdl_joystick_caps_args caps_args = { joy, name, name_len, n_axes, n_buttons, min, max };
    rb_call_on_host_stack(rb_sdl_joystick_caps_call, &caps_args);

    rb_call_on_host_stack(rb_sdl_joystick_close_call, joy);
    return RB_OK;
}

int rb_joy_get_state(int idx,
                     uint16_t *axes, int n_axes,
                     uint8_t *buttons, int n_buttons)
{
    int joystick_count = rb_joy_count();
    if (idx < 0 || idx >= joystick_count)
        return RB_FAIL;

    rb_sdl_joystick_open_args open_args = { .idx = idx, .joy = NULL };
    SDL_Joystick *joy = (SDL_Joystick *)rb_call_on_host_stack(rb_sdl_joystick_open_call, &open_args);
    if (!joy)
        return RB_FAIL;

    if ((n_axes > 0 && !axes) || (n_buttons > 0 && !buttons)) {
        rb_call_on_host_stack(rb_sdl_joystick_close_call, joy);
        return RB_FAIL;
    }

    rb_sdl_joystick_state_args state_args = { joy, axes, n_axes, buttons, n_buttons };
    rb_call_on_host_stack(rb_sdl_joystick_state_call, &state_args);

    rb_call_on_host_stack(rb_sdl_joystick_close_call, joy);
    return RB_OK;
}

/* ---- Keyboard ---- */

typedef struct {
    int scancode;
    int pressed;
} rb_sdl_keyboard_state_args;

static uintptr_t rb_sdl_get_keyboard_state_call(void *arg)
{
    rb_sdl_keyboard_state_args *a = arg;
    const Uint8 *state = SDL_GetKeyboardState(NULL);
    a->pressed = state ? state[a->scancode] : 0;
    return 0;
}

int16_t rb_keyboard_get_async_state(int vk)
{
    int scancode = vk_to_scancode(vk);
    if (scancode < 0)
        return 0;  // not mapped = not pressed

    rb_sdl_keyboard_state_args args = { scancode, 0 };
    rb_call_on_host_stack(rb_sdl_get_keyboard_state_call, &args);
    int pressed = args.pressed;
    return (int16_t)(pressed ? 0x8000 : 0);  // top bit = pressed, toggle bit = 0
}

/* ---- Cursor ---- */

static inline rb_cursor *get_cursor(rb_cursor_t cur)
{
    if (wine_handle_get_type((uint32_t)cur) != HANDLE_TYPE_RB_CURSOR)
        return NULL;
    return (rb_cursor *)wine_handle_get((uint32_t)cur);
}

static uintptr_t rb_sdl_create_system_cursor_call(void *arg)
{
    return (uintptr_t)SDL_CreateSystemCursor(*(SDL_SystemCursor *)arg);
}

static uintptr_t rb_sdl_free_cursor_call(void *arg)
{
    SDL_FreeCursor((SDL_Cursor *)arg);
    return 0;
}

static uintptr_t rb_sdl_show_cursor_call(void *arg)
{
    return (uintptr_t)SDL_ShowCursor(*(int *)arg);
}

rb_cursor_t rb_cursor_create(int idc)
{
    SDL_SystemCursor type = SDL_SYSTEM_CURSOR_ARROW;

    switch (idc) {
        case 32512: type = SDL_SYSTEM_CURSOR_ARROW;     break;  /* IDC_ARROW */
        case 32513: type = SDL_SYSTEM_CURSOR_IBEAM;     break;  /* IDC_IBEAM */
        case 32514: type = SDL_SYSTEM_CURSOR_WAIT;      break;  /* IDC_WAIT */
        case 32515: type = SDL_SYSTEM_CURSOR_CROSSHAIR; break;  /* IDC_CROSS */
        case 32516: type = SDL_SYSTEM_CURSOR_ARROW;     break;  /* IDC_UPARROW */
        case 32640: type = SDL_SYSTEM_CURSOR_SIZEALL;   break;  /* IDC_SIZE */
        case 32641: type = SDL_SYSTEM_CURSOR_HAND;      break;  /* IDC_ICON */
        case 32642: type = SDL_SYSTEM_CURSOR_SIZENWSE;  break;  /* IDC_SIZENWSE */
        case 32643: type = SDL_SYSTEM_CURSOR_SIZENS;    break;  /* IDC_SIZENS */
        case 32644: type = SDL_SYSTEM_CURSOR_SIZENESW;  break;  /* IDC_SIZENESW */
        case 32645: type = SDL_SYSTEM_CURSOR_SIZEWE;    break;  /* IDC_SIZEWE */
        case 32646: type = SDL_SYSTEM_CURSOR_SIZEALL;   break;  /* IDC_SIZEALL */
        case 32648: type = SDL_SYSTEM_CURSOR_NO;        break;  /* IDC_NO */
        case 32649: type = SDL_SYSTEM_CURSOR_HAND;      break;  /* IDC_HAND */
        default: type = SDL_SYSTEM_CURSOR_ARROW;        break;
    }

    SDL_Cursor *cursor = (SDL_Cursor *)rb_call_on_host_stack(rb_sdl_create_system_cursor_call, &type);

    rb_cursor *c = rb_host_malloc(sizeof(*c));
    if (!c) {
        if (cursor)
            rb_call_on_host_stack(rb_sdl_free_cursor_call, cursor);
        return 0;
    }
    c->cursor = cursor;
    return (rb_cursor_t)wine_handle_alloc(HANDLE_TYPE_RB_CURSOR, c);
}

int rb_cursor_destroy(rb_cursor_t cur)
{
    rb_cursor *c = get_cursor(cur);
    if (!c)
        return RB_FAIL;

    if (c->cursor)
        rb_call_on_host_stack(rb_sdl_free_cursor_call, c->cursor);
    rb_host_free(c);
    wine_handle_free((uint32_t)cur);
    return RB_OK;
}

int rb_cursor_show(int show)
{
    int sdl_show = show ? 1 : 0;
    rb_call_on_host_stack(rb_sdl_show_cursor_call, &sdl_show);
    return RB_OK;
}

/*
 * NOTE: rb_event_set_active_window(win) should be called in rb_window_create().
 * That change belongs in rb_window.c, not here.
 */
