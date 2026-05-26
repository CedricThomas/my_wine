#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include <fluidsynth.h>

#include "kernel32_priv.h"
#include "include/handle_manager.h"
#include "src/backend/sdl2/rb_sdl2_priv.h"

extern int rb_joy_count(void) __attribute__((weak));
extern int rb_joy_get_caps(int idx, char *name, int name_len,
                           int *n_axes, int *n_buttons,
                           uint16_t *min, uint16_t *max) __attribute__((weak));
extern int rb_joy_get_state(int idx, uint16_t *axes, int n_axes,
                            uint8_t *buttons, int n_buttons) __attribute__((weak));

typedef struct {
    uint16_t wMid;
    uint16_t wPid;
    char szPname[32];
    uint32_t wXmin;
    uint32_t wXmax;
    uint32_t wYmin;
    uint32_t wYmax;
    uint32_t wZmin;
    uint32_t wZmax;
    uint32_t wNumButtons;
    uint32_t wPeriodMin;
    uint32_t wPeriodMax;
    uint32_t wRmin;
    uint32_t wRmax;
    uint32_t wUmin;
    uint32_t wUmax;
    uint32_t wVmin;
    uint32_t wVmax;
    uint32_t wCaps;
    uint32_t wMaxAxes;
    uint32_t wNumAxes;
    uint32_t wMaxButtons;
    char szRegKey[32];
    char szOEMVxD[260];
} JOYCAPSA_WINE;

typedef struct {
    uint32_t dwSize;
    uint32_t dwFlags;
    uint32_t dwXpos;
    uint32_t dwYpos;
    uint32_t dwZpos;
    uint32_t dwRpos;
    uint32_t dwUpos;
    uint32_t dwVpos;
    uint32_t dwButtons;
    uint32_t dwButtonNumber;
    uint32_t dwPOV;
    uint32_t dwReserved1;
    uint32_t dwReserved2;
} JOYINFOEX_WINE;

typedef struct {
    char *lpData;
    uint32_t dwBufferLength;
    uint32_t dwBytesRecorded;
    uintptr_t dwUser;
    uint32_t dwFlags;
    uintptr_t lpNext;
    uintptr_t reserved;
    uint32_t dwOffset;
    uintptr_t dwReserved[8];
} MIDIHDR_WINE;

typedef struct {
    uint32_t cbStruct;
    uint32_t dwTempo;
} MIDIPROPTEMPO_WINE;

typedef struct {
    uint32_t cbStruct;
    uint32_t dwTimeDiv;
} MIDIPROPTIMEDIV_WINE;

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

typedef struct {
    midi_stream_state_t *state;
} midi_stream_state_args_t;

static midi_stream_state_t g_midi_stream = {
    .stream_handle = 0,
    .time_div = 96u,
    .tempo_us_per_qn = 500000u,
    .volume = 0xffffffffu,
    .app_active = 1,
    .playing = 0,
    .worker_active = 0,
    .stop_worker = 0,
    .generation = 1u,
    .lock = 0,
    .head = NULL,
    .tail = NULL
};

#define MHDR_DONE_WINE         0x00000001u
#define MHDR_PREPARED_WINE     0x00000002u
#define MIDIPROP_GET_WINE      0x40000000u
#define MIDIPROP_SET_WINE      0x80000000u
#define MIDIPROP_TIMEDIV_WINE  0x00000001u
#define MIDIPROP_TEMPO_WINE    0x00000002u
#define MEVT_F_CALLBACK_WINE   0x40000000u
#define MEVT_F_LONG_WINE       0x80000000u
#define MEVT_EVENTTYPE_WINE(x) ((uint8_t)(((x) >> 24) & 0xFFu))
#define MEVT_EVENTPARM_WINE(x) ((uint32_t)((x) & 0x00FFFFFFu))
#define MEVT_NOP_WINE          0x02u
#define MEVT_SHORTMSG_WINE     0x00u
#define MEVT_TEMPO_WINE        0x01u
#define MEVT_LONGMSG_WINE      0x80u
#define MEVT_COMMENT_WINE      0x82u
#define MEVT_VERSION_WINE      0x84u

