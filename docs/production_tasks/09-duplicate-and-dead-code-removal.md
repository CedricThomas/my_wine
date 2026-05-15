# Duplicate And Dead Code Removal

## Goal

Remove code that is duplicated, unused, obsolete, or misleading.

## Why

Dead code and duplicate paths make low-level runtime work risky. They also keep
stale architecture assumptions alive.

## Scope

- Remove unused functions and files.
- Remove obsolete comments and stale compatibility paths.
- Consolidate duplicated implementation paths after tests exist.
- Keep generated files and sample artifacts out of source control.

## Audit Inputs

Start from the audit's "Duplicate Helpers And Dead-Code Candidates" section and
check `audit/architecture-boundaries.md` before deleting anything that sits on a
layer boundary. Initial candidates are:

- `src/trampoline.S`
- `src/msvcrt/ntdll_synchronization.c` `find_semaphore`
- `src/crt/crt_watcom.c` TODO/fallback offset paths

Do not delete behavior solely because it is not obvious. Require compiler,
linker, search, test, or runtime evidence, especially for PE32, guest-safe
paths, and files that the boundary document marks as architecture-critical.

## Suggested Steps

1. Use compiler warnings, `rg`, and linker evidence to identify candidates.
2. Remove one category at a time.
3. Prefer deletion over abstraction when code is truly unused.
4. Add tests before deleting behavior that might be implicitly relied on.
5. Remove or update stale docs that reference deleted code, and update the
   audit candidate list.
6. Update `audit/architecture-boundaries.md` if deleting a file changes layer
   ownership or allowed dependencies.

## Done Criteria

- Removed code has a clear reason.
- `make run-tests` passes after each deletion batch.
- No stale docs continue to reference deleted behavior.
