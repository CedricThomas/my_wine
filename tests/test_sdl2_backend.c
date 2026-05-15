/*
 * test_sdl2_backend.c
 *
 * Comprehensive test for the SDL2 render backend.
 * Validates: init -> window -> surface -> palette -> lock/unlock ->
 * blt -> timer -> joystick -> keyboard -> shutdown.
 *
 * Build: make build/test_sdl2_backend
 * Run:   ./build/test_sdl2_backend [--headless]
 */

#include "render_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failed = 0;

#define T(cond, msg)                                                           \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "FAIL: %s\n", msg);                                      \
      failed++;                                                                \
    }                                                                          \
  } while (0)

static int run_tests(void) {
  /* ---- Init ---- */
  printf("  init... ");
  T(rb_init() == RB_OK, "rb_init failed");
  printf("OK\n");

  /* ---- Timer ---- */
  printf("  timer... ");
  uint32_t t1 = rb_timer_get_ticks();
  rb_timer_delay(10);
  uint32_t t2 = rb_timer_get_ticks();
  T(t2 >= t1, "timer not advancing");
  printf("OK\n");

  /* ---- Window ---- */
  printf("  window... ");
  rb_window_t win =
      rb_window_create("SDL2 Backend Test", -1, -1, 320, 200, RB_WINDOW_SHOWN);
  T(win != 0, "rb_window_create failed");

  rb_rect_t rect;
  if (win) {
    T(rb_window_get_rect(win, &rect) == RB_OK, "get_rect failed");
    T(rect.w == 320 && rect.h == 200, "window size mismatch");

    rb_window_get_client_rect(win, &rect);
    T(rect.w == 320 && rect.h == 200, "client rect mismatch");

    T(rb_window_set_title(win, "Renamed") == RB_OK, "set_title failed");
    T(rb_window_show(win, 0) == RB_OK, "show(hide) failed");
    T(rb_window_show(win, 1) == RB_OK, "show(show) failed");

    /* ---- DC ---- */
    rb_dc_t dc = rb_window_get_dc(win);
    T(rb_window_release_dc(win, dc) >= 0, "release_dc failed");

    /* ---- Cursor ---- */
    rb_cursor_t cur = rb_cursor_create(0); /* IDC_ARROW */
    if (cur) {
      T(rb_window_set_cursor(win, cur) == RB_OK, "set_cursor failed");
      T(rb_cursor_show(0) == RB_OK, "cursor_show failed");
      T(rb_cursor_destroy(cur) == RB_OK, "cursor_destroy failed");
    }

    rb_window_destroy(win);
  }
  printf("OK\n");

  /* ---- Palette ---- */
  printf("  palette... ");
  rb_palette_t pal = rb_palette_create(256);
  T(pal != 0, "rb_palette_create failed");

  if (pal) {
    uint32_t colors[256];
    for (int i = 0; i < 256; i++) {
      colors[i] = ((uint32_t)i << 16) | ((uint32_t)i << 8) | (uint32_t)i;
    }
    T(rb_palette_set_colors(pal, 0, 256, colors) == RB_OK, "set_colors failed");

    uint32_t read_colors[16];
    T(rb_palette_get_colors(pal, 0, 16, read_colors) == RB_OK,
      "get_colors failed");
    T(read_colors[0] == 0x00000000, "color[0] mismatch");
    T(read_colors[15] == 0x000F0F0F, "color[15] mismatch");
  }
  printf("OK\n");

  /* ---- Surface ---- */
  printf("  surface... ");
  rb_surface_t surf = rb_surface_create(320, 200, RB_FORMAT_8BIT, pal, 0);
  T(surf != 0, "rb_surface_create(8-bit) failed");

  if (surf) {
    int w, h, pitch;
    rb_pixel_format_t fmt;
    T(rb_surface_get_desc(surf, &w, &h, &fmt, &pitch) == RB_OK,
      "get_desc failed");
    T(w == 320 && h == 200 && fmt == RB_FORMAT_8BIT && pitch == 320,
      "surface desc mismatch");

    /* Lock, fill, unlock */
    uint8_t *data;
    int lock_pitch;
    T(rb_surface_lock(surf, NULL, &data, &lock_pitch) == RB_OK, "lock failed");
    if (data) {
      for (int y = 0; y < 200; y++) {
        memset(data + y * lock_pitch, (uint8_t)y, 320);
      }
    }
    T(rb_surface_unlock(surf) == RB_OK, "unlock failed");
  }
  printf("OK\n");

  /* ---- BLT ---- */
  printf("  blt... ");
  rb_surface_t surf2 = rb_surface_create(320, 200, RB_FORMAT_8BIT, pal, 0);
  T(surf2 != 0, "surface2 create failed");

  if (surf2 && surf) {
    T(rb_surface_blt(surf2, NULL, surf, NULL, 0, RB_BLT_SRCCOPY) == RB_OK,
      "blt SRCCOPY failed");

    uint8_t *data2;
    int lp;
    rb_surface_lock(surf2, NULL, &data2, &lp);
    if (data2) {
      rb_rect_t fill_rect = {10, 10, 20, 20};
      T(rb_surface_blt(surf2, &fill_rect, 0, NULL, 0xFF, RB_BLT_COLORFILL) ==
            RB_OK,
        "blt COLORFILL failed");
      T(data2[10 * lp + 10] == 0xFF, "color fill didn't take effect");
    }
    rb_surface_unlock(surf2);

    /* Set palette */
    T(rb_surface_set_palette(surf2, pal) == RB_OK, "set_palette failed");
  }
  printf("OK\n");

  /* ---- Joystick ---- */
  printf("  joystick... ");
  int joy_count = rb_joy_count();
  T(joy_count >= 0, "joy_count failed");
  printf("OK (%d)\n", joy_count);

  /* ---- Keyboard ---- */
  printf("  keyboard... ");
  int16_t state = rb_keyboard_get_async_state(0x20); /* VK_SPACE */
  (void)state; /* value depends on key state */
  printf("OK\n");

  /* ---- Event (peek) ---- */
  printf("  event... ");
  rb_msg_t msg;
  rb_event_peek(&msg);
  printf("OK\n");

  /* ---- Audio (may fail headless) ---- */
  printf("  audio... ");
  int audio_ok = rb_audio_open(22050, 2, 16, 4096);
  if (audio_ok == RB_OK) {
    rb_audio_buf_t buf = rb_audio_buffer_create(0, 4096);
    if (buf) {
      uint8_t *buf_data;
      uint32_t buf_len;
      T(rb_audio_buffer_lock(buf, 0, 4096, &buf_data, &buf_len) == RB_OK,
        "audio lock failed");
      T(buf_len > 0, "audio buffer len == 0");
      if (buf_data)
        memset(buf_data, 0, buf_len);
      T(rb_audio_buffer_unlock(buf, buf_data ? buf_data : (uint8_t *)0,
                               buf_len) == RB_OK,
        "audio unlock failed");
      T(rb_audio_buffer_set_volume(buf, -6000) == RB_OK, "set_volume failed");
      T(rb_audio_buffer_set_pan(buf, -5000) == RB_OK, "set_pan failed");
      T(rb_audio_buffer_set_frequency(buf, 22050) == RB_OK,
        "set_frequency failed");
      T(rb_audio_buffer_play(buf, 0) == RB_OK, "play failed");
      T(rb_audio_buffer_stop(buf) == RB_OK, "stop failed");
      rb_audio_buffer_destroy(buf);
    }
    rb_audio_close();
  }
  printf("%s\n", audio_ok == RB_OK ? "OK" : "SKIPPED");

  /* ---- Cleanup ---- */
  printf("  cleanup... ");
  if (surf2)
    rb_surface_destroy(surf2);
  if (surf)
    rb_surface_destroy(surf);
  if (pal)
    rb_palette_destroy(pal);
  rb_shutdown();
  printf("OK\n");

  return failed;
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  printf("SDL2 Backend Test\n\n");
  int failures = run_tests();
  if (failures == 0) {
    printf("\nPASS: All tests passed\n");
  } else {
    printf("\nFAIL: %d test(s) failed\n", failures);
  }
  return failures;
}
