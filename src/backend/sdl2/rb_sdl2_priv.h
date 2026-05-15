/*
 * rb_sdl2_priv.h
 *
 * Private internal state structures for the SDL2 backend implementation.
 * Included by all .c files in src/backend/sdl2/.
 * Defines the internal C structs that wrap SDL objects and are managed
 * through the handle manager.
 */

#ifndef RB_SDL2_PRIV_H
#define RB_SDL2_PRIV_H

#include <SDL2/SDL.h>
#include "render_backend.h"
#include "handle_manager.h"

/* ---- Handle type constants ---- */
/* Defined in handle_manager.h; listed here for reference:
 *   HANDLE_TYPE_HWIN       0x60
 *   HANDLE_TYPE_HBITMAP    → HANDLE_TYPE_BITMAP   0x30
 *   HANDLE_TYPE_HPALETTE   → HANDLE_TYPE_PALETTE  0x31
 *   HANDLE_TYPE_HCURSOR    0x33
 *   HANDLE_TYPE_DD_SURFACE 0x41
 *   HANDLE_TYPE_DD_PALETTE 0x61
 *   HANDLE_TYPE_DS_BUFFER  0x51
 */

/* ---- Private window state ---- */
struct rb_window {
    SDL_Window *window;
    rb_surface_t primary_surface; /* flip-chain primary surface handle */
};

/* ---- Private surface state ---- */
struct rb_surface {
    SDL_Surface *surface;
    uint8_t *own_buf;       /* malloc'd pixel buffer we own */
    rb_palette_t palette;   /* palette handle bound to this surface */
    int dirty;              /* flag: surface contents changed, needs update */
    rb_window_t window;     /* which window this surface is bound to */
    int pitch;              /* row stride in bytes */
};

/* ---- Private palette state ---- */
struct rb_palette {
    SDL_Palette *palette;
    int num_colors;
};

/* ---- Private audio buffer state ---- */
struct rb_audio_buf {
    uint8_t *data;
    int buffer_size;
    int format;           /* AUDIO_S16SYS, etc. */
    int playing;          /* 0 = stopped, 1 = playing */
    int loop;             /* 0 = no loop, 1 = loop */
    int volume;           /* -10000..0 */
    int pan;              /* -10000..10000 */
    uint32_t frequency;
    float gain;           /* 0.0..1.0, derived from volume */
    float pan_left;       /* 0.0..1.0 */
    float pan_right;      /* 0.0..1.0 */
};

/* ---- Private audio device state ---- */
struct rb_audio_state {
    SDL_AudioDeviceID device_id;
    int opened;           /* 0 = closed, 1 = open */
    int sample_rate;
    int channels;
    int bits_per_sample;
    int buffer_size;
};

/* ---- Private cursor state ---- */
struct rb_cursor {
    SDL_Cursor *cursor;
};

/* ---- Global audio state ---- */
extern struct rb_audio_state g_audio;
extern struct rb_audio_buf *g_audio_buffers[32];
extern int g_audio_buf_count;

/* ---- Audio callback (registered with SDL) ---- */
void rb_audio_callback(void *userdata, uint8_t *stream, int len);

