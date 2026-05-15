# Naming Cleanup

## Goal

Make file, function, and folder names match actual responsibilities.

## Why

Names currently preserve some old assumptions. For example, a file may expose a
`musl_*` interface while not actually implementing musl oldmalloc. Misleading
names slow down future work.

## Scope

- File names.
- Function names.
- Folder names.
- Public type names.
- Comments that describe old names or old architecture.

## Audit Inputs

Use the audit classifications and `audit/architecture-boundaries.md` to avoid
renaming files before ownership is clear. Names should preserve or clarify
these categories:

- PE32-only
- PE32+-only
- shared PE32/PE32+
- wrapper-only
- test-only
- sample-only
- generated or generated-adjacent
- guest-safe/no-glibc
- layer ownership, such as loader core, guest setup, syscall dispatch, Windows
  API stubs, heap, or CRT module policy

## Suggested Steps

1. Wait until architecture boundaries and large-file refactors are mostly done.
2. Identify names that are actively misleading.
3. Rename in small batches.
4. Keep compatibility wrappers only when needed.
5. Update the audit when paths or names change.
6. Update `audit/architecture-boundaries.md` when renamed files change how a
   layer is described.

## Done Criteria

- Names describe current behavior.
- Search results are easier to interpret.
- Old names are removed from docs unless historical context is needed.

## Completion Notes

Completed as a conservative cleanup pass:

- Renamed the generic `setup_signal_handlers()` API to
  `install_crash_signal_handlers()` so the name reflects its crash-handler
  scope.
- Removed stale "moved to" commentary from `src/loader/import_resolve.h`.
- Reworded the PE32 mmap heap backend comment so it is described as the PE32
  backend, not as a replacement for the PE32+ musl backend.
- Updated current docs and Doom95 planning notes where old wrapper/backend,
  removed header, or moved-path names were presented as current state.

No file or folder rename was needed in this pass. Remaining `musl_*` names are
vendored musl source/stub paths or the PE32+ musl backend and are intentionally
named.

Validation:

- `make my_wine64`
- `make my_wine32`
- `make run-tests`
