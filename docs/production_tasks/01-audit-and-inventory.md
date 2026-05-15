# Audit And Inventory

## Goal

Create a source-of-truth inventory of the codebase before refactoring.

Current sources of truth: `audit/source-inventory.md` and
`audit/architecture-boundaries.md`.

## Why

The docs are stale, and the code has multiple architecture-sensitive paths:
PE32, PE32+, guest-safe code, libc-allowed code, generated files, tests, and
samples. Cleanup should start from the actual source layout, not old docs.

## Scope

- List each source folder and its current responsibility.
- Identify PE32-only, PE32+-only, shared, generated, test-only, and sample-only
  files.
- Identify files that must not call glibc after FS/GS is switched.
- Identify obvious duplicate helpers and dead code candidates without removing
  them yet.
- Identify stale docs that should be deleted, archived, or rewritten later.

## Suggested Steps

1. Build a table of source files grouped by responsibility.
2. Mark architecture constraints for each group.
3. Mark glibc-safety constraints for each group.
4. Produce a short cleanup dependency map.
5. Record risky files that need tests before refactoring.
6. Update follow-up production tasks so they consume the audit instead of
   repeating discovery work.
7. Update `docs/production_tasks/README.md` if the audit changes the recommended
   order.

## Required Audit Sections

The inventory document should include:

- Source folders and current responsibilities.
- Concrete runtime source inventory by file or tightly related file group.
- PE32-only, PE32+-only, shared, wrapper-only, generated, test-only, and
  sample-only classifications.
- Files that must not call glibc after FS/GS is switched.
- Generated and ignored artifacts, including their generator inputs and policy.
- Duplicate helper and dead-code candidates, without removing them.
- Stale-doc candidates, without rewriting them early.
- Risky files that need tests before refactoring.
- Cleanup dependency map and follow-up task adjustments.

## Done Criteria

- A new inventory document exists.
- The document lists concrete files, not only folders.
- Follow-up tasks are adjusted if the inventory changes the expected order.
- Follow-up tasks reference the audit sections they should consume.
- `docs/production_tasks/README.md` reflects any order changes from the audit.

## Current Output

- Source inventory: `audit/source-inventory.md`.
- Architecture boundaries: `audit/architecture-boundaries.md`.
- Later tasks should treat that file as the source-of-truth baseline for
  architecture classification, generated artifacts, duplicate/dead-code
  candidates, stale-doc candidates, and risky files needing tests. Treat the
  boundary document as the baseline for ownership layers, allowed dependencies,
  and libc-safe versus syscall-only zones.
- Follow-up task docs have `Audit Inputs` sections that point to the relevant
  parts of the inventory and boundary document.

## Maintenance Rule

If a later task moves files, changes generated-file policy, changes a
glibc-safety boundary, removes duplicate/dead-code candidates, or resolves stale
docs, update `audit/source-inventory.md` in the same change set. If a later
task changes layer ownership, allowed dependencies, or libc/syscall-only zones,
update `audit/architecture-boundaries.md` in the same change set.