/* ---- VK → SDL_Scancode mapping ---- */
static const int g_vk_to_scancode[256] = {
    [0x08] = SDL_SCANCODE_BACKSPACE,   /* VK_BACK        */
    [0x09] = SDL_SCANCODE_TAB,         /* VK_TAB         */
    [0x0D] = SDL_SCANCODE_RETURN,      /* VK_RETURN      */
    [0x13] = SDL_SCANCODE_PAUSE,       /* VK_PAUSE       */
    [0x1B] = SDL_SCANCODE_ESCAPE,      /* VK_ESCAPE      */
    [0x20] = SDL_SCANCODE_SPACE,       /* VK_SPACE       */
    [0x21] = SDL_SCANCODE_PAGEUP,      /* VK_PRIOR       */
    [0x22] = SDL_SCANCODE_PAGEDOWN,    /* VK_NEXT        */
    [0x23] = SDL_SCANCODE_END,         /* VK_END         */
    [0x24] = SDL_SCANCODE_HOME,        /* VK_HOME        */
    [0x25] = SDL_SCANCODE_LEFT,        /* VK_LEFT        */
    [0x26] = SDL_SCANCODE_UP,          /* VK_UP          */
    [0x27] = SDL_SCANCODE_RIGHT,       /* VK_RIGHT       */
    [0x28] = SDL_SCANCODE_DOWN,        /* VK_DOWN        */
    [0x2D] = SDL_SCANCODE_INSERT,      /* VK_INSERT      */
    [0x2E] = SDL_SCANCODE_DELETE,      /* VK_DELETE      */
    [0x30] = SDL_SCANCODE_0,
    [0x31] = SDL_SCANCODE_1,
    [0x32] = SDL_SCANCODE_2,
    [0x33] = SDL_SCANCODE_3,
    [0x34] = SDL_SCANCODE_4,
    [0x35] = SDL_SCANCODE_5,
    [0x36] = SDL_SCANCODE_6,
    [0x37] = SDL_SCANCODE_7,
    [0x38] = SDL_SCANCODE_8,
    [0x39] = SDL_SCANCODE_9,
    [0x41] = SDL_SCANCODE_A,           /* VK_A           */
    [0x42] = SDL_SCANCODE_B,           /* VK_B           */
    [0x43] = SDL_SCANCODE_C,           /* VK_C           */
    [0x44] = SDL_SCANCODE_D,           /* VK_D           */
    [0x45] = SDL_SCANCODE_E,           /* VK_E           */
    [0x46] = SDL_SCANCODE_F,           /* VK_F           */
    [0x47] = SDL_SCANCODE_G,           /* VK_G           */
    [0x48] = SDL_SCANCODE_H,           /* VK_H           */
    [0x49] = SDL_SCANCODE_I,           /* VK_I           */
    [0x4A] = SDL_SCANCODE_J,           /* VK_J           */
    [0x4B] = SDL_SCANCODE_K,           /* VK_K           */
    [0x4C] = SDL_SCANCODE_L,           /* VK_L           */
    [0x4D] = SDL_SCANCODE_M,           /* VK_M           */
    [0x4E] = SDL_SCANCODE_N,           /* VK_N           */
    [0x4F] = SDL_SCANCODE_O,           /* VK_O           */
    [0x50] = SDL_SCANCODE_P,           /* VK_P           */
    [0x51] = SDL_SCANCODE_Q,           /* VK_Q           */
    [0x52] = SDL_SCANCODE_R,           /* VK_R           */
    [0x53] = SDL_SCANCODE_S,           /* VK_S           */
    [0x54] = SDL_SCANCODE_T,           /* VK_T           */
    [0x55] = SDL_SCANCODE_U,           /* VK_U           */
    [0x56] = SDL_SCANCODE_V,           /* VK_V           */
    [0x57] = SDL_SCANCODE_W,           /* VK_W           */
    [0x58] = SDL_SCANCODE_X,           /* VK_X           */
    [0x59] = SDL_SCANCODE_Y,           /* VK_Y           */
    [0x5A] = SDL_SCANCODE_Z,           /* VK_Z           */
    [0x70] = SDL_SCANCODE_F1,          /* VK_F1          */
    [0x71] = SDL_SCANCODE_F2,          /* VK_F2          */
    [0x72] = SDL_SCANCODE_F3,          /* VK_F3          */
    [0x73] = SDL_SCANCODE_F4,          /* VK_F4          */
    [0x74] = SDL_SCANCODE_F5,          /* VK_F5          */
    [0x75] = SDL_SCANCODE_F6,          /* VK_F6          */
    [0x76] = SDL_SCANCODE_F7,          /* VK_F7          */
    [0x77] = SDL_SCANCODE_F8,          /* VK_F8          */
    [0x78] = SDL_SCANCODE_F9,          /* VK_F9          */
    [0x79] = SDL_SCANCODE_F10,         /* VK_F10         */
    [0x7A] = SDL_SCANCODE_F11,         /* VK_F11         */
    [0x7B] = SDL_SCANCODE_F12,         /* VK_F12         */
    [0xA0] = SDL_SCANCODE_LSHIFT,      /* VK_LSHIFT      */
    [0xA1] = SDL_SCANCODE_RSHIFT,      /* VK_RSHIFT      */
    [0xA2] = SDL_SCANCODE_LCTRL,       /* VK_LCONTROL    */
    [0xA3] = SDL_SCANCODE_RCTRL,       /* VK_RCONTROL    */
};

/* ---- Helper: VK code → SDL scancode ---- */
static inline int vk_to_scancode(int vk)
{
    int idx = vk & 0xFF;
    return g_vk_to_scancode[idx] ? g_vk_to_scancode[idx] : -1;
}

#endif /* RB_SDL2_PRIV_H */
