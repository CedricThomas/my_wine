/*
 * rb_event_keyboard.c
 *
 * Owns keyboard/text translation for the SDL backend, including the watched
 * key queue used to avoid duplicate guest-visible key delivery.
 */

#include "rb_sdl2_priv.h"
#include "include/debug.h"
#include <string.h>

#define RB_KEY_WATCH_QUEUE_CAPACITY 64

#define WM_CLOSE          0x0010
#define WM_KEYDOWN        0x0100
#define WM_KEYUP          0x0101
#define WM_CHAR           0x0102
#define WM_SYSKEYDOWN     0x0104
#define WM_SYSKEYUP       0x0105

#define VK_BACK           0x08
#define VK_TAB            0x09
#define VK_RETURN         0x0D
#define VK_ESCAPE         0x1B
#define VK_SPACE          0x20
#define VK_PRIOR          0x21
#define VK_NEXT           0x22
#define VK_END            0x23
#define VK_HOME           0x24
#define VK_LEFT           0x25
#define VK_UP             0x26
#define VK_RIGHT          0x27
#define VK_DOWN           0x28
#define VK_INSERT         0x2D
#define VK_DELETE         0x2E
#define VK_F1             0x70
#define VK_F2             0x71
#define VK_F3             0x72
#define VK_F4             0x73
#define VK_F5             0x74
#define VK_F6             0x75
#define VK_F7             0x76
#define VK_F8             0x77
#define VK_F9             0x78
#define VK_F10            0x79
#define VK_F11            0x7A
#define VK_F12            0x7B
#define VK_LSHIFT         0xA0
#define VK_RSHIFT         0xA1
#define VK_LCONTROL       0xA2
#define VK_RCONTROL       0xA3
#define VK_LMENU          0xA4
#define VK_RMENU          0xA5

typedef struct {
    uint32_t type;
    uint32_t timestamp;
    uint32_t window_id;
    SDL_Scancode scancode;
    SDL_Keycode sym;
} rb_key_watch_event;

static rb_key_watch_event g_key_watch_queue[RB_KEY_WATCH_QUEUE_CAPACITY];
static size_t g_key_watch_queue_count = 0;

static void rb_event_note_watched_key(const SDL_KeyboardEvent *key)
{
    rb_key_watch_event *entry;

    if (!key)
        return;
    if (g_key_watch_queue_count >= RB_KEY_WATCH_QUEUE_CAPACITY) {
        memmove(g_key_watch_queue, g_key_watch_queue + 1,
                (RB_KEY_WATCH_QUEUE_CAPACITY - 1) * sizeof(g_key_watch_queue[0]));
        g_key_watch_queue_count = RB_KEY_WATCH_QUEUE_CAPACITY - 1;
    }

    entry = &g_key_watch_queue[g_key_watch_queue_count++];
    entry->type = key->type;
    entry->timestamp = key->timestamp;
    entry->window_id = key->windowID;
    entry->scancode = key->keysym.scancode;
    entry->sym = key->keysym.sym;
}

static int rb_event_take_watched_key(const SDL_KeyboardEvent *key)
{
    size_t i;

    if (!key)
        return 0;

    for (i = 0; i < g_key_watch_queue_count; i++) {
        rb_key_watch_event *entry = &g_key_watch_queue[i];

        if (entry->type == key->type &&
            entry->timestamp == key->timestamp &&
            entry->window_id == key->windowID &&
            entry->scancode == key->keysym.scancode &&
            entry->sym == key->keysym.sym) {
            if (i + 1 < g_key_watch_queue_count) {
                memmove(&g_key_watch_queue[i], &g_key_watch_queue[i + 1],
                        (g_key_watch_queue_count - i - 1) * sizeof(g_key_watch_queue[0]));
            }
            g_key_watch_queue_count--;
            return 1;
        }
    }

    return 0;
}

