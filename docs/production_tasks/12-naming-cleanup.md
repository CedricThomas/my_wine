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

Use the audit classifications to avoid renaming files before ownership is
clear. Names should preserve or clarify these categories:

- PE32-only
- PE32+-only
- shared PE32/PE32+
- wrapper-only
- test-only
- sample-only
- generated or generated-adjacent
- guest-safe/no-glibc

## Suggested Steps

1. Wait until architecture boundaries and large-file refactors are mostly done.
2. Identify names that are actively misleading.
3. Rename in small batches.
4. Keep compatibility wrappers only when needed.
5. Update the audit when paths or names change.

## Done Criteria

- Names describe current behavior.
- Search results are easier to interpret.
- Old names are removed from docs unless historical context is needed.
