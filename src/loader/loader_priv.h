/*
 * loader_priv.h — Internal loader module aggregator
 *
 * Aggregates all per-module loader headers. Each module's declarations
 * are in its own header file; this header simply includes them all
 * for convenience so any loader .c file can include one header.
 */

#ifndef MY_WINE_LOADER_PRIV_H
#define MY_WINE_LOADER_PRIV_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "include/pe_parser.h"

/* ── Per-module headers ──────────────────────────────────────── */

#include "image_mapper.h"       /* g_image_base, g_host_gs_base, get/set_pe_path, map_image[_at] */
#include "import_table.h"       /* import_entry_t, import_flat, import_table[], strategies */
#include "ordinal_table.h"      /* ordinal_lookup */
#include "import_resolve.h"     /* resolve_imports, find_text_thunk, find_dll_path */
#include "import_init.h"        /* init_msvcrt_imports */
#include "relocations.h"        /* apply_relocations */
#include "teb_peb.h"            /* g_stack_base, g_stack_size, setup_teb_peb, setup_stack */
#include "crash_handlers.h"     /* setup_signal_handlers, seh_crash_handler */
#include "guest_setup.h"        /* run_guest_entry, setup_guest_and_run, cleanup_guest */
#include "gs_base.h"            /* set_gs_base, get_gs_base */
#include "module_list.h"        /* loaded_module_t, module registry */
#include "export_table.h"       /* parse_export_table, lookup_export, reset_export_cache */
#include "peb_ldr.h"            /* PEB_LDR_DATA, ldr_add/remove_module, g_peb_ldr */
#include "dll_path.h"            /* find_dll_path */
#include "dll_loader.h"          /* load_dll */

/* ── Unix stack setup (from dispatcher_entry.c) ── */
int setup_unix_stack(void);
void cleanup_unix_stack(void);

#endif /* MY_WINE_LOADER_PRIV_H */
