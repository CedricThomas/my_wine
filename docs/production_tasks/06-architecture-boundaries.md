# Architecture Boundaries

## Goal

Define clear layers between PE parsing, mapping, guest setup, syscall dispatch,
Windows API stubs, heap, and architecture-specific code.

## Why

PE32 and PE32+ currently share some code but also have different ABI and runtime
safety constraints. Cleanup needs stable boundaries before moving or renaming
files.

## Scope

- Define layers.
- Define allowed dependencies between layers.
- Mark libc-allowed vs syscall-only areas.
- Separate PE32-only and PE32+-only responsibilities.

## Audit Inputs

Use these audit sections as the first draft of the boundary document:

- "Source Folders And Responsibilities"
- "Runtime Source Inventory"
- "Architecture Classification"
- "Files That Must Not Call Glibc After FS/GS Switch"

The boundary work should preserve the PE32-only, PE32+-only, wrapper-only,
shared, test-only, and sample-only classifications unless a deliberate code
move changes them.

## Suggested Steps

1. Write a short architecture boundary document from current source behavior.
2. Create a dependency rule table.
3. Move only low-risk files first, if movement is needed.
4. Add include path restrictions or reviewer rules later.
5. Update `audit/source-inventory.md` after any file move or layer ownership
   change.

## Done Criteria

- Each major source file has an obvious owning layer.
- PE32-only and PE32+-only code paths are easy to locate.
- Shared code is intentionally shared, not shared by accident.

## Result

- Added `audit/architecture-boundaries.md` as the current boundary map.
- Linked it from `docs/architecture.md` and `docs/onboarding.md`.
- No source files were moved, so `audit/source-inventory.md` classifications
  remain unchanged.
