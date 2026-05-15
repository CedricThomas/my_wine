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

## Audit Inputs

Preserve comments that explain constraints captured by the audit and
`audit/architecture-boundaries.md`, especially:

- FS/GS switch restrictions.
- No-glibc/guest-safe code paths.
- PE32 vs PE32+ ABI and pointer-size differences.
- Generated-file workflow and generated-adjacent vendored code.
- Known risky files that need tests before refactoring.
- Layer ownership and allowed dependency exceptions that are not obvious from
  filenames.

Remove or rewrite comments that match the audit's stale-code and stale-doc
candidates.

## Suggested Steps

1. Clean comments in one module at a time.
2. Avoid mixing comment cleanup with functional refactors.
3. Replace long comments with links to current docs where useful.
4. Update the audit if a comment cleanup uncovers a new stale architecture
   claim.
5. Update `audit/architecture-boundaries.md` if comments reveal that a boundary
   rule is missing or inaccurate.

## Done Criteria

- Comments are shorter and more accurate.
- Important runtime constraints remain documented near the code.
- No stale architecture claims remain in source comments.
