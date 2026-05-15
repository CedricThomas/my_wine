# Shared Utility Layer

## Goal

Centralize repeated low-level helpers.

## Why

The codebase has repeated hand-rolled string, formatting, pointer, and debug
helpers because some paths cannot call glibc. That constraint is real, but the
duplication makes correctness harder to maintain.

## Scope

- Syscall-safe string helpers.
- Syscall-safe formatting helpers.
- Debug/write helpers.
- Checked arithmetic and range helpers.
- Guest pointer validation helpers.

## Audit Inputs

Use the audit's duplicate-helper list and
`audit/architecture-boundaries.md` to decide what belongs in the utility layer.
Known duplicate families include:

- Hand-rolled string/memory helpers in `src/loader/import_resolve.c`,
  `src/loader/module_list.c`, `src/loader/dll_loader.c`,
  `src/loader/export_table.c`, `src/loader/import_table.c`,
  `src/msvcrt/kernel32_misc.c`, and `src/loader/pe32_entry.c`.
- Direct stderr/syscall logging helpers in `src/msvcrt/kernel32_priv.h`,
  `src/msvcrt/kernel32_console.c`, `src/msvcrt/crt_stdio.c`,
  `src/msvcrt/crt_stdlib.c`, and crash/setup paths.
- Architecture-dependent pointer writes in `src/loader/teb_peb.c`,
  `src/loader/import_table.c`, `src/msvcrt/crt_refptrs.c`, and
  `src/loader/pe32_entry.c`.

Split host/setup utilities from guest-safe utilities. Do not make a helper
shared unless it is safe for every caller listed in the audit and legal under
the boundary document's dependency table.

## Suggested Steps

1. Identify duplicate helper implementations.
2. Split helpers into libc-allowed and syscall-safe groups.
3. Replace callers gradually.
4. Add tests for utility functions with edge cases.
5. Update the audit when duplicate helper candidates are consolidated or found
   to be intentionally separate.
6. Update `audit/architecture-boundaries.md` if the new utility layer changes
   ownership or allowed dependencies.

## Done Criteria

- Duplicate helper implementations are reduced.
- Utility names clearly state whether they are syscall-safe.
- Callers in post-FS/GS-switch paths do not accidentally call glibc.