static int rb_keycode_to_vk(SDL_Keycode sym, SDL_Scancode scancode)
{
    if (sym >= SDLK_a && sym <= SDLK_z)
        return 'A' + (int)(sym - SDLK_a);
    if (sym >= SDLK_0 && sym <= SDLK_9)
        return '0' + (int)(sym - SDLK_0);

    switch (sym) {
    case SDLK_BACKSPACE: return VK_BACK;
    case SDLK_TAB: return VK_TAB;
    case SDLK_RETURN: return VK_RETURN;
    case SDLK_ESCAPE: return VK_ESCAPE;
    case SDLK_SPACE: return VK_SPACE;
    case SDLK_PAGEUP: return VK_PRIOR;
    case SDLK_PAGEDOWN: return VK_NEXT;
    case SDLK_END: return VK_END;
    case SDLK_HOME: return VK_HOME;
    case SDLK_LEFT: return VK_LEFT;
    case SDLK_UP: return VK_UP;
    case SDLK_RIGHT: return VK_RIGHT;
    case SDLK_DOWN: return VK_DOWN;
    case SDLK_INSERT: return VK_INSERT;
    case SDLK_DELETE: return VK_DELETE;
    case SDLK_F1: return VK_F1;
    case SDLK_F2: return VK_F2;
    case SDLK_F3: return VK_F3;
    case SDLK_F4: return VK_F4;
    case SDLK_F5: return VK_F5;
    case SDLK_F6: return VK_F6;
    case SDLK_F7: return VK_F7;
    case SDLK_F8: return VK_F8;
    case SDLK_F9: return VK_F9;
    case SDLK_F10: return VK_F10;
    case SDLK_F11: return VK_F11;
    case SDLK_F12: return VK_F12;
    case SDLK_LSHIFT: return VK_LSHIFT;
    case SDLK_RSHIFT: return VK_RSHIFT;
    case SDLK_LCTRL: return VK_LCONTROL;
    case SDLK_RCTRL: return VK_RCONTROL;
    case SDLK_LALT: return VK_LMENU;
    case SDLK_RALT: return VK_RMENU;
    default:
        break;
    }

    return scancode_to_vk(scancode);
}

