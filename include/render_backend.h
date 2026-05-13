/*
 * render_backend.h
 *
 * Abstraction between Windows API stubs and the rendering backend (SDL2).
 * This file defines the interface that user32.c, gdi32.c, ddraw.c, dsound.c
 * all call into.  A different backend can be swapped by providing a different
 * .c implementation of these functions.
 */

#ifndef RENDER_BACKEND_H
#define RENDER_BACKEND_H

#include <stdint.h>

/* ---- Opaque handle types ---- */
typedef uintptr_t rb_window_t;       /* HWND-equivalent      */
typedef uintptr_t rb_surface_t;      /* HBITMAP / DDSurface  */
typedef uintptr_t rb_palette_t;      /* HPALETTE / DDPalette */
typedef uintptr_t rb_audio_buf_t;    /* DirectSound buffer   */
typedef uintptr_t rb_dc_t;           /* HDC-equivalent       */
typedef uintptr_t rb_cursor_t;       /* HCURSOR-equivalent   */
typedef uintptr_t rb_font_t;         /* HFONT-equivalent     */

/* ---- Pixel format ---- */
typedef enum {
    RB_FORMAT_8BIT  = 8,
    RB_FORMAT_15BIT = 15,
    RB_FORMAT_16BIT = 16,
    RB_FORMAT_32BIT = 32,
} rb_pixel_format_t;

/* ---- Rect ---- */
typedef struct {
    int32_t x, y, w, h;
} rb_rect_t;

/* ---- Initialization / Shutdown ---- */
int rb_init(void);
void rb_shutdown(void);

/* ---- Window Lifecycle ---- */
rb_window_t rb_window_create(const char *title,
                             int x, int y, int w, int h,
                             uint32_t flags);
int rb_window_destroy(rb_window_t win);
int rb_window_show(rb_window_t win, int show);  /* show: 1=show, 0=hide */
int rb_window_set_position(rb_window_t win, int x, int y);
int rb_window_set_size(rb_window_t win, int w, int h);
int rb_window_set_title(rb_window_t win, const char *title);
int rb_window_get_rect(rb_window_t win, rb_rect_t *rect);
int rb_window_get_client_rect(rb_window_t win, rb_rect_t *rect);
int rb_window_set_fullscreen(rb_window_t win, int fullscreen, int w, int h, int bpp);

/* ---- Surface Lifecycle ---- */
rb_surface_t rb_surface_create(int w, int h, rb_pixel_format_t format,
                               rb_palette_t palette, uint32_t flags);
rb_surface_t rb_surface_create_flip_chain(rb_window_t win,
                                          int w, int h,
                                          rb_pixel_format_t format,
                                          rb_palette_t palette,
                                          int backbuffer_count);
int rb_surface_destroy(rb_surface_t surf);
int rb_surface_lock(rb_surface_t surf, const rb_rect_t *rect,
                    uint8_t **out_data, int *out_pitch);
int rb_surface_unlock(rb_surface_t surf);
int rb_surface_blt(rb_surface_t dst, const rb_rect_t *dst_rect,
                   rb_surface_t src, const rb_rect_t *src_rect,
                   uint32_t color, uint32_t flags);  /* RB_BLT_COLORFILL, RB_BLT_SRCCOPY */
int rb_surface_flip(rb_surface_t surf);
int rb_surface_get_desc(rb_surface_t surf,
                        int *w, int *h, rb_pixel_format_t *format,
                        int *pitch);

/* ---- Palette ---- */
rb_palette_t rb_palette_create(int num_colors);
int rb_palette_destroy(rb_palette_t pal);
int rb_palette_set_colors(rb_palette_t pal,
                          uint32_t start, uint32_t count,
                          const uint32_t *colors);  /* colors as 0x00BBGGRR */
int rb_surface_set_palette(rb_surface_t surf, rb_palette_t pal);
int rb_palette_get_colors(rb_palette_t pal,
                          uint32_t start, uint32_t count,
                          uint32_t *colors);

/* ---- DC (Device Context) ---- */
rb_dc_t rb_window_get_dc(rb_window_t win);
int rb_window_release_dc(rb_window_t win, rb_dc_t dc);

/* ---- Cursor ---- */
rb_cursor_t rb_cursor_create(int idc);  /* IDC_ARROW=0, IDC_CROSS=1, etc. */
int rb_cursor_destroy(rb_cursor_t cur);
int rb_window_set_cursor(rb_window_t win, rb_cursor_t cur);
int rb_cursor_show(int show);
int rb_window_warp_mouse(rb_window_t win, int x, int y);

/* ---- Event System ---- */
typedef struct {
    uintptr_t hwnd;
    uint32_t  message;   /* WM_* constant */
    uint32_t  wParam;
    int32_t   lParam;
    uint32_t  time;
    int32_t   pt_x, pt_y;
} rb_msg_t;

int rb_event_wait(rb_msg_t *out_msg);   /* Blocking. Returns 0 on WM_QUIT. */
int rb_event_peek(rb_msg_t *out_msg);   /* Non-blocking. 1=available, 0=empty. */
int rb_event_push(rb_msg_t *msg);       /* Push message into queue. */

/* ---- Audio ---- */
int rb_audio_open(int sample_rate, int channels, int bits_per_sample,
                  int buffer_size);
void rb_audio_close(void);
rb_audio_buf_t rb_audio_buffer_create(int format, int buffer_size);
int rb_audio_buffer_destroy(rb_audio_buf_t buf);
int rb_audio_buffer_lock(rb_audio_buf_t buf,
                         uint32_t offset, uint32_t bytes,
                         uint8_t **out_ptr, uint32_t *out_len);
int rb_audio_buffer_unlock(rb_audio_buf_t buf,
                           const uint8_t *ptr, uint32_t len);
int rb_audio_buffer_play(rb_audio_buf_t buf, int loop);
int rb_audio_buffer_stop(rb_audio_buf_t buf);
int rb_audio_buffer_set_volume(rb_audio_buf_t buf, int volume);  /* -10000..0 */
int rb_audio_buffer_set_pan(rb_audio_buf_t buf, int pan);         /* -10000..10000 */
int rb_audio_buffer_set_frequency(rb_audio_buf_t buf, uint32_t freq);

/* ---- Timer ---- */
uint32_t rb_timer_get_ticks(void);
void rb_timer_delay(uint32_t ms);

/* ---- Joystick ---- */
int rb_joy_count(void);
int rb_joy_get_caps(int idx, char *name, int name_len,
                    int *n_axes, int *n_buttons,
                    uint16_t *min, uint16_t *max);
int rb_joy_get_state(int idx,
                     uint16_t *axes, int n_axes,
                     uint8_t *buttons, int n_buttons);

/* ---- Keyboard ---- */
int16_t rb_keyboard_get_async_state(int vk);

/* ---- Constants / Flags ---- */
#define RB_HINT_AUTO      -1

#define RB_WINDOW_FULLSCREEN    (1 << 0)
#define RB_WINDOW_RESIZABLE     (1 << 1)
#define RB_WINDOW_SHOWN         (1 << 2)

#define RB_SURFACE_FLIP     (1 << 0)
#define RB_SURFACE_OFFSCREEN (1 << 1)
#define RB_SURFACE_PRIMARY  (1 << 2)
#define RB_SURFACE_BACK     (1 << 3)

#define RB_BLT_SRCCOPY      (1 << 0)
#define RB_BLT_COLORFILL    (1 << 1)

#define RB_OK                0
#define RB_FAIL              -1

#endif /* RENDER_BACKEND_H */
