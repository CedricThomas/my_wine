#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kernel32_priv.h"
#include "winmm_doom95_priv.h"

static float midi_stream_gain_from_volume(uint32_t dwVolume)
{
    uint32_t left = dwVolume & 0xFFFFu;
    uint32_t right = (dwVolume >> 16) & 0xFFFFu;

    return (float)(left + right) / (float)(2u * 65535u);
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

int winmm_doom95_midi_backend_has_device(void)
{
    char soundfont_path[256];

    return midi_find_soundfont(soundfont_path, sizeof(soundfont_path));
}

int winmm_doom95_midi_backend_open(midi_fluidsynth_backend_t *backend,
                                   uint32_t volume)
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
        (void)fluid_settings_setnum(settings, "synth.gain",
                                    (double)midi_stream_gain_from_volume(volume));

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

void winmm_doom95_midi_backend_close(midi_fluidsynth_backend_t *backend)
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

void winmm_doom95_midi_backend_reset(midi_fluidsynth_backend_t *backend)
{
    if (!backend || !backend->ready || !backend->synth)
        return;
    (void)fluid_synth_system_reset(backend->synth);
}

void winmm_doom95_midi_backend_apply_volume(midi_stream_state_t *state)
{
    float gain;

    if (!state || !state->backend.ready || !state->backend.synth)
        return;

    gain = midi_stream_gain_from_volume(
        __atomic_load_n(&state->volume, __ATOMIC_ACQUIRE));
    fluid_synth_set_gain(state->backend.synth, gain);
}

void winmm_doom95_midi_backend_send_short(midi_fluidsynth_backend_t *backend,
                                          uint32_t packed_msg)
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

void winmm_doom95_midi_backend_send_sysex(midi_fluidsynth_backend_t *backend,
                                          const uint8_t *data, uint32_t len)
{
    int handled = 0;

    if (!backend || !backend->ready || !backend->synth || !data || len == 0u)
        return;

    (void)fluid_synth_sysex(backend->synth, (const char *)data, (int)len,
                            NULL, NULL, &handled, 0);
}
