/*
 * import_registry.h — Declarative import table entry macros
 *
 * Provides DECLARE_IMPORT macros that generate initialiser elements
 * for the import_table[] array in import_table.c.  The declarative
 * style makes the table self-documenting and easier to extend.
 *
 * Usage:
 *
 *     import_entry_t import_table[] = {
 *         // ntdll functions (via syscall thunks)
 *         DECLARE_IMPORT("ntdll.dll", "NtClose", handler_NtClose)
 *         DECLARE_IMPORT("ntdll.dll", "NtReadFile", handler_NtReadFile)
 *         ...
 *         // kernel32 functions
 *         DECLARE_IMPORT("kernel32.dll", "GetStdHandle", GetStdHandle)
 *         ...
 *         // msvcrt data symbols (address-of)
 *         DECLARE_IMPORT_ADDR("msvcrt.dll", "_fmode", _fmode)
 *         ...
 *         { NULL, NULL, NULL }  // sentinel
 *     };
 *
 * The macros expand to compound literals compatible with the
 * import_entry_t struct defined in loader_priv.h.
 */

#ifndef IMPORT_REGISTRY_H
#define IMPORT_REGISTRY_H

/*
 * DECLARE_IMPORT(dll, name, addr) — function pointer entry.
 *   dll   — DLL name string literal
 *   name  — function name string literal
 *   addr  — function pointer (no & needed; function names decay)
 */
#define DECLARE_IMPORT(dll, name, addr) \
    { (dll), (name), (void *)(addr) }

/*
 * DECLARE_IMPORT_ADDR(dll, name, addr) — address-of entry (for data symbols).
 *   dll   — DLL name string literal
 *   name  — symbol name string literal
 *   addr  — expression whose address to store (e.g. _acmdln)
 */
#define DECLARE_IMPORT_ADDR(dll, name, addr) \
    { (dll), (name), (void *)&(addr) }

#endif /* IMPORT_REGISTRY_H */
