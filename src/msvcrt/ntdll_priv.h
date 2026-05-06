/*
 * ntdll_priv.h — Private declarations shared across ntdll split files
 *
 * All files in src/msvcrt/ntdll_*.c include this header. It re-exports the
 * public ntdll.h and declares the internal globals, types, and helpers.
 */

#ifndef MY_WINE_NTDLL_PRIV_H
#define MY_WINE_NTDLL_PRIV_H

#include "include/ntdll.h"
#include "include/pe.h"
#include <stddef.h>
#include <pthread.h>

/* ── Handle Table ──────────────────────────────────────────────── */

#define HANDLE_TABLE_SIZE    256
#define STDIN_HANDLE         0x7FFFFFFF
#define STDOUT_HANDLE        0x7FFFFFFE
#define STDERR_HANDLE        0x7FFFFFFD

typedef struct {
    int        fd;
    uint8_t    used;
} handle_entry_t;

/*
 * SINGLE-THREAD ONLY: these globals are not safe for concurrent access.
 * No mutex, spinlock, or atomic protection is applied.
 * If multi-threaded guest code is supported in the future, each table
 * must be protected (e.g., pthread_mutex_t) and all accessors updated.
 */
extern handle_entry_t handle_table[HANDLE_TABLE_SIZE];

void init_handle_table(void);
int handle_to_fd(uint64_t handle);
uint64_t fd_to_handle(int fd);
void free_handle(uint64_t handle);

/* ── Section Tracking ──────────────────────────────────────────── */

#define MAX_SECTIONS 64

typedef struct {
    void     *base;
    size_t    size;
    int       fd;
    uint64_t  max_size;
} wine_section_t;

/*
 * SINGLE-THREAD ONLY: these globals are not safe for concurrent access.
 * No mutex, spinlock, or atomic protection is applied.
 * If multi-threaded guest code is supported in the future, each table
 * must be protected (e.g., pthread_mutex_t) and all accessors updated.
 */
extern wine_section_t sections[MAX_SECTIONS];
extern int section_count;

/* ── View Tracking ─────────────────────────────────────────────── */

typedef struct {
    void  *base;
    size_t size;
} wine_view_t;

extern wine_view_t views[MAX_SECTIONS];
extern int view_count;

/* ── Event Tracking ────────────────────────────────────────────── */

#define MAX_EVENTS 64

typedef struct {
    int handle;
    int signaled;
    int event_type; // 0 = Notification, 1 = Synchronization
    pthread_cond_t cond;
} wine_event_t;

/*
 * SINGLE-THREAD ONLY: these globals are not safe for concurrent access.
 * No mutex, spinlock, or atomic protection is applied.
 * If multi-threaded guest code is supported in the future, each table
 * must be protected (e.g., pthread_mutex_t) and all accessors updated.
 */
extern wine_event_t events[MAX_EVENTS];
extern int event_count;

/* ── Mutex Tracking ────────────────────────────────────────────── */

#define MAX_MUTEXES 64

typedef struct {
    int            handle;
    pthread_mutex_t mutex;
    int            locked;  /* track if currently locked */
} wine_mutex_t;

/*
 * SINGLE-THREAD ONLY: these globals are not safe for concurrent access.
 * No mutex, spinlock, or atomic protection is applied.
 * If multi-threaded guest code is supported in the future, each table
 * must be protected (e.g., pthread_mutex_t) and all accessors updated.
 */
extern wine_mutex_t mutexes[MAX_MUTEXES];
extern int mutex_count;

/* ── Synchronization Globals ────────────────────────────────────── */

extern pthread_mutex_t events_global_mutex;

/* ── Thread Tracking ───────────────────────────────────────────── */

#define MAX_THREADS 32

typedef struct {
    int tid;     /* clone() returns PID of child */
    int suspended;
} wine_thread_t;

/*
 * SINGLE-THREAD ONLY: these globals are not safe for concurrent access.
 * No mutex, spinlock, or atomic protection is applied.
 * If multi-threaded guest code is supported in the future, each table
 * must be protected (e.g., pthread_mutex_t) and all accessors updated.
 */
extern wine_thread_t threads[MAX_THREADS];
extern int thread_count;

/* ── Helpers ───────────────────────────────────────────────────── */

int map_protect(uint64_t protect);
int find_view(void *base);

#endif /* MY_WINE_NTDLL_PRIV_H */
