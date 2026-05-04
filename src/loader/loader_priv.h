/*
 * loader_priv.h — Internal loader module declarations
 *
 * Shared globals, structs, and function declarations for the loader/
 * sub-modules. Not meant to be included outside the loader package.
 */

#ifndef MY_WINE_LOADER_PRIV_H
#define MY_WINE_LOADER_PRIV_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/ucontext.h>
#include "include/pe.h"
#include "include/pe_parser.h"

/* ── Global state shared across loader modules ─────────────── */

/* Set by image_mapper.c, read by teb_peb.c and import_resolve.c */
extern void *g_image_base;

/* Set by teb_peb.c (setup_stack), read by main.c */
extern void *g_stack_base;

/* Set by teb_peb.c (setup_stack), read by entry.c for cleanup */
extern size_t g_stack_size;

/* ── Import resolver types ─────────────────────────────────── */

typedef struct {
    const char *dll_name;
    const char *name;
    void *address;
} import_entry_t;

/* Flat import entry used in pass 2 thunk patching */
struct import_flat {
    uint64_t   ilt_value;      /* OriginalFirstThunk[i].AddressOfData */
    uint64_t   resolved_addr;  /* FirstThunk[i].AddressOfData (from pass 1) */
    const char *dll_name;
    const char *func_name;
};

/* Name→address table for NT, kernel32 and msvcrt functions
 * Defined in import_table.c */
extern import_entry_t import_table[];
extern size_t import_table_count;

/* ── import_table.c ────────────────────────────────────────── */

void set_import(const char *name, void *address);
void init_import_table(void);
int import_cmp_by_name(const void *key, const void *elem);
int build_flat_import_array(void *base, IMAGE_NT_HEADERS64 *nt,
                            struct import_flat flat[]);
bool strategy_resolved_overlap(uint64_t current_val,
                               struct import_flat *flat, int num_flat);
bool strategy_ilt_value_match(uint64_t *target_ptr, uint64_t current_val,
                              uint64_t target,
                              struct import_flat *flat, int num_flat);
bool strategy_ilt_offset_match(uint64_t *target_ptr, uint64_t target,
                               uint64_t current_val,
                               uint64_t import_dir_va, uint64_t import_dir_end,
                               struct import_flat *flat, int num_flat);
bool strategy_positional(uint64_t *target_ptr, uint64_t target,
                         int thunk_idx,
                         struct import_flat *flat, int num_flat);

/* ── import_resolve.c ─────────────────────────────────────── */

int resolve_imports(void *base, IMAGE_NT_HEADERS64 *nt);
void *find_text_thunk(void *image_base, IMAGE_NT_HEADERS64 *nt,
                       IMAGE_SECTION_HEADER *sections,
                       void *target_addr);

/* ── import_init.c ─────────────────────────────────────────── */

void init_msvcrt_imports(void);

/* ── image_mapper.c ────────────────────────────────────────── */

/**
 * Map a PE file at the preferred image base.
 *
 * Opens the file, maps read-only, parses headers, maps the image
 * memory, copies sections, sets per-section protections, cleans up.
 *
 * @param  path  path to the PE file
 * @param  out_dos   (optional) receives parsed DOS header
 * @param  out_nt    (optional) receives parsed NT headers
 * @param  out_nt_size (optional) receives size of parsed NT headers struct
 * @return  image base address (virtual), or NULL on failure
 */
void *map_image(const char *path,
                IMAGE_DOS_HEADER *out_dos,
                IMAGE_NT_HEADERS64 *out_nt,
                size_t *out_nt_size);

/* ── teb_peb.c ─────────────────────────────────────────────── */

void *setup_teb_peb(void);
void *setup_stack(IMAGE_OPTIONAL_HEADER64 *opt);

/* ── crash_handlers.c ─────────────────────────────────────── */

void setup_signal_handlers(void);
__attribute__((ms_abi)) void seh_crash_handler(void *, void *, void *, void *);

/* ── entry.c / child_setup.c ────────────────────────────────── */

int jump_to_entry(uint64_t entry_abs, void *stack_top, void *stack_base,
                  void *teb, char **guest_argv, char **guest_envp);

void setup_child_and_run(uint64_t entry_abs, void *stack_top, void *teb,
                         char **guest_argv, char **guest_envp);
void cleanup_guest(void *teb, void *stack_base);

/* ── gs_base.c ─────────────────────────────────────────────── */

int set_gs_base(void *addr);
void *get_gs_base(void);

#endif /* MY_WINE_LOADER_PRIV_H */
