/*
 * rb_driver_policy.c
 *
 * Owns SDL backend driver policy: requested-driver capture, startup fallback
 * sequencing, and active-driver snapshotting for init diagnostics.
 */

#include "rb_sdl2_priv.h"
#include <stdlib.h>
#include <string.h>

static const char *rb_getenv_or_auto(const char *name)
{
    const char *value = getenv(name);

    return (value && value[0] != '\0') ? value : "auto";
}

static void rb_copy_driver_name(char *dst, size_t dst_size, const char *src)
{
    size_t i;

    if (!dst || dst_size == 0)
        return;

    if (!src) {
        dst[0] = '\0';
        return;
    }

    for (i = 0; i + 1 < dst_size && src[i] != '\0'; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

void rb_backend_capture_requested_drivers(rb_backend_driver_info *info)
{
    if (!info)
        return;

    rb_copy_driver_name(info->requested_video_driver,
                        sizeof(info->requested_video_driver),
                        rb_getenv_or_auto("SDL_VIDEODRIVER"));
    rb_copy_driver_name(info->requested_audio_driver,
                        sizeof(info->requested_audio_driver),
                        rb_getenv_or_auto("SDL_AUDIODRIVER"));
}

static int rb_backend_init_video(uint32_t flags,
                                 void (*install_x11_error_handler)(void))
{
    const char *requested_video_driver = getenv("SDL_VIDEODRIVER");
    int try_x11_fallback = 0;
    int had_requested_video_driver = requested_video_driver != NULL;

    /*
     * Desktop launchers can leave startup-notification state in the
     * environment. SDL windows created under that state may keep a busy
     * cursor over the window even after the app is responsive.
     */
    unsetenv("DESKTOP_STARTUP_ID");
    unsetenv("XDG_ACTIVATION_TOKEN");

    if (requested_video_driver == NULL || strcmp(requested_video_driver, "wayland") == 0)
        try_x11_fallback = 1;

    install_x11_error_handler();
    if (SDL_Init(flags) == 0) {
        install_x11_error_handler();
        return 0;
    }

    install_x11_error_handler();
    if (getenv("DISPLAY") && try_x11_fallback) {
        SDL_Quit();
        setenv("SDL_VIDEODRIVER", "x11", 1);
        if (SDL_Init(flags) == 0) {
            install_x11_error_handler();
            return 0;
        }
        if (had_requested_video_driver)
            setenv("SDL_VIDEODRIVER", requested_video_driver, 1);
        else
            unsetenv("SDL_VIDEODRIVER");
        install_x11_error_handler();
    }

    return -1;
}

static int rb_backend_init_audio(void)
{
    const char *requested_audio_driver = getenv("SDL_AUDIODRIVER");
    static const char *preferred_drivers[] = {
        "pipewire",
        "pulseaudio",
        "alsa",
        "jack",
        "sndio",
        "dsp",
        "dummy",
        "disk"
    };
    int i;
    int num_drivers;

    if (requested_audio_driver && requested_audio_driver[0] != '\0')
        return SDL_AudioInit(requested_audio_driver);

    if (SDL_AudioInit(NULL) == 0)
        return 0;

    num_drivers = SDL_GetNumAudioDrivers();
    for (i = 0; i < (int)(sizeof(preferred_drivers) / sizeof(preferred_drivers[0])); i++) {
        int j;

        for (j = 0; j < num_drivers; j++) {
            const char *driver = SDL_GetAudioDriver(j);

            if (!driver || strcmp(driver, preferred_drivers[i]) != 0)
                continue;
            if (SDL_AudioInit(driver) == 0)
                return 0;
            break;
        }
    }

    for (i = 0; i < num_drivers; i++) {
        const char *driver = SDL_GetAudioDriver(i);

        if (!driver || driver[0] == '\0')
            continue;
        if (strcmp(driver, "disk") == 0 || strcmp(driver, "dummy") == 0)
            continue;
        if (SDL_AudioInit(driver) == 0)
            return 0;
    }

    for (i = 0; i < num_drivers; i++) {
        const char *driver = SDL_GetAudioDriver(i);

        if (!driver || driver[0] == '\0')
            continue;
        if ((strcmp(driver, "disk") != 0) && (strcmp(driver, "dummy") != 0))
            continue;
        if (SDL_AudioInit(driver) == 0)
            return 0;
    }

    return -1;
}

uintptr_t rb_backend_init_sdl_call(void *arg)
{
    rb_sdl_init_args *a = arg;
    uint32_t base_flags;
    int want_audio;

    if (!a || !a->install_x11_error_handler)
        return (uintptr_t)-1;

    base_flags = a->flags & ~(uint32_t)SDL_INIT_AUDIO;
    want_audio = (a->flags & SDL_INIT_AUDIO) != 0;

    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

    if (rb_backend_init_video(base_flags, a->install_x11_error_handler) < 0)
        return (uintptr_t)-1;

    if (want_audio && rb_backend_init_audio() < 0) {
        SDL_Quit();
        return (uintptr_t)-1;
    }

    return 0;
}

uintptr_t rb_backend_capture_active_drivers_call(void *arg)
{
    rb_backend_driver_info *info = arg;

    if (!info)
        return 0;

    rb_copy_driver_name(info->active_video_driver,
                        sizeof(info->active_video_driver),
                        SDL_GetCurrentVideoDriver());
    rb_copy_driver_name(info->active_audio_driver,
                        sizeof(info->active_audio_driver),
                        SDL_GetCurrentAudioDriver());
    return 0;
}
