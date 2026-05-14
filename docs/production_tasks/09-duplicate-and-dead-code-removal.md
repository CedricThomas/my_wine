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

## Suggested Steps

1. Use compiler warnings, `rg`, and linker evidence to identify candidates.
2. Remove one category at a time.
3. Prefer deletion over abstraction when code is truly unused.
4. Add tests before deleting behavior that might be implicitly relied on.

## Done Criteria

- Removed code has a clear reason.
- `make run-tests` passes after each deletion batch.
- No stale docs continue to reference deleted behavior.

