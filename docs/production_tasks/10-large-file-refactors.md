# Large File Refactors

## Goal

Split large or multi-responsibility files into focused modules.

## Why

Files that mix parsing, policy, memory access, diagnostics, architecture logic,
and runtime state are hard to reason about and risky to change.

## Scope

Likely candidates:

- `src/loader/pe32_entry.c`
- `src/msvcrt/kernel32_misc.c`
- `src/syscall/dispatcher.c`
- `src/loader/import_resolve.c`
- `src/msvcrt/crt_offset_discovery.c`
- `src/loader/import_table.c`
- `src/loader/teb_peb.c`
- `src/loader/guest_setup.c`
- `src/msvcrt/crt_32_stub.c`

## Audit Inputs

Use the audit's "Risky Files Needing Tests Before Refactor" section as the
priority list, then use `audit/architecture-boundaries.md` to choose the target
layer for each extracted module. Before splitting a file, copy its architecture,
allowed dependency, and glibc-safety constraints into the refactor notes so
extracted modules do not accidentally cross guest-safe boundaries.

## Suggested Steps

1. Add or improve tests around the target file first.
2. Extract pure helpers before stateful logic.
3. Extract architecture-specific code into separate files.
4. Keep commits behavior-preserving.
5. Rename after extraction, not before.
6. Update `audit/source-inventory.md` after each file split.
7. Update `audit/architecture-boundaries.md` after each split that changes
   ownership, dependency rules, or libc zones.

## Done Criteria

- The refactored files have one clear responsibility each.
- Tests cover the behavior that was moved.
- No unrelated style churn is mixed into the refactor.
