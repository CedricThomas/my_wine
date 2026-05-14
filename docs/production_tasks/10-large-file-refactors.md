# Large File Refactors

## Goal

Split large or multi-responsibility files into focused modules.

## Why

Files that mix parsing, policy, memory access, diagnostics, architecture logic,
and runtime state are hard to reason about and risky to change.

## Scope

Likely candidates:

- `src/loader/import_resolve.c`
- `src/loader/pe32_entry.c`
- `src/loader/guest_setup.c`
- `src/syscall/dispatcher.c`
- CRT patching and offset discovery files

## Suggested Steps

1. Add or improve tests around the target file first.
2. Extract pure helpers before stateful logic.
3. Extract architecture-specific code into separate files.
4. Keep commits behavior-preserving.
5. Rename after extraction, not before.

## Done Criteria

- The refactored files have one clear responsibility each.
- Tests cover the behavior that was moved.
- No unrelated style churn is mixed into the refactor.

