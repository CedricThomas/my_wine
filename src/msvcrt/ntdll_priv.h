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
#include "include/handle_manager.h"
#include <stddef.h>

/* ── Handle Table Helper Functions ─────────────────────────────── */

int handle_to_fd(uint64_t handle);
uint64_t fd_to_handle(int fd);
void free_handle(uint64_t handle);

/* ── Type Definitions ──────────────────────────────────────────── */

#define MAX_SECTIONS 64

typedef struct {
    void     *base;
    size_t    size;
    int       fd;
    uint64_t  max_size;
} wine_section_t;

typedef struct {
    void  *base;
    size_t size;
} wine_view_t;

#define MAX_EVENTS 64

typedef struct {
    int handle;
    int signaled;
    int event_type; // 0 = Notification, 1 = Synchronization
} wine_event_t;

#define MAX_MUTEXES 64

typedef struct {
    int handle;
    int locked;  /* track if currently locked */
} wine_mutex_t;

#define MAX_SEMAPHORES 64

typedef struct {
    int handle;
    int count;
    int max_count;
} wine_semaphore_t;

#define MAX_THREADS 32

typedef struct {
    int tid;
    int handle;
    int suspended;
} wine_thread_t;

/* ── Kernel Object Store ───────────────────────────────────────── */

typedef struct {
    // Section / view tables
    wine_section_t sections[MAX_SECTIONS];
    int            section_count;
    wine_view_t    views[MAX_SECTIONS];
    int            view_count;

    // Event table
    wine_event_t   events[MAX_EVENTS];
    int            event_count;

    // Mutex table
    wine_mutex_t   mutexes[MAX_MUTEXES];
    int            mutex_count;

    // Semaphore table
    wine_semaphore_t semaphores[MAX_SEMAPHORES];
    int            semaphore_count;

    // Thread table
    wine_thread_t  threads[MAX_THREADS];
    int            thread_count;

    // Spinlock protecting all tables
    wine_spinlock_t lock;
} wine_kernel_objects_t;

extern wine_kernel_objects_t g_ko;

/* ── Inline Accessors ──────────────────────────────────────────── */

// Sections
static inline wine_section_t *ko_section(int i)       { return &g_ko.sections[i]; }
static inline int             ko_section_count(void)  { return g_ko.section_count; }
static inline void            ko_set_section_count(int n) { g_ko.section_count = n; }

// Views
static inline wine_view_t *ko_view(int i)             { return &g_ko.views[i]; }
static inline int          ko_view_count(void)         { return g_ko.view_count; }
static inline void         ko_set_view_count(int n)    { g_ko.view_count = n; }

// Events
static inline wine_event_t *ko_event(int i)            { return &g_ko.events[i]; }
static inline int           ko_event_count(void)       { return g_ko.event_count; }
static inline void          ko_set_event_count(int n)  { g_ko.event_count = n; }

// Mutexes
static inline wine_mutex_t *ko_mutex(int i)            { return &g_ko.mutexes[i]; }
static inline int           ko_mutex_count(void)       { return g_ko.mutex_count; }
static inline void          ko_set_mutex_count(int n)  { g_ko.mutex_count = n; }

// Semaphores
static inline wine_semaphore_t *ko_semaphore(int i)    { return &g_ko.semaphores[i]; }
static inline int               ko_semaphore_count(void) { return g_ko.semaphore_count; }
static inline void              ko_set_semaphore_count(int n) { g_ko.semaphore_count = n; }

// Threads
static inline wine_thread_t *ko_thread(int i)          { return &g_ko.threads[i]; }
static inline int            ko_thread_count(void)     { return g_ko.thread_count; }
static inline void           ko_set_thread_count(int n) { g_ko.thread_count = n; }

/* ── Helpers ──────────────────────────────────────────────────── */

int map_protect(uint64_t protect);
int find_view(void *base);

#endif /* MY_WINE_NTDLL_PRIV_H */
