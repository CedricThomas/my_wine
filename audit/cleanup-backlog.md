# Cleanup Backlog

Date: 2026-05-26

This is the executable cleanup plan for the current codebase. It is ordered to
reduce risk: increase confidence first, split oversized ownership units second,
then isolate architecture and sample-specific policy, then tighten interfaces,
then refresh docs once the new structure is real.

Use this document as the working queue for the next refactor series.

## Current Progress

- Done: native test coverage for Doom95 path/current-directory helpers.
- Done: end-to-end PE32 launch coverage for `my_wine32` argv-path and
  `WINE32_PE_PATH` fallback using `entry_test_32.exe`.
- Done: runtime test harness updated so both checks run in default `make run-tests`.
- Done: added `wine_reset_current_directory_cache()` so path-helper state can
  be tested without process restarts.
- Done: first non-behavioral `pe32_entry.c` cleanup pass to separate bootstrap,
  CRT/import setup, guest-state setup, and final handoff stages inside the file.
- Done: first real `pe32_entry.c` extraction by moving Doom95-specific runtime
  shaping into `src/loader/pe32_doom95_compat.c`.
- Done: extracted PE32 entry-resolution and CRT startup bypass logic into
  `src/loader/pe32_entry_resolve.c`.
- Done: extracted PE32 guest-launch / FS setup logic into
  `src/loader/pe32_guest_launch.c`.
- Done: extracted the remaining PE32 bootstrap/setup helpers into
  `src/loader/pe32_bootstrap.c`, leaving `src/loader/pe32_entry.c` as the
  thin orchestration layer.
- Done: started `kernel32_misc.c` cleanup by extracting the virtual-memory API
  cluster into `src/msvcrt/kernel32_memory.c`.
- Done: extracted time/performance APIs from `kernel32_misc.c` into
  `src/msvcrt/kernel32_time.c`.
- Done: extracted file/handle helpers from `kernel32_misc.c` into
  `src/msvcrt/kernel32_file.c`, including the PE32 fallback `CreateFileA`
  handle state.
- Done: extracted ANSI string/codepage helpers from `kernel32_misc.c` into
  `src/msvcrt/kernel32_string.c`.
- Done: started `user32_window.c` cleanup by extracting the registered class
  table and `RegisterClassA` into `src/msvcrt/user32_class_registry.c`.
- Done: extracted focus/activation policy and Win32 focus APIs from
  `user32_window.c` into `src/msvcrt/user32_focus.c`.
- Done: started `rb_event.c` cleanup by extracting backend event state
  (active-window tracking, window routes, synthetic queue) into
  `src/backend/sdl2/rb_event_state.c`.
- Done: extracted shutdown queue/policy from `rb_event.c` into
  `src/backend/sdl2/rb_event_shutdown.c`.
- Done: extracted focus policy, synthetic focus messages, and ALT/system-key
  state handling from `rb_event.c` into `src/backend/sdl2/rb_event_focus.c`.
- Done: extracted keyboard/text translation and watched-key queue handling
  from `rb_event.c` into `src/backend/sdl2/rb_event_keyboard.c`.
- Done: extracted mouse and non-focus window-event translation from
  `rb_event.c` into `src/backend/sdl2/rb_event_window_mouse.c`.
- Done: extracted USER32 geometry/visibility/simple state APIs from
  `user32_window.c` into `src/msvcrt/user32_window_ops.c`.
- Next: reassess the now-thin `rb_event.c`, then continue splitting the
  remaining `user32_window.c` lifecycle and paint/DC responsibilities.

## Operating Rules

1. Coverage before movement.
2. File splits before directory moves.
3. Ownership cleanup before naming cleanup.
4. Keep PE32 and PE32+ explicit.
5. Keep guest-sensitive code free of accidental libc/TLS regressions.
6. Keep DOOM95 compatibility working during every milestone.

## Milestones

