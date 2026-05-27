# Refactor Readiness

Date: 2026-05-26

This document turns the current audit into a cleanup plan. It is not a feature
roadmap. The goal is to improve structure, naming, testability, and long-term
maintainability without regressing the existing PE32, PE32+, and DOOM95
behavior.

The tactical execution backlog lives in
`audit/cleanup-backlog.md`. This file stays high-level; the backlog should
carry the concrete sequence.

## Current Health

- The project is refactorable now: `make run-tests` passes cleanly.
- The code already has several useful seams:
  PE parsing vs loader, PE32 process extraction, CRT policy modules, backend
  API wrappers, and dedicated DOOM95-specific files.
- The main obstacle is not missing coverage. It is ownership sprawl and mixed
  responsibilities inside a small number of large files.

## Priority Hotspots

| Priority | Area | Why it is a hotspot | First extraction target |
|---|---|---|---|
| P0 | `src/loader/pe32_entry.c` | Largest PE32 file; mixes generic PE32 launch with DOOM95 shaping and selector-sensitive code. | Split into `pe32_bootstrap`, `pe32_entry_selection`, `pe32_doom95_setup`, and `pe32_guest_launch` helpers/files. |
| P0 | `src/msvcrt/` domain sprawl | One directory owns CRT, ntdll, kernel32, USER32, graphics, audio, and sample shims. | Re-group by subsystem boundaries first, even if APIs stay identical. |
| P0 | `src/msvcrt/kernel32_misc.c` | Catch-all kernel32 implementation with unrelated APIs and different safety requirements. | Split by concern: memory, strings/env/path, TLS/error state, file/path helpers, time/perf. |
| P0 | `src/msvcrt/user32_window.c` | Large lifecycle/state file holding class registry, focus ownership, backend bootstrap, allocation policy, and many Win32 entrypoints. | Extract window registry/state, focus/activation policy, and class registry management. |
| P0 | `src/msvcrt/ddraw_interface.c` | Large DirectDraw shim with guest layout handling, backend policy, debug counters, and COM-ish method tables. | Separate guest-struct marshaling, object lifetime, and backend surface/window operations. |
| P1 | `src/backend/sdl2/rb_event.c` | Central translation point between SDL2 and guest messages. | Split event decoding from message delivery/state side effects. |
| P1 | `src/loader/import_table.c` | Large static registry and lookup logic with PE32/PE32+ divergence. | Separate generated/static import data from lookup policy and mutation helpers. |
| P1 | `src/msvcrt/winmm_doom95.c` | Deep sample-specific multimedia logic living beside generic stubs. | Move under a clearly named sample-compatibility area before deeper cleanup. |
| P1 | `src/msvcrt/kernel32_doom95.c` | Ownership cleanup there is effectively complete for now; the file is just a named seam. | Do not spend more time here unless new Doom95-only `kernel32` behavior appears. |
| P2 | `src/loader/loader_state.h` | Wide mutable global state makes dependencies implicit. | Introduce narrower accessor groups or subsystem state structs before attempting full inversion. |

## Target Directory Shape

This is the direction that best fits the current code. It does not require a
big-bang rewrite.

```text
src/
  loader/
    pe/
    imports/
    modules/
    process/
    guest/
  guest/
    crt/
    ntdll/
    kernel32/
    user32/
    graphics/
    audio/
    compat/
  backend/
    sdl2/
  syscall/
  heap/
  crt/
```

Interpretation:

- `src/crt/` stays as CRT policy and binary introspection.
- Guest-facing Win32 implementations move out of the overloaded `src/msvcrt/`
  identity and into a clearer `src/guest/` split.
- Sample-specific code goes into `guest/compat/` rather than mixing with the
  generic API surface.
- Loader internals should be grouped by responsibility rather than keeping all
  concerns flat under `src/loader/`.

## Sequencing

### Phase 1: Non-behavioral preparation

- Normalize audit docs and keep them current.
- Add missing ownership comments at the top of hotspot files.
- Add a small number of focused tests around any behavior that is currently
  only covered indirectly by DOOM95 or broad integration tests.
- Separate generated/static data from logic where possible.

### Phase 2: File splits without API redesign

- Split `kernel32_misc.c`, `user32_window.c`, `ddraw_interface.c`,
  `pe32_entry.c`, and `rb_event.c` into smaller translation units.
- Keep external function names and headers stable at this stage.
- Use linker-visible file boundaries to make ownership obvious before changing
  call graphs.

### Phase 3: Directory and ownership cleanup

- Move guest-facing subsystems out of `src/msvcrt/` into clearer subsystem
  folders.
- Create an explicit `compat/` area for DOOM95/sample behavior.
- Group loader files by PE parsing, imports, process setup, and module state.

### Phase 4: Interface tightening

- Reduce direct access to `g_loader`.
- Replace weak global coupling where a narrow interface is sufficient.
- Narrow public headers and move private declarations back into subsystem-local
  headers.

### Phase 5: Policy cleanup

- Revisit naming, duplicated helpers, and subsystem-local conventions.
- Remove transitional wrappers once new ownership is stable.
- Tighten docs so that the audit becomes mostly structural rather than warning-heavy.

## Invariants To Preserve

1. `my_wine`, `my_wine32`, and `my_wine64` remain separate binaries.
2. PE32 and PE32+ setup paths stay explicit and architecture-owned.
3. Guest-sensitive code does not gain new hidden libc/TLS dependencies.
4. Host-library calls from guest-facing code continue to restore host selector
   context explicitly.
5. DOOM95 compatibility remains functional while generic runtime cleanup
   proceeds.
6. Generated dispatcher flow remains driven by `include/nt_syscalls.def` and
   `scripts/gen_dispatcher.py`.

## Test Gaps Worth Closing Before Large Moves

| Gap | Why it matters |
|---|---|
| More focused tests around PE32 entry/argument shaping | `pe32_entry.c` currently carries complex behavior with limited unit-level isolation. |
| Direct tests for class registry/window ownership transitions | `user32_window.c` owns non-trivial state transitions that are easy to regress during file splits. |
| More direct backend event translation tests | `rb_event.c` is large and behavior-rich relative to its dedicated coverage. |
| Clear tests for DOOM95-specific path/current-directory behavior | `kernel32_doom95.c` contains logic that should not be accidentally “generalized away”. |
| Targeted tests for import table lookup behavior by architecture | `import_table.c` and `import_resolve.c` differ between PE32 and PE32+. |

## What Not To Do

- Do not start with mass renames or directory moves before splitting hotspot
  files.
- Do not merge PE32 and PE32+ setup paths in the name of “deduplication”.
- Do not push more generic runtime code into DOOM95-specific files.
- Do not introduce convenience libc helpers into dispatcher or guest-sensitive
  code paths.
- Do not try to redesign Win32 API completeness while doing structural cleanup.

## Definition Of Ready For A Deeper Refactor

The project is ready for a substantial cleanup when all of the following are
true:

- hotspot files have been split into smaller ownership-focused units,
- generic guest API code and sample compatibility code are physically separate,
- `g_loader` access is narrower and easier to reason about,
- subsystem-local tests exist for the most fragile state machines,
- and the audit docs still match the actual tree after those moves.
