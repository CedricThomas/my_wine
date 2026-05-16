#ifndef MY_WINE_HANDLE_MANAGER_H
#define MY_WINE_HANDLE_MANAGER_H

#include <stdint.h>

/* ── Spinlock — safe after FS→TEB switch (no TLS/glibc dependency) ── */

typedef volatile int wine_spinlock_t;

static inline void wine_spinlock_lock(volatile int *lock)
{
    while (__atomic_test_and_set(lock, __ATOMIC_ACQUIRE))
        ;
}

static inline void wine_spinlock_unlock(volatile int *lock)
{
    __atomic_clear(lock, __ATOMIC_RELEASE);
}

#define HANDLE_TABLE_SIZE 512

// Handle type tags
#define HANDLE_TYPE_FILE      0x01
#define HANDLE_TYPE_EVENT     0x02
#define HANDLE_TYPE_MUTEX     0x03
#define HANDLE_TYPE_SEMAPHORE 0x05
#define HANDLE_TYPE_SECTION   0x06
#define HANDLE_TYPE_THREAD    0x04
#define HANDLE_TYPE_HMODULE   0x10
#define HANDLE_TYPE_DC        0x20
#define HANDLE_TYPE_BITMAP    0x30
#define HANDLE_TYPE_PALETTE   0x31
#define HANDLE_TYPE_HFONT     0x32
#define HANDLE_TYPE_HCURSOR   0x33
#define HANDLE_TYPE_DD        0x40
#define HANDLE_TYPE_DD_SURFACE 0x41
#define HANDLE_TYPE_DS        0x50
#define HANDLE_TYPE_DS_BUFFER 0x51

// DOOM95-specific handle types
#define HANDLE_TYPE_HWIN         0x60
#define HANDLE_TYPE_DD_PALETTE   0x61
#define HANDLE_TYPE_DD_CLIPPER   0x62
#define HANDLE_TYPE_RB_WINDOW    0x63
#define HANDLE_TYPE_RB_SURFACE   0x64
#define HANDLE_TYPE_RB_PALETTE   0x65
#define HANDLE_TYPE_RB_CURSOR    0x66
#define HANDLE_TYPE_HMIDI_STREAM 0x70
#define HANDLE_TYPE_HMIDI_OUT    0x71
#define HANDLE_TYPE_HRSRC        0x72
#define HANDLE_TYPE_HGLOBAL      0x73
#define HANDLE_TYPE_HKEY         0x80
#define HANDLE_TYPE_HHOOK        0x90

// Pseudo-handle constants
// Note: These must use unsigned literals so they zero-extend to uint64_t
// on 32-bit (matching how the dispatcher zero-extends the 32-bit stack value).
// 0x7FFFFFFF as int = -1, sign-extends to 0xFFFFFFFFFFFFFF... as uint64_t.
// 0x7FFFFFFF as unsigned = 0x000000007FFFFFFF when promoted to uint64_t.
#define STDIN_HANDLE   ((uint64_t)0x7FFFFFFF)
#define STDOUT_HANDLE  ((uint64_t)0x7FFFFFFE)
#define STDERR_HANDLE  ((uint64_t)0x7FFFFFFD)

// Handle entry
typedef struct {
    uint32_t id;
    uint8_t type;
    uint8_t ref_count;
    void *object;
} wine_handle_t;

// API
void wine_handle_manager_init(void);
uint64_t wine_handle_alloc(uint8_t type, void *object);
void *wine_handle_get(uint32_t handle);
uint8_t wine_handle_get_type(uint32_t handle);
void wine_handle_add_ref(uint32_t handle);
void wine_handle_free(uint32_t handle);

#endif // MY_WINE_HANDLE_MANAGER_H
