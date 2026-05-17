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

#include <assert.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include "render_backend.h"
#include "handle_manager.h"
#include "src/loader/loader_state.h"

extern wine_loader_state_t g_loader __attribute__((weak));
extern void *unix_stack_ptr_val __attribute__((weak));

/* ---- Handle type constants ---- */
/* Defined in handle_manager.h; listed here for reference:
 *   HANDLE_TYPE_HWIN       0x60
 *   HANDLE_TYPE_HBITMAP    → HANDLE_TYPE_BITMAP   0x30
 *   HANDLE_TYPE_HPALETTE   → HANDLE_TYPE_PALETTE  0x31
 *   HANDLE_TYPE_HCURSOR    0x33
 *   HANDLE_TYPE_DD_SURFACE 0x41
 *   HANDLE_TYPE_DD_PALETTE 0x61
 *   HANDLE_TYPE_DS_BUFFER  0x51
 *   HANDLE_TYPE_RB_WINDOW  0x63
 *   HANDLE_TYPE_RB_SURFACE 0x64
 *   HANDLE_TYPE_RB_PALETTE 0x65
 *   HANDLE_TYPE_RB_CURSOR  0x66
 */

/* ---- Private window state ---- */
typedef struct rb_window {
    SDL_Window *window;
    uint32_t sdl_window_id;
    uintptr_t native_window_id;
    uintptr_t guest_hwnd;
    rb_surface_t primary_surface; /* flip-chain primary surface handle */
    rb_surface_t backbuffer;      /* flip-chain backbuffer handle (owned by window) */
} rb_window;

/* ---- Private surface state ---- */
typedef struct rb_surface {
    SDL_Surface *surface;
    uint8_t *own_buf;       /* malloc'd pixel buffer we own */
    rb_palette_t palette;   /* palette handle bound to this surface */
    int dirty;              /* flag: surface contents changed, needs update */
    rb_window_t window;     /* which window this surface is bound to */
    int pitch;              /* row stride in bytes */
} rb_surface;

/* ---- Private palette state ---- */
typedef struct rb_palette {
    SDL_Palette *palette;
    int num_colors;
} rb_palette;

/* ---- Private audio buffer state ---- */
typedef struct rb_audio_buf {
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
} rb_audio_buf;

/* ---- Private audio device state ---- */
typedef struct rb_audio_state {
    SDL_AudioDeviceID device_id;
    int opened;           /* 0 = closed, 1 = open */
    int sample_rate;
    int channels;
    int bits_per_sample;
    int buffer_size;
} rb_audio_state;

/* ---- Private cursor state ---- */
typedef struct rb_cursor {
    SDL_Cursor *cursor;
} rb_cursor;

/*
 * Guest-facing USER32 stubs enter the SDL backend with GS (64-bit) / FS (32-bit)
 * pointing at the emulated TEB.  SDL/glibc expect the host selector, so restore
 * it for host library calls and put the guest value back before returning to
 * guest code.
 */
static inline uintptr_t rb_host_context_enter(void)
{
#if defined(__x86_64__)
    uintptr_t guest_gs;
    uintptr_t host_gs = (&g_loader != 0) ? loader_get_host_gs_base() : 0;

    if (&g_loader == 0)
        return 0;

    __asm__ volatile("rdgsbase %0" : "=r"(guest_gs));
    if (guest_gs != host_gs)
        __asm__ volatile("wrgsbase %0" :: "r"(host_gs));
    return guest_gs;
#elif defined(__i386__)
    uint16_t host_fs = (&g_loader != 0) ? loader_get_host_fs_selector() : 0;
    uint16_t guest_fs;

    if (&g_loader == 0)
        return 0;

    __asm__ volatile("mov %%fs, %0" : "=r"(guest_fs));
    if (guest_fs != host_fs && host_fs != 0)
        __asm__ volatile("mov %0, %%fs" :: "r"(host_fs) : "memory");
    return (uintptr_t)guest_fs;
#else
    return 0;
#endif
}

static inline void rb_host_context_leave(uintptr_t saved_gs)
{
#if defined(__x86_64__)
    if (&g_loader != 0 && saved_gs && saved_gs != loader_get_host_gs_base())
        __asm__ volatile("wrgsbase %0" :: "r"(saved_gs));
#elif defined(__i386__)
    uint16_t saved_fs = (uint16_t)saved_gs;
    uint16_t host_fs = (&g_loader != 0) ? loader_get_host_fs_selector() : 0;
    if (&g_loader != 0 && host_fs != 0 && saved_fs && saved_fs != host_fs)
        __asm__ volatile("mov %0, %%fs" :: "r"(saved_fs) : "memory");
#else
    (void)saved_gs;
#endif
}

typedef uintptr_t (*rb_host_call_fn)(void *);

static inline int rb_host_context_is_active(void)
{
#if defined(__x86_64__)
    if (&g_loader == 0)
        return 1;

    uintptr_t current_gs = 0;
    __asm__ volatile("rdgsbase %0" : "=r"(current_gs));
    return current_gs == loader_get_host_gs_base();
#elif defined(__i386__)
    if (&g_loader == 0)
        return 1;

    uint16_t host_fs = loader_get_host_fs_selector();
    uint16_t current_fs = 0;
    if (host_fs == 0)
        return 1;
    __asm__ volatile("mov %%fs, %0" : "=r"(current_fs));
    return current_fs == host_fs;
#else
    return 1;
#endif
}