static uint32_t rb_build_key_lparam(const SDL_KeyboardEvent *key, int is_keyup)
{
    uint32_t scancode = 0;
    uint32_t extended = 0;
    uint32_t lparam;

    switch (key->keysym.scancode) {
    case SDL_SCANCODE_ESCAPE: scancode = 0x01; break;
    case SDL_SCANCODE_1: scancode = 0x02; break;
    case SDL_SCANCODE_2: scancode = 0x03; break;
    case SDL_SCANCODE_3: scancode = 0x04; break;
    case SDL_SCANCODE_4: scancode = 0x05; break;
    case SDL_SCANCODE_5: scancode = 0x06; break;
    case SDL_SCANCODE_6: scancode = 0x07; break;
    case SDL_SCANCODE_7: scancode = 0x08; break;
    case SDL_SCANCODE_8: scancode = 0x09; break;
    case SDL_SCANCODE_9: scancode = 0x0a; break;
    case SDL_SCANCODE_0: scancode = 0x0b; break;
    case SDL_SCANCODE_MINUS: scancode = 0x0c; break;
    case SDL_SCANCODE_EQUALS: scancode = 0x0d; break;
    case SDL_SCANCODE_BACKSPACE: scancode = 0x0e; break;
    case SDL_SCANCODE_TAB: scancode = 0x0f; break;
    case SDL_SCANCODE_Q: scancode = 0x10; break;
    case SDL_SCANCODE_W: scancode = 0x11; break;
    case SDL_SCANCODE_E: scancode = 0x12; break;
    case SDL_SCANCODE_R: scancode = 0x13; break;
    case SDL_SCANCODE_T: scancode = 0x14; break;
    case SDL_SCANCODE_Y: scancode = 0x15; break;
    case SDL_SCANCODE_U: scancode = 0x16; break;
    case SDL_SCANCODE_I: scancode = 0x17; break;
    case SDL_SCANCODE_O: scancode = 0x18; break;
    case SDL_SCANCODE_P: scancode = 0x19; break;
    case SDL_SCANCODE_LEFTBRACKET: scancode = 0x1a; break;
    case SDL_SCANCODE_RIGHTBRACKET: scancode = 0x1b; break;
    case SDL_SCANCODE_RETURN: scancode = 0x1c; break;
    case SDL_SCANCODE_LCTRL: scancode = 0x1d; break;
    case SDL_SCANCODE_RCTRL: scancode = 0x1d; extended = 1; break;
    case SDL_SCANCODE_A: scancode = 0x1e; break;
    case SDL_SCANCODE_S: scancode = 0x1f; break;
    case SDL_SCANCODE_D: scancode = 0x20; break;
    case SDL_SCANCODE_F: scancode = 0x21; break;
    case SDL_SCANCODE_G: scancode = 0x22; break;
    case SDL_SCANCODE_H: scancode = 0x23; break;
    case SDL_SCANCODE_J: scancode = 0x24; break;
    case SDL_SCANCODE_K: scancode = 0x25; break;
    case SDL_SCANCODE_L: scancode = 0x26; break;
    case SDL_SCANCODE_SEMICOLON: scancode = 0x27; break;
    case SDL_SCANCODE_APOSTROPHE: scancode = 0x28; break;
    case SDL_SCANCODE_GRAVE: scancode = 0x29; break;
    case SDL_SCANCODE_LSHIFT: scancode = 0x2a; break;
    case SDL_SCANCODE_BACKSLASH: scancode = 0x2b; break;
    case SDL_SCANCODE_Z: scancode = 0x2c; break;
    case SDL_SCANCODE_X: scancode = 0x2d; break;
    case SDL_SCANCODE_C: scancode = 0x2e; break;
    case SDL_SCANCODE_V: scancode = 0x2f; break;
    case SDL_SCANCODE_B: scancode = 0x30; break;
    case SDL_SCANCODE_N: scancode = 0x31; break;
    case SDL_SCANCODE_M: scancode = 0x32; break;
    case SDL_SCANCODE_COMMA: scancode = 0x33; break;
    case SDL_SCANCODE_PERIOD: scancode = 0x34; break;
    case SDL_SCANCODE_SLASH: scancode = 0x35; break;
    case SDL_SCANCODE_RSHIFT: scancode = 0x36; break;
    case SDL_SCANCODE_KP_MULTIPLY: scancode = 0x37; break;
    case SDL_SCANCODE_LALT: scancode = 0x38; break;
    case SDL_SCANCODE_RALT: scancode = 0x38; extended = 1; break;
    case SDL_SCANCODE_SPACE: scancode = 0x39; break;
    case SDL_SCANCODE_CAPSLOCK: scancode = 0x3a; break;
    case SDL_SCANCODE_F1: scancode = 0x3b; break;
    case SDL_SCANCODE_F2: scancode = 0x3c; break;
    case SDL_SCANCODE_F3: scancode = 0x3d; break;
    case SDL_SCANCODE_F4: scancode = 0x3e; break;
    case SDL_SCANCODE_F5: scancode = 0x3f; break;
    case SDL_SCANCODE_F6: scancode = 0x40; break;
    case SDL_SCANCODE_F7: scancode = 0x41; break;
    case SDL_SCANCODE_F8: scancode = 0x42; break;
    case SDL_SCANCODE_F9: scancode = 0x43; break;
    case SDL_SCANCODE_F10: scancode = 0x44; break;
    case SDL_SCANCODE_HOME: scancode = 0x47; extended = 1; break;
    case SDL_SCANCODE_UP: scancode = 0x48; extended = 1; break;
    case SDL_SCANCODE_PAGEUP: scancode = 0x49; extended = 1; break;
    case SDL_SCANCODE_LEFT: scancode = 0x4b; extended = 1; break;
    case SDL_SCANCODE_RIGHT: scancode = 0x4d; extended = 1; break;
    case SDL_SCANCODE_END: scancode = 0x4f; extended = 1; break;
    case SDL_SCANCODE_DOWN: scancode = 0x50; extended = 1; break;
    case SDL_SCANCODE_PAGEDOWN: scancode = 0x51; extended = 1; break;
    case SDL_SCANCODE_INSERT: scancode = 0x52; extended = 1; break;
    case SDL_SCANCODE_DELETE: scancode = 0x53; extended = 1; break;
    case SDL_SCANCODE_F11: scancode = 0x57; break;
    case SDL_SCANCODE_F12: scancode = 0x58; break;
    default:
        scancode = (uint32_t)key->keysym.scancode & 0x7fu;
        break;
    }

    lparam = 1u | ((scancode & 0xffu) << 16);

    if (rb_event_is_system_key_event(key))
        lparam |= (1u << 29);
    if (extended)
        lparam |= (1u << 24);
    if (key->repeat || is_keyup)
        lparam |= (1u << 30);
    if (is_keyup)
        lparam |= (1u << 31);

    return lparam;
}

