# Comment Cleanup

## Goal

Remove noisy comments and keep only comments that explain important constraints.

## Why

Over-commented code becomes stale quickly. Low-level runtime code still needs
comments, but they should explain constraints, invariants, ABI behavior, and
non-obvious choices.

## Scope

- Remove comments that restate obvious code.
- Remove stale comments.
- Keep comments about ABI, FS/GS, syscall-safety, memory layout, PE format
  quirks, and Windows/Linux semantic differences.

## Suggested Steps

1. Clean comments in one module at a time.
2. Avoid mixing comment cleanup with functional refactors.
3. Replace long comments with links to current docs where useful.

## Done Criteria

- Comments are shorter and more accurate.
- Important runtime constraints remain documented near the code.
- No stale architecture claims remain in source comments.

