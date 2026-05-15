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

## Completion Notes

Completed as a focused source-comment cleanup:

- Removed migration-history comments such as "moved from", "previously split",
  and stale `g_crt_ctx` references from CRT, loader, and common code.
- Removed decorative section dividers and step-number comments where the code
  already made the flow clear.
- Kept comments that document ABI, FS/GS safety, fixed memory layouts, CRT BSS
  offset discovery, and guest/syscall-safe behavior.
- Rewrote the critical-section slow-path comment to describe the non-obvious
  auto-reset event behavior without narrating each statement.
- Updated current architecture wording from the old PE32 "dual-process" label
  to the wrapper/backend model where it appeared as current documentation.

Validation:

- `make my_wine64`
- `make my_wine32`
- `make run-tests`
- `git diff --check`
