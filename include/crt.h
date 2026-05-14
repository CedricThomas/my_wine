/*
 * crt.h — CRT module API
 *
 * Defines an opaque module interface for pluggable CRT implementations
 * (MinGW, Watcom, etc.) and the shared structures/types used across all
 * CRT backends.
 */

#ifndef MY_WINE_CRT_H
#define MY_WINE_CRT_H

#include <stdint.h>
#include <stddef.h>
#include "pe.h"
#include "common.h"

/* ── CRT type enumeration ──────────────────────────────────────── */

typedef enum {
    CRT_TYPE_MINGW,
    CRT_TYPE_WATCOM,
    CRT_TYPE_UNKNOWN
} crt_type_t;

/* ── CRT context (moved from include/msvcrt.h) ────────────────── */

typedef struct {
    uint64_t image_base;
    uint64_t bss_vaddr;
    uint32_t argc_bss_offset;
    uint32_t argv_bss_offset;
    uint32_t envp_bss_offset;
} crt_context_t;

/* ── FILE structures for g_crt.iob ────────────────────────────── */

#define WINE_FILE_SIZE 48
#define WINE_IOEOF  0x8000
#define WINE_IOWRT  0x0002
#define WINE_IONBF  0x4000
#define WINE_IOREAD 0x0001
#define WINE_IOFBF  0x0200

#pragma pack(push, 1)
typedef struct {
    int            _fd;
    unsigned char *_ptr;
    int            _cnt;
    unsigned char *_base;
    uintptr_t      _flag;
    /* Padding to ensure wine_FILE is always WINE_FILE_SIZE bytes regardless
     * of pointer size. On 64-bit: 4+8+4+8+8=32, pad=16 → 48. On 32-bit:
     * 4+4+4+4+4=20, pad=28 → 48. */
#if defined(__i386__)
    unsigned char  _pad[28];
#else
    unsigned char  _pad[16];
#endif
} wine_FILE;
#pragma pack(pop)

_Static_assert(sizeof(wine_FILE) == WINE_FILE_SIZE, "wine_FILE size mismatch");

typedef union {
    wine_FILE f[3];
    char      bytes[48 * 3];
} iob_union;

/* ── Consolidated CRT global state ─────────────────────────────── */
/*
 * wine_crt_state_t — All CRT global state in one struct.
 * Defined as g_crt in src/msvcrt/crt_globals.c.
 */

typedef struct {
    /* App / mode flags */
    int app_type;
    int commode;
    int fmode;

    /* Environment / argv pointers */
    char **environ;
    char **initenv;
    char **guest_argv;
    char **guest_envp;

    /* Command line storage */
    char cmdline_storage[PAGE_SIZE];
    char *acmdln;
    char *p_acmdln;

    /* Startup state */
    uint64_t native_startup_lock;
    int native_startup_state;
    int dowildcard;
    int newmode;
    crt_context_t crt_ctx;

    /* Two-level refptr stubs (zero-valued for safe CRT startup) */
    uint64_t dyn_tls_callback_stub;
    uint64_t mingw_excpt_handler_stub;
    uint64_t xc_a_stub;
    uint64_t xc_z_stub;

    /* Constructor/destructor list stubs */
    uint32_t ctor_list_stub[1];
    uint32_t dtor_list_stub[1];

    /* Constructor range markers */
    uint64_t xi_a_stub;
    uint64_t xi_z_stub;

    /* Initenv stub */
    void **imp_initenv_stub;

    /* FILE structures */
    iob_union iob;
} wine_crt_state_t;

extern wine_crt_state_t g_crt;

/* ── Refptr mapping (moved from src/msvcrt/msvcrt_priv.h) ─────── */

typedef struct {
    const char *name;
    void       *target;
} refptr_mapping_t;

/* ── Opaque CRT module ─────────────────────────────────────────── */

typedef struct crt_module crt_module_t;

/* ── Accessor declarations ─────────────────────────────────────── */

/* Detect which CRT type a PE image was linked against */
crt_type_t crt_detect_type(const char *file_path, IMAGE_NT_HEADERS *nt);

/* Lookup a module by CRT type */
const crt_module_t *crt_get_module(crt_type_t type);

/* Get the null-terminated list of entry-point symbol names for the module */
const char *const *crt_entry_symbols(const crt_module_t *mod);

/* Get the refptr mapping table for the module (NULL-terminated) */
const refptr_mapping_t *crt_get_refptr_mappings(const crt_module_t *mod);

/* Get the number of refptr mappings in the module's table */
int crt_refptr_mapping_count(const crt_module_t *mod);

/* Patch the PE's refptrs using the module's mapping table */
void crt_patch_refptrs(const crt_module_t *mod, const char *file_path,
                       void *image_base, IMAGE_NT_HEADERS *nt,
                       IMAGE_SECTION_HEADER *sections);

/* Discover CRT-specific BSS offsets by scanning the PE */
void crt_discover_offsets(const crt_module_t *mod, const char *file_path,
                          IMAGE_NT_HEADERS *nt, IMAGE_SECTION_HEADER *sections,
                          crt_context_t *ctx);

/* Seed the BSS section with initial values (argc=0, argv=NULL, etc.) */
void crt_seed_bss(const crt_module_t *mod, void *image_base,
                  IMAGE_NT_HEADERS *nt, IMAGE_SECTION_HEADER *sections);

/* Get the default BSS offset for the init (argc) slot */
uint32_t crt_bss_init_offset(const crt_module_t *mod);

/* Get the default BSS offset for the argv slot */
uint32_t crt_bss_argv_offset(const crt_module_t *mod);

/* Get the default BSS offset for the initenv slot */
uint32_t crt_bss_initenv_offset(const crt_module_t *mod);

/* Set the globally active CRT module (call once after detection) */
void crt_set_active(const crt_module_t *mod);

/* Get the globally active CRT module without re-detection */
const crt_module_t *crt_get_active(void);

/* Check whether the module provides a seed_bss vtable entry */
int crt_has_seed_bss(const crt_module_t *mod);

#endif /* MY_WINE_CRT_H */