static inline uintptr_t rb_call_on_host_stack(rb_host_call_fn fn, void *arg)
{
#if defined(__x86_64__)
    uintptr_t ret;
    uintptr_t old_rsp;
    uintptr_t saved_gs = rb_host_context_enter();

    if (&unix_stack_ptr_val == 0 || !unix_stack_ptr_val) {
        ret = fn(arg);
        rb_host_context_leave(saved_gs);
        return ret;
    }

    uintptr_t new_rsp = (uintptr_t)unix_stack_ptr_val & ~(uintptr_t)15;
    assert((new_rsp & 15) == 0);
    assert(rb_host_context_is_active());
    __asm__ volatile(
        "mov %%rsp,%[old_rsp]\n\t"
        "mov %[new_rsp],%%rsp\n\t"
        "call *%[fn]\n\t"
        "mov %[old_rsp],%%rsp\n\t"
        : "=a"(ret), [old_rsp] "=&r"(old_rsp)
        : [new_rsp] "r"(new_rsp), [fn] "r"(fn), "D"(arg)
        : "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11", "memory", "cc");

    rb_host_context_leave(saved_gs);
    return ret;
#elif defined(__i386__)
    uintptr_t ret;
    uintptr_t old_esp;
    uintptr_t saved_fs = rb_host_context_enter();

    if (&unix_stack_ptr_val == 0 || !unix_stack_ptr_val) {
        ret = fn(arg);
        rb_host_context_leave(saved_fs);
        return ret;
    }

    uintptr_t new_esp = ((uintptr_t)unix_stack_ptr_val & ~(uintptr_t)15) - 8;
    assert((new_esp & 15) == 8);
    assert(rb_host_context_is_active());
    __asm__ volatile(
        "push %%ebp\n\t"
        "push %%ebx\n\t"
        "push %%esi\n\t"
        "push %%edi\n\t"
        "mov %%esp, %[old_esp]\n\t"
        "mov %[new_esp], %%esp\n\t"
        "push %[old_esp]\n\t"
        "push %[arg]\n\t"
        "call *%[fn]\n\t"
        "addl $4, %%esp\n\t"
        "mov (%%esp), %%esp\n\t"
        "pop %%edi\n\t"
        "pop %%esi\n\t"
        "pop %%ebx\n\t"
        "pop %%ebp\n\t"
        : "=a"(ret), [old_esp] "=&r"(old_esp)
        : [new_esp] "r"(new_esp), [fn] "r"(fn), [arg] "r"(arg)
        : "ecx", "edx", "memory", "cc"
    );

    rb_host_context_leave(saved_fs);
    return ret;
#else
    return fn(arg);
#endif
}

static inline uintptr_t rb_host_malloc_call(void *arg)
{
    return (uintptr_t)malloc(*(size_t *)arg);
}

typedef struct {
    size_t nmemb;
    size_t size;
} rb_host_calloc_args;

static inline uintptr_t rb_host_calloc_call(void *arg)
{
    rb_host_calloc_args *a = arg;
    return (uintptr_t)calloc(a->nmemb, a->size);
}

static inline uintptr_t rb_host_free_call(void *arg)
{
    free(arg);
    return 0;
}

static inline uintptr_t rb_host_getenv_call(void *arg)
{
    return (uintptr_t)getenv((const char *)arg);
}

static inline void *rb_host_malloc(size_t size)
{
    return (void *)rb_call_on_host_stack(rb_host_malloc_call, &size);
}

static inline void *rb_host_calloc(size_t nmemb, size_t size)
{
    rb_host_calloc_args args = { nmemb, size };
    return (void *)rb_call_on_host_stack(rb_host_calloc_call, &args);
}

static inline void rb_host_free(void *ptr)
{
    if (ptr)
        rb_call_on_host_stack(rb_host_free_call, ptr);
}

static inline const char *rb_host_getenv(const char *name)
{
    return (const char *)rb_call_on_host_stack(rb_host_getenv_call, (void *)name);
}

/* ---- Global audio state ---- */
extern rb_audio_state g_audio;
extern rb_audio_buf *g_audio_buffers[32];
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
    [0xA4] = SDL_SCANCODE_LALT,        /* VK_LMENU       */
    [0xA5] = SDL_SCANCODE_RALT,        /* VK_RMENU       */
    [0x10] = SDL_SCANCODE_LSHIFT,      /* VK_SHIFT (generic → left) */
    [0x11] = SDL_SCANCODE_LCTRL,       /* VK_CONTROL (generic → left) */
    [0x12] = SDL_SCANCODE_LALT,        /* VK_MENU (generic → left) */
};

/* ---- Helper: VK code → SDL scancode ---- */
static inline int vk_to_scancode(int vk)
{
    int idx = vk & 0xFF;
    return g_vk_to_scancode[idx] ? g_vk_to_scancode[idx] : -1;
}

static inline int scancode_to_vk(SDL_Scancode scancode)
{
    int vk;

    for (vk = 0; vk < 256; vk++) {
        if (g_vk_to_scancode[vk] == (int)scancode)
            return vk;
    }

    return -1;
}

/* ---- Event system helpers ---- */
void rb_event_set_active_window(uintptr_t hwnd);
uintptr_t rb_event_get_active_window(void);
int rb_event_bind_window(uintptr_t hwnd, rb_window_t win);
void rb_event_unbind_window(uintptr_t hwnd);
uint32_t rb_event_get_sdl_window_id(uintptr_t hwnd);
uintptr_t rb_x11_consume_bad_window(void);
int rb_runtime_consume_shutdown_request(void);

#endif /* RB_SDL2_PRIV_H */
