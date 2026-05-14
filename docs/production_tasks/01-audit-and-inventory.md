# Audit And Inventory

## Goal

Create a source-of-truth inventory of the codebase before refactoring.

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

## Done Criteria

- A new inventory document exists.
- The document lists concrete files, not only folders.
- Follow-up tasks are adjusted if the inventory changes the expected order.