static int rb_event_translate_key_event(SDL_Event *sdl, rb_msg_t *msg)
{
    int vk;

    if (rb_event_take_watched_key(&sdl->key))
        return 0;
    if (!msg->hwnd)
        msg->hwnd = rb_event_get_active_window();
    if (!msg->hwnd)
        return 0;

    if (sdl->type == SDL_KEYDOWN)
        rb_event_note_alt_keydown(sdl->key.keysym.sym);

    vk = rb_keycode_to_vk(sdl->key.keysym.sym, sdl->key.keysym.scancode);
    if (vk < 0)
        return 0;

    rb_keyboard_note_key_event(vk, sdl->type == SDL_KEYDOWN,
                               sdl->type == SDL_KEYDOWN ? (sdl->key.repeat != 0) : 0);

    if (sdl->type == SDL_KEYDOWN &&
        rb_event_is_system_key_event(&sdl->key) && vk == VK_F4) {
        msg->message = WM_CLOSE;
        msg->wParam = 0;
        msg->lParam = 0;
        msg->time = (uint32_t)sdl->key.timestamp;
        DEBUG_WRITE_ERR("rb_event: Alt+F4 -> WM_CLOSE\n",
                        sizeof("rb_event: Alt+F4 -> WM_CLOSE\n") - 1);
        DEBUG_LEVEL(1, "rb_event: Alt+F4 -> WM_CLOSE hwnd=0x%lx",
                    (unsigned long)msg->hwnd);
        return 1;
    }

    if (sdl->type == SDL_KEYUP &&
        rb_event_is_system_key_event(&sdl->key) && vk == VK_F4) {
        rb_event_note_alt_keyup(sdl->key.keysym.sym);
        return 0;
    }

    msg->message = (sdl->type == SDL_KEYDOWN)
                   ? (rb_event_is_system_key_event(&sdl->key) ? WM_SYSKEYDOWN : WM_KEYDOWN)
                   : (rb_event_is_system_key_event(&sdl->key) ? WM_SYSKEYUP : WM_KEYUP);
    msg->wParam = (uint32_t)vk;
    msg->lParam = rb_build_key_lparam(&sdl->key, sdl->type == SDL_KEYUP);
    if (sdl->type == SDL_KEYDOWN)
        msg->lParam &= ~((intptr_t)1 << 30);
    msg->time = (uint32_t)sdl->key.timestamp;

    if (sdl->type == SDL_KEYUP)
        rb_event_note_alt_keyup(sdl->key.keysym.sym);

    return 1;
}

int rb_event_watch(void *userdata, SDL_Event *event)
{
    int vk;

    (void)userdata;
    if (!event)
        return 1;

    if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) {
        vk = rb_keycode_to_vk(event->key.keysym.sym, event->key.keysym.scancode);
        if (vk >= 0) {
            rb_keyboard_note_key_event(vk, event->type == SDL_KEYDOWN,
                                       event->key.repeat != 0);

            {
                uintptr_t hwnd = rb_event_resolve_hwnd_from_sdl_window(event->key.windowID);
                rb_msg_t msg;

                if (!hwnd)
                    hwnd = rb_event_get_active_window();
                if (hwnd) {
                    memset(&msg, 0, sizeof(msg));
                    msg.hwnd = hwnd;
                    msg.message = (event->type == SDL_KEYDOWN)
                                  ? (rb_event_is_system_key_event(&event->key) ? WM_SYSKEYDOWN : WM_KEYDOWN)
                                  : (rb_event_is_system_key_event(&event->key) ? WM_SYSKEYUP : WM_KEYUP);
                    msg.wParam = (uintptr_t)vk;
                    msg.lParam = (intptr_t)rb_build_key_lparam(&event->key,
                                                               event->type == SDL_KEYUP);
                    if (event->type == SDL_KEYDOWN)
                        msg.lParam &= ~((intptr_t)1 << 30);
                    msg.time = (uint32_t)event->key.timestamp;
                    rb_event_push_synthetic(&msg);
                    rb_event_note_watched_key(&event->key);
                }
            }
        }
    }

    return 1;
}

void rb_event_install_watch(void)
{
    static int installed = 0;

    if (installed)
        return;
    SDL_AddEventWatch(rb_event_watch, NULL);
    installed = 1;
}

int rb_event_translate_keyboard_or_text(SDL_Event *sdl, rb_msg_t *msg)
{
    if (!sdl || !msg)
        return 0;

    switch (sdl->type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        return rb_event_translate_key_event(sdl, msg);
    case SDL_TEXTINPUT:
        if (!msg->hwnd)
            msg->hwnd = rb_event_get_active_window();
        if (!msg->hwnd)
            return 0;
        msg->message = WM_CHAR;
        msg->wParam = sdl->text.text[0];
        msg->lParam = 0;
        msg->time = (uint32_t)sdl->text.timestamp;
        return 1;
    default:
        return 0;
    }
}
