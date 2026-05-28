#ifndef MY_WINE_WINMM_DOOM95_PRIV_H
#define MY_WINE_WINMM_DOOM95_PRIV_H

#include <pthread.h>
#include <stdint.h>

#include <fluidsynth.h>

typedef enum {
    MIDI_EVENT_SHORT = 0,
    MIDI_EVENT_TEMPO = 1,
    MIDI_EVENT_SYSEX = 2
} midi_event_type_t;

typedef struct midi_event {
    struct midi_event *next;
    uint32_t delta_ticks;
    midi_event_type_t type;
    uint32_t short_msg;
    uint32_t tempo;
    uint8_t *sysex;
    uint32_t sysex_len;
} midi_event_t;

typedef struct {
    fluid_settings_t *settings;
    fluid_synth_t *synth;
    fluid_audio_driver_t *driver;
    char soundfont_path[256];
    int soundfont_id;
    int ready;
} midi_fluidsynth_backend_t;

typedef struct {
    uint32_t stream_handle;
    uint32_t time_div;
    uint32_t tempo_us_per_qn;
    uint32_t volume;
    int app_active;
    int playing;
    int worker_active;
    volatile int stop_worker;
    volatile uint32_t generation;
    volatile int lock;
    pthread_t worker_thread;
    midi_event_t *head;
    midi_event_t *tail;
    midi_fluidsynth_backend_t backend;
} midi_stream_state_t;

int winmm_doom95_midi_backend_has_device(void);
int winmm_doom95_midi_backend_open(midi_fluidsynth_backend_t *backend,
                                   uint32_t volume);
void winmm_doom95_midi_backend_close(midi_fluidsynth_backend_t *backend);
void winmm_doom95_midi_backend_reset(midi_fluidsynth_backend_t *backend);
void winmm_doom95_midi_backend_apply_volume(midi_stream_state_t *state);
void winmm_doom95_midi_backend_send_short(midi_fluidsynth_backend_t *backend,
                                          uint32_t packed_msg);
void winmm_doom95_midi_backend_send_sysex(midi_fluidsynth_backend_t *backend,
                                          const uint8_t *data, uint32_t len);

#endif /* MY_WINE_WINMM_DOOM95_PRIV_H */
