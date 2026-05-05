/*
 * debug.c — Shared debug infrastructure.
 *
 * Provides the debug_check_fn function pointer that all translation units
 * share. Defaults to debug_disabled (returns 0). common.c overrides it
 * to check g_debug_enabled at startup.
 *
 * Must be linked into ALL binaries (my_wine + tests) to prevent crashes
 * from unresolved extern symbols.
 */

#include "include/debug.h"

/* Default handler: debug disabled */
static int debug_disabled(void) { return 0; }

/* Global function pointer — shared by all TUs.
 * Weak definition: overridden by common.c in my_wine.
 * In test binaries (no common.o), this is the only definition. */
__attribute__((weak)) int (*debug_check_fn)(void) = &debug_disabled;