| Milestone | Goal | Exit condition |
|---|---|---|
| M0 | Stabilize baseline | Tests and sample coverage gaps are cataloged and first-priority gaps are covered. |
| M1 | Split hotspot files | Largest mixed-responsibility files are broken into smaller translation units without behavior changes. |
| M2 | Isolate compatibility code | DOOM95/sample-specific behavior is physically separated from generic runtime code. |
| M3 | Reduce global and implicit coupling | `g_loader`, weak globals, and cross-subsystem leakage are reduced behind narrower contracts. |
| M4 | Normalize structure | Directory layout matches subsystem ownership. |
| M5 | Rewrite docs | Audit/docs reflect the post-cleanup structure, not the pre-cleanup one. |

## M0: Coverage First

### M0.1 PE32 boot and entry behavior

Targets:

- [src/loader/pe32_entry.c](/home/arzad/Playground/projects/my_wine/src/loader/pe32_entry.c)
- [src/loader/pe32_process.c](/home/arzad/Playground/projects/my_wine/src/loader/pe32_process.c)

Tasks:

- Add tests around entry-point selection behavior.
- Add tests around command-line and environment shaping.
- Add tests around DOOM95-specific argv shaping and fallback behavior.
- Add tests around current working directory and PE path propagation.

Good candidates:

- new focused native tests in `tests/`
- new tiny PE32 sample programs under `samples/`

### M0.2 USER32 state transitions

Targets:

- [src/msvcrt/user32_window.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/user32_window.c)
- [src/msvcrt/user32_message.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/user32_message.c)
- [src/msvcrt/user32_dialog.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/user32_dialog.c)

Tasks:

- Add explicit tests for class registration and duplicate registration behavior.
- Add tests for active/focus window transitions on create/destroy.
- Add tests for replacement-window selection when a focused/active window closes.
- Add tests for dialog/window ownership edge cases that currently rely on broad integration behavior.

### M0.3 Backend event translation

Targets:

- [src/backend/sdl2/rb_event.c](/home/arzad/Playground/projects/my_wine/src/backend/sdl2/rb_event.c)
- [src/backend/sdl2/rb_window.c](/home/arzad/Playground/projects/my_wine/src/backend/sdl2/rb_window.c)

Tasks:

- Add direct translation tests for SDL window/input events to guest messages.
- Add coverage for active-window tracking and close-chain behavior.
- Add tests for edge cases currently only exercised by `sdl2_*` samples.

### M0.4 Import-resolution architecture differences

Targets:

- [src/loader/import_table.c](/home/arzad/Playground/projects/my_wine/src/loader/import_table.c)
- [src/loader/import_resolve.c](/home/arzad/Playground/projects/my_wine/src/loader/import_resolve.c)

Tasks:

- Add tests that pin PE32 linear-scan behavior versus PE32+ binary-search behavior.
- Add tests for ordinal lookup fallbacks and duplicate import names across DLL spellings.
- Add tests around missing import handling and error-path stability.

### M0.5 DOOM95 compatibility behavior

Targets:

- [src/msvcrt/kernel32_doom95.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/kernel32_doom95.c)
- [src/msvcrt/winmm_doom95.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/winmm_doom95.c)
- [src/msvcrt/gdi32_doom95.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/gdi32_doom95.c)
- [src/crt/crt_watcom.c](/home/arzad/Playground/projects/my_wine/src/crt/crt_watcom.c)

Tasks:

- Add targeted tests for path normalization and current-directory behavior.
- Add regression tests for Watcom/DOOM95 CRT offset assumptions where feasible.
- Promote some manual Doom95 smoke checks into reproducible scripts or sample-driven checks.

## M1: Split Hotspot Files

This milestone is non-negotiable. Do not do broad directory moves before it.

### M1.1 Split `pe32_entry.c` (1109 LOC)

Target:

- [src/loader/pe32_entry.c](/home/arzad/Playground/projects/my_wine/src/loader/pe32_entry.c)

Desired decomposition:

- `pe32_bootstrap`: early setup, env lookup, image mapping, basic sanity checks
- `pe32_entry_selection`: entry symbol discovery and selection policy
- `pe32_doom95_setup`: Doom95-specific command/stack shaping
- `pe32_guest_launch`: selector switch, crash handler install, final guest jump

Smells to remove:

- file-level responsibility overload
- generic PE32 flow mixed with sample-specific policy
- setup logic mixed with guest-launch logic

Status:

- Done. The file is now reduced to orchestration, with bootstrap, entry
  selection, Doom95 compatibility policy, and guest launch extracted into
  dedicated PE32 modules.

### M1.2 Split `kernel32_misc.c` (632 LOC)

Target:

- [src/msvcrt/kernel32_misc.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/kernel32_misc.c)

Desired decomposition:

- `kernel32_memory`
- `kernel32_strings_paths`
- `kernel32_tls_error`
- `kernel32_time_perf`
- `kernel32_misc_legacy` only if temporary

Smells to remove:

- catch-all API ownership
- mixed safety constraints in one file
- unrelated helper clustering

### M1.3 Split `user32_window.c` (1081 LOC)

Target:

- [src/msvcrt/user32_window.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/user32_window.c)

Desired decomposition:

- `user32_class_registry`
- `user32_window_registry`
- `user32_focus_activation`
- `user32_window_api`
- `user32_backend_bootstrap`

Smells to remove:

- state model, allocation policy, backend policy, and Win32 API entrypoints all mixed together
- hidden contract around active/focus ownership

### M1.4 Split `ddraw_interface.c` (1676 LOC)

Target:

- [src/msvcrt/ddraw_interface.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/ddraw_interface.c)

Desired decomposition:

- `ddraw_guest_layouts`
- `ddraw_surface_desc`
- `ddraw_objects`
- `ddraw_surfaces`
- `ddraw_palette`
- `ddraw_backend_glue`

Smells to remove:

- guest marshaling mixed with object lifetime and backend operations
- Doom95-oriented minimal support mixed with generic DirectDraw object surface

### M1.5 Split `rb_event.c` (1091 LOC)

Target:

- [src/backend/sdl2/rb_event.c](/home/arzad/Playground/projects/my_wine/src/backend/sdl2/rb_event.c)

Desired decomposition:

- `rb_event_translate_window`
- `rb_event_translate_input`
- `rb_event_queue_or_dispatch`
- `rb_event_active_window_policy`

Smells to remove:

- translation and policy side effects in the same control flow
- large switch-heavy code with hidden ownership rules

### M1.6 Split `import_table.c` (1024 LOC)

Target:

- [src/loader/import_table.c](/home/arzad/Playground/projects/my_wine/src/loader/import_table.c)

Desired decomposition:

- `import_table_data`
- `import_table_lookup`
- `import_table_mutation`
- `import_table_pe32_compat`

Smells to remove:

- static data and mutation logic tightly coupled
- architecture differences embedded in broad shared flow

## M2: Isolate DOOM95 and Other Compatibility Code

### M2.1 Create explicit compatibility ownership

Targets:

- [src/msvcrt/kernel32_doom95.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/kernel32_doom95.c)
- [src/msvcrt/winmm_doom95.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/winmm_doom95.c)
- [src/msvcrt/gdi32_doom95.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/gdi32_doom95.c)
- [src/msvcrt/launcher_ui_stubs.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/launcher_ui_stubs.c)
- [src/msvcrt/launcher_kernel32_stubs.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/launcher_kernel32_stubs.c)
- [src/msvcrt/dplay_stub.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/dplay_stub.c)
- [src/crt/crt_watcom.c](/home/arzad/Playground/projects/my_wine/src/crt/crt_watcom.c)

Tasks:

- Move compatibility behavior into a clearly named compatibility area.
- Separate generic path/filesystem/process helpers from Doom95-only policy.
- Document which parts are generic runtime, which parts are Watcom-specific, and which parts are Doom95-only compatibility.

### M2.2 Stop generic code from drifting into compatibility files

Tasks:

- Audit new generic helpers added during splits and place them in generic subsystem files instead.
- Use compatibility files only for behavior that is truly sample-specific or guest-specific.

## M3: Reduce Global and Implicit Coupling

### M3.1 Narrow loader state ownership

Target:

- [src/loader/loader_state.h](/home/arzad/Playground/projects/my_wine/src/loader/loader_state.h)

Tasks:

- Split loader-global concerns into smaller ownership groups.
- Reduce direct struct-field access where accessors can clarify invariants.
- Isolate module registry, selector state, and image/process metadata where possible.

Smells to remove:

- broad mutable shared state
- architecture and subsystem details cohabiting in one global struct

### M3.2 Reduce weak-symbol coupling

Targets:

- [src/backend/sdl2/rb_sdl2_priv.h](/home/arzad/Playground/projects/my_wine/src/backend/sdl2/rb_sdl2_priv.h)
- guest/backend bridge points using weak globals

Tasks:

- Replace weak globals with explicit bridge functions or bridge state where feasible.
- Make host-context enter/leave ownership obvious and centralized.
- Avoid subsystem code depending on “if linked, maybe available” behavior unless it is truly optional.

### M3.3 Tighten subsystem headers

Tasks:

- Move private declarations out of public headers.
- Split headers that expose mixed concerns.
- Keep `include/` for real cross-subsystem contracts, not convenience leakage.

## M4: Normalize Structure

Only begin this after M1 and most of M2-M3 are done.

### M4.1 Re-home guest-facing code

Current problem:

- `src/msvcrt/` is acting as a bucket rather than a subsystem.

Target shape:

```text
src/guest/
  crt/
  ntdll/
  kernel32/
  user32/
  graphics/
  audio/
  compat/
```

Tasks:

- Move code by subsystem ownership, not by filename aesthetics.
- Keep linker-visible behavior stable while moving files.

### M4.2 Re-home loader internals

Target shape:

```text
src/loader/
  pe/
  imports/
  modules/
  process/
  guest/
```

Tasks:

- Separate parser-adjacent loader logic from module registry and process setup.
- Keep PE32-specific process/launch logic clearly isolated.

## M5: Final Documentation Pass

Tasks:

- Rewrite `audit/` to describe the new structure rather than the old debt.
- Rewrite only the useful docs under `docs/`; archive or delete stale planning material if the user wants that later.
- Add a maintainer-facing architecture overview that explains subsystem boundaries, libc zones, and PE32/PE32+ split points in one place.

## Ordered Workstreams

If you want a strict execution order, use this:

1. Add PE32 boot/argv/env coverage.
2. Add USER32 state-transition coverage.
3. Add backend event translation coverage.
4. Add Doom95 path/current-directory coverage.
5. Split `pe32_entry.c`.
6. Split `kernel32_misc.c`.
7. Split `user32_window.c`.
8. Split `ddraw_interface.c`.
9. Split `rb_event.c`.
10. Split `import_table.c`.
11. Move Doom95 compatibility code into explicit ownership.
12. Reduce `g_loader` and weak-global coupling.
13. Re-home directories.
14. Refresh docs.

## Do Not Do These Early

- Do not merge PE32 and PE32+ launch/setup code.
- Do not start with large path renames or broad directory churn.
- Do not replace explicit architecture branches with clever abstraction before coverage is stronger.
- Do not try to “finish Win32 support” while cleaning structure.
- Do not mix generic runtime helpers back into Doom95 compatibility files.

## Suggested First Two Implementation Sprints

### Sprint A

- Add PE32 boot/argv/env tests.
- Add Doom95 path/current-directory tests.
- Add a short ownership comment to each P0 hotspot file.

### Sprint B

- Split `pe32_entry.c` with no public-behavior change.
- Add any missing regression tests discovered during the split.
- Re-run full test and sample sweep.

Those two sprints give the highest leverage because they reduce risk in the
most fragile PE32 and Doom95-sensitive area before touching the rest of the
tree.