static void midi_queue_clear_local(midi_stream_state_t *state);
static uintptr_t midi_stream_start_worker_host_call(void *arg);
static uintptr_t midi_stream_stop_worker_host_call(void *arg);
static uintptr_t midi_backend_apply_volume_host_call(void *arg);

static void midi_lock(volatile int *lock_word)
{
    while (__atomic_exchange_n(lock_word, 1, __ATOMIC_ACQUIRE) != 0) {
    }
}

static void midi_unlock(volatile int *lock_word)
{
    __atomic_store_n(lock_word, 0, __ATOMIC_RELEASE);
}

static void midi_sleep_ns(uint64_t ns)
{
    struct timespec ts;

    ts.tv_sec = (time_t)(ns / 1000000000ULL);
    ts.tv_nsec = (long)(ns % 1000000000ULL);
    (void)nanosleep(&ts, NULL);
}

static uint64_t midi_host_now_ns(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint64_t midi_ticks_to_ns(uint32_t ticks, uint32_t tempo_us_per_qn, uint32_t time_div)
{
    if (ticks == 0u || tempo_us_per_qn == 0u || time_div == 0u)
        return 0u;
    return ((uint64_t)ticks * (uint64_t)tempo_us_per_qn * 1000ULL) / (uint64_t)time_div;
}

static float midi_stream_gain_from_volume(uint32_t dwVolume)
{
    uint32_t left = dwVolume & 0xFFFFu;
    uint32_t right = (dwVolume >> 16) & 0xFFFFu;

    return (float)(left + right) / (float)(2u * 65535u);
}

static void midi_event_free_local(midi_event_t *event)
{
    if (!event)
        return;
    free(event->sysex);
    free(event);
}

static uintptr_t midi_queue_clear_host_call(void *arg)
{
    midi_queue_clear_local((midi_stream_state_t *)arg);
    return 0;
}

static void midi_queue_clear_local(midi_stream_state_t *state)
{
    midi_event_t *event;

    if (!state)
        return;

    midi_lock(&state->lock);
    event = state->head;
    state->head = NULL;
    state->tail = NULL;
    midi_unlock(&state->lock);

    while (event) {
        midi_event_t *next = event->next;
        midi_event_free_local(event);
        event = next;
    }
}

static int midi_find_soundfont(char *out_path, size_t out_size)
{
    static const char *fallbacks[] = {
        "/usr/share/soundfonts/FluidR3_GM.sf2",
        "/usr/share/soundfonts/FluidR3_GS.sf2"
    };
    const char *env_path = getenv("MY_WINE_SOUNDFONT");
    size_t i;

    if (out_path == NULL || out_size == 0u)
        return 0;

    if (env_path && env_path[0] != '\0' && access(env_path, R_OK) == 0) {
        snprintf(out_path, out_size, "%s", env_path);
        return 1;
    }

    for (i = 0; i < sizeof(fallbacks) / sizeof(fallbacks[0]); i++) {
        if (access(fallbacks[i], R_OK) == 0) {
            snprintf(out_path, out_size, "%s", fallbacks[i]);
            return 1;
        }
    }

    return 0;
}

static int midi_backend_open_local(midi_fluidsynth_backend_t *backend, uint32_t volume)
{
    static const char *driver_candidates[] = { "pulseaudio", "pipewire", "alsa" };
    fluid_settings_t *settings = NULL;
    fluid_synth_t *synth = NULL;
    fluid_audio_driver_t *driver = NULL;
    const char *driver_override = getenv("MY_WINE_FLUID_DRIVER");
    const char *driver_name = NULL;
    size_t i;
    int soundfont_id = -1;

    if (!backend)
        return 0;
    if (backend->ready)
        return 1;
    if (!midi_find_soundfont(backend->soundfont_path, sizeof(backend->soundfont_path))) {
        DEBUG_LEVEL(1, "winmm: no readable soundfont found");
        return 0;
    }

    for (i = 0; ; i++) {
        settings = new_fluid_settings();
        if (!settings)
            break;

        if (driver_override && driver_override[0] != '\0')
            driver_name = driver_override;
        else if (i < sizeof(driver_candidates) / sizeof(driver_candidates[0]))
            driver_name = driver_candidates[i];
        else
            driver_name = NULL;

        if (driver_name != NULL)
            (void)fluid_settings_setstr(settings, "audio.driver", driver_name);
        (void)fluid_settings_setnum(settings, "synth.gain", (double)midi_stream_gain_from_volume(volume));

        synth = new_fluid_synth(settings);
        if (!synth) {
            delete_fluid_settings(settings);
            settings = NULL;
            break;
        }

        soundfont_id = fluid_synth_sfload(synth, backend->soundfont_path, 1);
        if (soundfont_id < 0) {
            delete_fluid_synth(synth);
            delete_fluid_settings(settings);
            synth = NULL;
            settings = NULL;
            if (driver_override && driver_override[0] != '\0')
                break;
            continue;
        }

        driver = new_fluid_audio_driver(settings, synth);
        if (driver) {
            backend->settings = settings;
            backend->synth = synth;
            backend->driver = driver;
            backend->soundfont_id = soundfont_id;
            backend->ready = 1;
            DEBUG_LEVEL(1, "winmm: FluidSynth ready driver=%s soundfont=%s",
                        driver_name ? driver_name : "default",
                        backend->soundfont_path);
            return 1;
        }

        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
        synth = NULL;
        settings = NULL;
        if (driver_override && driver_override[0] != '\0')
            break;
    }

    DEBUG_LEVEL(1, "winmm: failed to start FluidSynth backend");
    return 0;
}

static void midi_backend_close_local(midi_fluidsynth_backend_t *backend)
{
    if (!backend)
        return;
    if (backend->driver)
        delete_fluid_audio_driver(backend->driver);
    if (backend->synth)
        delete_fluid_synth(backend->synth);
    if (backend->settings)
        delete_fluid_settings(backend->settings);
    memset(backend, 0, sizeof(*backend));
}

static void midi_backend_reset_local(midi_fluidsynth_backend_t *backend)
{
    if (!backend || !backend->ready || !backend->synth)
        return;
    (void)fluid_synth_system_reset(backend->synth);
}

static void midi_backend_apply_volume_local(midi_stream_state_t *state)
{
    float gain;

    if (!state || !state->backend.ready || !state->backend.synth)
        return;

    gain = midi_stream_gain_from_volume(__atomic_load_n(&state->volume, __ATOMIC_ACQUIRE));
    fluid_synth_set_gain(state->backend.synth, gain);
}

static void midi_backend_send_short_local(midi_fluidsynth_backend_t *backend, uint32_t packed_msg)
{
    uint8_t status = (uint8_t)(packed_msg & 0xFFu);
    uint8_t data1 = (uint8_t)((packed_msg >> 8) & 0x7Fu);
    uint8_t data2 = (uint8_t)((packed_msg >> 16) & 0x7Fu);
    int channel = status & 0x0Fu;
    int command = status & 0xF0u;

    if (!backend || !backend->ready || !backend->synth)
        return;

    switch (command) {
    case 0x80:
        (void)fluid_synth_noteoff(backend->synth, channel, data1);
        break;
    case 0x90:
        if (data2 == 0u)
            (void)fluid_synth_noteoff(backend->synth, channel, data1);
        else
            (void)fluid_synth_noteon(backend->synth, channel, data1, data2);
        break;
    case 0xA0:
        (void)fluid_synth_key_pressure(backend->synth, channel, data1, data2);
        break;
    case 0xB0:
        (void)fluid_synth_cc(backend->synth, channel, data1, data2);
        break;
    case 0xC0:
        (void)fluid_synth_program_change(backend->synth, channel, data1);
        break;
    case 0xD0:
        (void)fluid_synth_channel_pressure(backend->synth, channel, data1);
        break;
    case 0xE0:
        (void)fluid_synth_pitch_bend(backend->synth, channel,
                                     (int)data1 | ((int)data2 << 7));
        break;
    default:
        break;
    }
}

static void midi_backend_send_sysex_local(midi_fluidsynth_backend_t *backend,
                                          const uint8_t *data, uint32_t len)
{
    int handled = 0;

    if (!backend || !backend->ready || !backend->synth || !data || len == 0u)
        return;

    (void)fluid_synth_sysex(backend->synth, (const char *)data, (int)len,
                            NULL, NULL, &handled, 0);
}

static void *midi_stream_worker_thread(void *arg)
{
    midi_stream_state_t *state = (midi_stream_state_t *)arg;

    if (!state)
        return NULL;

    while (!__atomic_load_n(&state->stop_worker, __ATOMIC_ACQUIRE)) {
        midi_event_t *event = NULL;
        uint32_t tempo_us_per_qn;
        uint32_t time_div;
        uint32_t generation;
        uint64_t remaining_ns;

        if (!__atomic_load_n(&state->app_active, __ATOMIC_ACQUIRE) ||
            !__atomic_load_n(&state->playing, __ATOMIC_ACQUIRE)) {
            midi_sleep_ns(2000000ULL);
            continue;
        }

        if (!state->backend.ready) {
            if (!midi_backend_open_local(&state->backend,
                                         __atomic_load_n(&state->volume, __ATOMIC_ACQUIRE))) {
                midi_sleep_ns(500000000ULL);
                continue;
            }
            midi_backend_apply_volume_local(state);
        }

        midi_lock(&state->lock);
        if (state->head != NULL) {
            event = state->head;
            state->head = event->next;
            if (state->head == NULL)
                state->tail = NULL;
        }
        tempo_us_per_qn = __atomic_load_n(&state->tempo_us_per_qn, __ATOMIC_ACQUIRE);
        time_div = __atomic_load_n(&state->time_div, __ATOMIC_ACQUIRE);
        generation = __atomic_load_n(&state->generation, __ATOMIC_ACQUIRE);
        midi_unlock(&state->lock);

        if (!event) {
            midi_sleep_ns(2000000ULL);
            continue;
        }

        remaining_ns = midi_ticks_to_ns(event->delta_ticks, tempo_us_per_qn, time_div);
        while (remaining_ns != 0u &&
               !__atomic_load_n(&state->stop_worker, __ATOMIC_ACQUIRE)) {
            uint64_t slice = remaining_ns > 5000000ULL ? 5000000ULL : remaining_ns;
            uint64_t before;
            uint64_t after;

            if (__atomic_load_n(&state->generation, __ATOMIC_ACQUIRE) != generation)
                break;
            if (!__atomic_load_n(&state->app_active, __ATOMIC_ACQUIRE) ||
                !__atomic_load_n(&state->playing, __ATOMIC_ACQUIRE)) {
                midi_sleep_ns(2000000ULL);
                continue;
            }

            before = midi_host_now_ns();
            midi_sleep_ns(slice);
            after = midi_host_now_ns();
            if (after > before) {
                uint64_t elapsed = after - before;
                remaining_ns = (elapsed >= remaining_ns) ? 0u : (remaining_ns - elapsed);
            }
        }

        if (__atomic_load_n(&state->stop_worker, __ATOMIC_ACQUIRE)) {
            midi_event_free_local(event);
            break;
        }
        if (__atomic_load_n(&state->generation, __ATOMIC_ACQUIRE) != generation) {
            midi_event_free_local(event);
            continue;
        }

        switch (event->type) {
        case MIDI_EVENT_SHORT:
            midi_backend_send_short_local(&state->backend, event->short_msg);
            break;
        case MIDI_EVENT_TEMPO:
            __atomic_store_n(&state->tempo_us_per_qn, event->tempo, __ATOMIC_RELEASE);
            break;
        case MIDI_EVENT_SYSEX:
            midi_backend_send_sysex_local(&state->backend, event->sysex, event->sysex_len);
            break;
        }

        midi_event_free_local(event);
    }

    midi_backend_reset_local(&state->backend);
    midi_backend_close_local(&state->backend);
    return NULL;
}

static uintptr_t midi_stream_start_worker_host_call(void *arg)
{
    midi_stream_state_t *state = (midi_stream_state_t *)arg;

    if (!state || state->worker_active)
        return state ? (uintptr_t)state->worker_active : 0u;

    __atomic_store_n(&state->stop_worker, 0, __ATOMIC_RELEASE);
    if (pthread_create(&state->worker_thread, NULL, midi_stream_worker_thread, state) != 0)
        return 0u;

    state->worker_active = 1;
    return 1u;
}

static uintptr_t midi_stream_stop_worker_host_call(void *arg)
{
    midi_stream_state_t *state = (midi_stream_state_t *)arg;

    if (!state)
        return 0u;
    if (state->worker_active) {
        __atomic_store_n(&state->stop_worker, 1, __ATOMIC_RELEASE);
        (void)pthread_join(state->worker_thread, NULL);
        state->worker_active = 0;
    }
    midi_queue_clear_local(state);
    return 0u;
}

static uintptr_t midi_backend_apply_volume_host_call(void *arg)
{
    midi_backend_apply_volume_local((midi_stream_state_t *)arg);
    return 0u;
}

static void midi_stream_reset_state(midi_stream_state_t *state)
{
    if (!state)
        return;

    __atomic_add_fetch(&state->generation, 1u, __ATOMIC_ACQ_REL);
    __atomic_store_n(&state->playing, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&state->app_active, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&state->tempo_us_per_qn, 500000u, __ATOMIC_RELEASE);
    __atomic_store_n(&state->time_div, 96u, __ATOMIC_RELEASE);
    __atomic_store_n(&state->volume, 0xffffffffu, __ATOMIC_RELEASE);
    (void)rb_call_on_host_stack(midi_queue_clear_host_call, state);
}

void winmm_doom95_set_application_active(int active)
{
    __atomic_store_n(&g_midi_stream.app_active, active ? 1 : 0, __ATOMIC_RELEASE);
}

void winmm_doom95_shutdown(void)
{
    (void)rb_call_on_host_stack(midi_stream_stop_worker_host_call, &g_midi_stream);
    midi_stream_reset_state(&g_midi_stream);
}

static int midi_queue_push_event(midi_stream_state_t *state, midi_event_t *event)
{
    if (!state || !event)
        return 0;

    event->next = NULL;
    midi_lock(&state->lock);
    if (state->tail)
        state->tail->next = event;
    else
        state->head = event;
    state->tail = event;
    midi_unlock(&state->lock);
    return 1;
}

static midi_event_t *midi_event_new(midi_event_type_t type)
{
    midi_event_t *event = (midi_event_t *)rb_host_calloc(1u, sizeof(*event));

    if (event)
        event->type = type;
    return event;
}

static int midi_queue_short_message(midi_stream_state_t *state, uint32_t delta_ticks,
                                    uint32_t packed_msg)
{
    midi_event_t *event = midi_event_new(MIDI_EVENT_SHORT);

    if (!event)
        return 0;
    event->delta_ticks = delta_ticks;
    event->short_msg = packed_msg;
    return midi_queue_push_event(state, event);
}

static int midi_queue_tempo(midi_stream_state_t *state, uint32_t delta_ticks, uint32_t tempo)
{
    midi_event_t *event = midi_event_new(MIDI_EVENT_TEMPO);

    if (!event)
        return 0;
    event->delta_ticks = delta_ticks;
    event->tempo = tempo ? tempo : 500000u;
    return midi_queue_push_event(state, event);
}

static int midi_queue_sysex(midi_stream_state_t *state, uint32_t delta_ticks,
                            const uint8_t *data, uint32_t len)
{
    midi_event_t *event = midi_event_new(MIDI_EVENT_SYSEX);

    if (!event)
        return 0;
    event->delta_ticks = delta_ticks;
    event->sysex_len = len;
    if (len != 0u) {
        event->sysex = (uint8_t *)rb_host_malloc(len);
        if (!event->sysex) {
            rb_host_free(event);
            return 0;
        }
        memcpy(event->sysex, data, len);
    }
    return midi_queue_push_event(state, event);
}

static int midi_stream_append_buffer(midi_stream_state_t *state, const MIDIHDR_WINE *hdr)
{
    const uint8_t *ptr;
    uint32_t remaining;
    uint32_t pending_delta = 0u;

    if (!state || !hdr || !hdr->lpData)
        return 0;

    ptr = (const uint8_t *)hdr->lpData;
    remaining = hdr->dwBytesRecorded ? hdr->dwBytesRecorded : hdr->dwBufferLength;

    while (remaining >= 12u) {
        uint32_t delta = *(const uint32_t *)(const void *)(ptr + 0);
        uint32_t event = *(const uint32_t *)(const void *)(ptr + 8);
        uint8_t event_type = MEVT_EVENTTYPE_WINE(event);
        uint32_t parm = MEVT_EVENTPARM_WINE(event);
        uint32_t long_len = 0u;
        uint32_t chunk_len = 12u;

        ptr += 12u;
        remaining -= 12u;
        pending_delta += delta;

        if ((event & MEVT_F_CALLBACK_WINE) != 0u)
            continue;

        if ((event & MEVT_F_LONG_WINE) != 0u) {
            long_len = parm;
            chunk_len = (long_len + 3u) & ~3u;
            if (chunk_len > remaining)
                break;
            if (event_type == MEVT_LONGMSG_WINE &&
                !midi_queue_sysex(state, pending_delta, ptr, long_len))
                return 0;
            pending_delta = 0u;
            ptr += chunk_len;
            remaining -= chunk_len;
            continue;
        }

        if (event_type == MEVT_SHORTMSG_WINE) {
            if (!midi_queue_short_message(state, pending_delta, parm))
                return 0;
            pending_delta = 0u;
            continue;
        }
        if (event_type == MEVT_TEMPO_WINE) {
            if (!midi_queue_tempo(state, pending_delta, parm))
                return 0;
            pending_delta = 0u;
            continue;
        }
        if (event_type == MEVT_NOP_WINE || event_type == MEVT_COMMENT_WINE ||
            event_type == MEVT_VERSION_WINE)
            continue;
    }

    return 1;
}

KERNEL32_STUB
uint32_t timeGetTime(void)
{
    static uint32_t call_count = 0;
    struct timespec ts;
    uint32_t value;

    if (INLINE_SYSCALL_CLOCK_GETTIME(CLOCK_MONOTONIC, &ts) != 0)
        return 0;

    value = (uint32_t)((uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL);

    if (debug_level_at_least(1)) {
        uint32_t count = ++call_count;
        if ((count & (count - 1)) == 0 || (count % 100000u) == 0)
            DEBUG("winmm: timeGetTime count=%u value=%u", count, value);
    }

    return value;
}

KERNEL32_STUB
uint32_t joyGetNumDevs(void)
{
    int count = rb_joy_count ? rb_joy_count() : 0;
    return (uint32_t)(count < 0 ? 0 : count);
}

KERNEL32_STUB
uint32_t joyGetDevCapsA(uint32_t uJoyID, JOYCAPSA_WINE *caps, uint32_t cbjc)
{
    char name[32];
    int axes = 0;
    int buttons = 0;
    uint16_t min = 0;
    uint16_t max = 65535;

    if (!caps || cbjc < sizeof(*caps))
        return 165;
    memset(caps, 0, sizeof(*caps));
    if (!rb_joy_get_caps ||
        rb_joy_get_caps((int)uJoyID, name, sizeof(name), &axes, &buttons, &min, &max) != 0)
        return 167;
    memcpy(caps->szPname, name, sizeof(caps->szPname));
    caps->wXmin = min;
    caps->wXmax = max;
    caps->wYmin = min;
    caps->wYmax = max;
    caps->wZmin = min;
    caps->wZmax = max;
    caps->wNumButtons = (uint32_t)buttons;
    caps->wMaxAxes = (uint32_t)axes;
    caps->wNumAxes = (uint32_t)axes;
    caps->wMaxButtons = (uint32_t)buttons;
    return 0;
}

KERNEL32_STUB
uint32_t joyGetPosEx(uint32_t uJoyID, JOYINFOEX_WINE *info)
{
    uint16_t axes[6] = {0};
    uint8_t buttons[32] = {0};
    int i;

    if (!info || info->dwSize < sizeof(*info))
        return 165;
    if (!rb_joy_get_state ||
        rb_joy_get_state((int)uJoyID, axes, 6, buttons, 32) != 0)
        return 167;

    info->dwXpos = axes[0];
    info->dwYpos = axes[1];
    info->dwZpos = axes[2];
    info->dwRpos = axes[3];
    info->dwUpos = axes[4];
    info->dwVpos = axes[5];
    info->dwButtons = 0;
    for (i = 0; i < 32; i++) {
        if (buttons[i])
            info->dwButtons |= (1u << i);
    }
    info->dwButtonNumber = 0;
    info->dwPOV = 0xffffffffu;
    return 0;
}

KERNEL32_STUB
uint32_t midiOutGetNumDevs(void)
{
    char soundfont_path[256];
    int num_devs = midi_find_soundfont(soundfont_path, sizeof(soundfont_path)) ? 1 : 0;

    DEBUG_LEVEL(1, "winmm: midiOutGetNumDevs -> %d", num_devs);
    return (uint32_t)num_devs;
}

KERNEL32_STUB
uint32_t midiOutPrepareHeader(void *hmo, void *pmh, uint32_t cbmh)
{
    MIDIHDR_WINE *hdr = (MIDIHDR_WINE *)pmh;
    (void)hmo;
    (void)cbmh;

    if (hdr)
        hdr->dwFlags |= MHDR_PREPARED_WINE;
    return 0;
}

KERNEL32_STUB
uint32_t midiOutReset(void *hmo)
{
    (void)hmo;

    __atomic_add_fetch(&g_midi_stream.generation, 1u, __ATOMIC_ACQ_REL);
    __atomic_store_n(&g_midi_stream.playing, 0, __ATOMIC_RELEASE);
    (void)rb_call_on_host_stack(midi_queue_clear_host_call, &g_midi_stream);
    (void)rb_call_on_host_stack(midi_backend_apply_volume_host_call, &g_midi_stream);
    return 0;
}

KERNEL32_STUB
uint32_t midiOutSetVolume(void *hmo, uint32_t dwVolume)
{
    (void)hmo;

    __atomic_store_n(&g_midi_stream.volume, dwVolume, __ATOMIC_RELEASE);
    (void)rb_call_on_host_stack(midi_backend_apply_volume_host_call, &g_midi_stream);
    return 0;
}

KERNEL32_STUB
uint32_t midiOutUnprepareHeader(void *hmo, void *pmh, uint32_t cbmh)
{
    MIDIHDR_WINE *hdr = (MIDIHDR_WINE *)pmh;
    (void)hmo;
    (void)cbmh;

    if (hdr)
        hdr->dwFlags &= ~MHDR_PREPARED_WINE;
    return 0;
}

KERNEL32_STUB
uint32_t midiStreamOpen(void **phms, uint32_t *puDeviceID, uint32_t cMidi,
                        uintptr_t dwCallback, uintptr_t dwInstance, uint32_t fdwOpen)
{
    uint32_t handle;

    (void)puDeviceID;
    (void)cMidi;
    (void)dwCallback;
    (void)dwInstance;
    (void)fdwOpen;

    winmm_doom95_shutdown();
    midi_stream_reset_state(&g_midi_stream);

    handle = (uint32_t)wine_handle_alloc(HANDLE_TYPE_HMIDI_STREAM, NULL);
    if (phms)
        *phms = (void *)(uintptr_t)handle;
    g_midi_stream.stream_handle = handle;
    return 0;
}

KERNEL32_STUB
uint32_t midiStreamClose(void *hms)
{
    (void)rb_call_on_host_stack(midi_stream_stop_worker_host_call, &g_midi_stream);
    midi_stream_reset_state(&g_midi_stream);
    if ((uint32_t)(uintptr_t)hms != 0)
        wine_handle_free((uint32_t)(uintptr_t)hms);
    return 0;
}

KERNEL32_STUB
uint32_t midiStreamOut(void *hms, void *pmh, uint32_t cbmh)
{
    MIDIHDR_WINE *hdr = (MIDIHDR_WINE *)pmh;

    (void)hms;
    (void)cbmh;
    if (!hdr)
        return 0;

    hdr->dwFlags &= ~MHDR_DONE_WINE;
    (void)midi_stream_append_buffer(&g_midi_stream, hdr);
    hdr->dwFlags |= MHDR_DONE_WINE;
    return 0;
}

KERNEL32_STUB
uint32_t midiStreamPause(void *hms)
{
    (void)hms;
    __atomic_store_n(&g_midi_stream.playing, 0, __ATOMIC_RELEASE);
    return 0;
}

KERNEL32_STUB
uint32_t midiStreamProperty(void *hms, void *lppropdata, uint32_t dwProperty)
{
    (void)hms;

    if ((dwProperty & MIDIPROP_SET_WINE) != 0u && lppropdata != NULL) {
        if ((dwProperty & MIDIPROP_TIMEDIV_WINE) != 0u) {
            const MIDIPROPTIMEDIV_WINE *prop = (const MIDIPROPTIMEDIV_WINE *)lppropdata;
            if (prop->cbStruct >= sizeof(*prop) && prop->dwTimeDiv != 0u)
                __atomic_store_n(&g_midi_stream.time_div, prop->dwTimeDiv, __ATOMIC_RELEASE);
        }
        if ((dwProperty & MIDIPROP_TEMPO_WINE) != 0u) {
            const MIDIPROPTEMPO_WINE *prop = (const MIDIPROPTEMPO_WINE *)lppropdata;
            if (prop->cbStruct >= sizeof(*prop) && prop->dwTempo != 0u)
                __atomic_store_n(&g_midi_stream.tempo_us_per_qn, prop->dwTempo, __ATOMIC_RELEASE);
        }
    }

    return 0;
}

KERNEL32_STUB
uint32_t midiStreamRestart(void *hms)
{
    (void)hms;

    if (!g_midi_stream.worker_active &&
        (int)rb_call_on_host_stack(midi_stream_start_worker_host_call, &g_midi_stream) == 0) {
        DEBUG_LEVEL(1, "winmm: failed to start MIDI worker");
        return 0;
    }

    __atomic_store_n(&g_midi_stream.playing, 1, __ATOMIC_RELEASE);
    return 0;
}
