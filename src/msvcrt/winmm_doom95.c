#include <stdint.h>
#include <string.h>

#include "kernel32_priv.h"
#include "include/handle_manager.h"

extern int rb_joy_count(void) __attribute__((weak));
extern int rb_joy_get_caps(int idx, char *name, int name_len,
                           int *n_axes, int *n_buttons,
                           uint16_t *min, uint16_t *max) __attribute__((weak));
extern int rb_joy_get_state(int idx, uint16_t *axes, int n_axes,
                            uint8_t *buttons, int n_buttons) __attribute__((weak));
extern void rb_event_pump_host(void) __attribute__((weak));

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

KERNEL32_STUB
uint32_t timeGetTime(void)
{
    static uint32_t call_count = 0;
    static uint32_t pump_count = 0;
    static uint32_t last_value = 0;
    static uint32_t same_tick_polls = 0;
    struct timespec ts;
    uint32_t value;

    if (INLINE_SYSCALL_CLOCK_GETTIME(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    value = (uint32_t)((uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL);

    if (value == last_value) {
        same_tick_polls++;
        if (same_tick_polls >= 2048u && (same_tick_polls & 255u) == 0u) {
            ts.tv_sec = 0;
            ts.tv_nsec = 1000000L;
            (void)INLINE_SYSCALL_NANOSLEEP(&ts, NULL);
        }
    } else {
        last_value = value;
        same_tick_polls = 0;
    }

    if (debug_level_at_least(1)) {
        uint32_t count = ++call_count;
        if ((count & (count - 1)) == 0 || (count % 100000u) == 0)
            DEBUG("winmm: timeGetTime count=%u value=%u", count, value);
    }

    pump_count++;
    if (rb_event_pump_host && ((pump_count & 0x7ffu) == 0))
        rb_event_pump_host();

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
    return 1;
}

KERNEL32_STUB
uint32_t midiOutPrepareHeader(void *hmo, void *pmh, uint32_t cbmh)
{
    (void)hmo;
    (void)pmh;
    (void)cbmh;
    return 0;
}

KERNEL32_STUB
uint32_t midiOutReset(void *hmo)
{
    (void)hmo;
    return 0;
}

KERNEL32_STUB
uint32_t midiOutSetVolume(void *hmo, uint32_t dwVolume)
{
    (void)hmo;
    (void)dwVolume;
    return 0;
}

KERNEL32_STUB
uint32_t midiOutUnprepareHeader(void *hmo, void *pmh, uint32_t cbmh)
{
    (void)hmo;
    (void)pmh;
    (void)cbmh;
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

    handle = (uint32_t)wine_handle_alloc(HANDLE_TYPE_HMIDI_STREAM, NULL);
    if (phms)
        *phms = (void *)(uintptr_t)handle;
    return 0;
}

KERNEL32_STUB
uint32_t midiStreamClose(void *hms)
{
    if ((uint32_t)(uintptr_t)hms != 0)
        wine_handle_free((uint32_t)(uintptr_t)hms);
    return 0;
}

KERNEL32_STUB
uint32_t midiStreamOut(void *hms, void *pmh, uint32_t cbmh)
{
    (void)hms;
    (void)pmh;
    (void)cbmh;
    return 0;
}

KERNEL32_STUB
uint32_t midiStreamPause(void *hms)
{
    (void)hms;
    return 0;
}

KERNEL32_STUB
uint32_t midiStreamProperty(void *hms, void *lppropdata, uint32_t dwProperty)
{
    (void)hms;
    (void)lppropdata;
    (void)dwProperty;
    return 0;
}

KERNEL32_STUB
uint32_t midiStreamRestart(void *hms)
{
    (void)hms;
    return 0;
}
