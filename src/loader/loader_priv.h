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
#include <sys/ucontext.h>
#include "include/pe_parser.h"

/* ── Global state shared across loader modules ─────────────── */

/* Set by image_mapper.c, read by teb_peb.c and import_resolver.c */
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

/* Name→address table for NT, kernel32 and msvcrt functions
 * Defined in import_table.c */
extern import_entry_t import_table[];
extern size_t import_table_count;

/* ── import_table.c ────────────────────────────────────────── */

void set_import(const char *name, void *address);
void init_import_table(void);
int import_cmp_by_name(const void *key, const void *elem);

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

/* ── entry.c ───────────────────────────────────────────────── */

int jump_to_entry(uint64_t entry_abs, void *stack_top, void *stack_base,
                  void *teb, char **guest_argv, char **guest_envp);

/* ── gs_base.c ─────────────────────────────────────────────── */

int set_gs_base(void *addr);
void *get_gs_base(void);

#endif /* MY_WINE_LOADER_PRIV_H */
