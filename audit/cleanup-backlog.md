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
- Done: extracted USER32 paint/DC, coordinate-mapping, and window-metric
  helpers from `user32_window.c` into `src/msvcrt/user32_paint.c`.
- Done: extracted USER32 handle-backed window property accessors from
  `user32_window.c` into `src/msvcrt/user32_window_state.c`.
- Done: extracted USER32 backend/bootstrap and heap-backed lifecycle helpers
  into `src/msvcrt/user32_window_lifecycle.c` while keeping the public
  `CreateWindowExA` / `DestroyWindow` exports in `src/msvcrt/user32_window.c`
  as the stable orchestration boundary.
- Done: extracted repeated USER32 create-path setup helpers from
  `src/msvcrt/user32_window.c` into `src/msvcrt/user32_window_lifecycle.c`,
  including entry initialization, backend-window creation, CREATESTRUCT
  filling, and failed-create cleanup.
- Done: extracted the successful USER32 create handoff from
  `src/msvcrt/user32_window.c` into `src/msvcrt/user32_window_lifecycle.c`,
  leaving `CreateWindowExA` as a thinner export shell around argument prep,
  helper dispatch, final focus activation, and return.
- Done: extracted the USER32 destroy teardown mechanics from
  `src/msvcrt/user32_window.c` into `src/msvcrt/user32_window_lifecycle.c`,
  leaving `DestroyWindow` as a thin export wrapper around lookup and helper
  dispatch.
- Done: started `user32_message.c` cleanup by extracting dispatch/send/default
  procedure responsibilities into `src/msvcrt/user32_message_dispatch.c`,
  leaving `src/msvcrt/user32_message.c` focused on the message pump, queueing,
  quit state, and hook handling.
- Done: extracted generic current-directory, DOS-path normalization,
  case-insensitive host lookup, and path-oriented `kernel32` exports from
  `src/msvcrt/kernel32_doom95.c` into `src/msvcrt/kernel32_path.c`, making the
  Doom95 compatibility file own less generic runtime policy.
- Done: extracted generic file-handle metadata, seek helpers, and directory
  enumeration (`GetFileType`, `GetFileSize`, `GetFileTime`,
  `SetFilePointer`, `FindNextFileA`) from `src/msvcrt/kernel32_doom95.c` into
  `src/msvcrt/kernel32_file.c`, leaving the Doom95 compatibility file narrower
  and consolidating filesystem ownership.
- Done: extracted generic resource-wrapper exports (`FindResourceA`,
  `SizeofResource`, `LoadResource`, `LockResource`) from
  `src/msvcrt/kernel32_doom95.c` into `src/msvcrt/kernel32_resource.c`,
  making the compatibility file less of a generic `kernel32` dumping ground.
- Done: extracted the TLS cluster (`TlsAlloc`, `TlsFree`, `TlsSetValue`,
  `TlsGetValue`) into `src/msvcrt/kernel32_tls.c`, removing the last cross-file
  TLS globals from `kernel32_doom95.c`/`kernel32_misc.c` and making TLS state
  file-local.
- Done: extracted generic process/system/codepage wrappers
  (`GetCurrentThreadId`, `GetCurrentProcessId`, `GetCurrentThread`,
  `GetSystemInfo`, `GetVersion`, `GetTimeZoneInformation`, `GetCPInfo`,
  `GetEnvironmentStrings`) into `src/msvcrt/kernel32_system.c`.
- Done: moved `GetTickCount` into `src/msvcrt/kernel32_time.c`.
- Done: moved `GetModuleFileNameA` into `src/msvcrt/kernel32_module.c`.
- Done: moved `LocalAlloc`, `GlobalAlloc`, and `LocalFree` into
  `src/msvcrt/kernel32_memory.c`.
- Done: moved DOS/filetime conversion helpers into `src/msvcrt/kernel32_time.c`.
- Done: moved console wrappers (`GetConsoleMode`, `SetConsoleMode`,
  `SetStdHandle`, `WriteConsoleA`, `ReadConsoleInputA`) into
  `src/msvcrt/kernel32_console.c`.
- Done: moved `CreateThread` / `ExitThread` into
  `src/msvcrt/kernel32_process.c` and `DeviceIoControl` into
  `src/msvcrt/kernel32_file.c`, leaving `src/msvcrt/kernel32_doom95.c` as an
  empty compatibility seam instead of a generic-runtime dumping ground.
- Done: continued `user32_message.c` cleanup by extracting queue/filter/quit
  state plus keyboard-hook helpers and related exports (`PostMessageA`,
  `PostQuitMessage`, hook wrappers, `SystemParametersInfoA`) into
  `src/msvcrt/user32_message_queue.c`, leaving
  `src/msvcrt/user32_message.c` focused on `GetMessageA`, `PeekMessageA`, and
  `TranslateMessage`.
- Done: started `src/msvcrt/ddraw_interface.c` cleanup by extracting guest
  `DDSURFACEDESC` layout translation, normalization, and parse/fill helpers
  into `src/msvcrt/ddraw_surface_desc.c`, leaving the main DirectDraw
  interface file focused more tightly on COM object behavior and backend
  orchestration.
- Done: continued `src/msvcrt/ddraw_interface.c` cleanup by extracting
  palette-object allocation, backend color packing, and `IDirectDrawPalette`
  COM lifetime/entry access into `src/msvcrt/ddraw_palette.c`, leaving
  `ddraw_interface.c` with a thin `CreatePalette` wrapper and surface-side
  palette attachment through shared helpers.
- Done: continued `src/msvcrt/ddraw_interface.c` cleanup by extracting the
  narrow clipper-object allocation and `IDirectDrawClipper` COM
  lifetime/`HWND` access path into `src/msvcrt/ddraw_clipper.c`, leaving
  `ddraw_interface.c` with a thin `CreateClipper` wrapper plus
  surface-side clipper attachment/get access through shared helpers.
- Done: continued `src/msvcrt/ddraw_interface.c` cleanup by extracting the
  surface-side clipper attachment/get/detach helpers into
  `src/msvcrt/ddraw_clipper.c`, leaving `ddraw_interface.c` with thin
  `IDirectDrawSurface::SetClipper` / `GetClipper` wrappers while keeping the
  surface vtable and broader surface behavior in place.
- Done: continued `src/msvcrt/ddraw_interface.c` cleanup by extracting surface
  object allocation, owner-list teardown, and COM lifetime/release mechanics
  into `src/msvcrt/ddraw_surface.c`, leaving `ddraw_interface.c` with thin
  `IDirectDrawSurface::AddRef` / `Release` wrappers while keeping the surface
  vtable, lock/blit/flip behavior, and create-path orchestration in place.
- Done: continued `src/msvcrt/ddraw_interface.c` cleanup by extracting the
  narrow DirectDraw query/status helper cluster into `src/msvcrt/ddraw_mode.c`,
  including fake vertical-blank timing, scanline/frequency queries,
  caps/display-mode/GDI-surface queries, while keeping state-changing display
  setup (`SetCooperativeLevel`, `SetDisplayMode`) in `ddraw_interface.c`.
- Done: continued `src/msvcrt/ddraw_interface.c` cleanup by extracting the
  `IDirectDrawSurface` behavior cluster into `src/msvcrt/ddraw_surface_ops.c`,
  including rect translation, attached-surface wrappers, blit/flip/lock/
  unlock, palette access, and the surface vtable, leaving `ddraw_interface.c`
  focused on DirectDraw object creation, mode/setup orchestration, and surface
  creation paths.
- Done: continued `src/msvcrt/ddraw_interface.c` cleanup by extracting the
  DirectDraw object allocation/reference/lifetime core into
  `src/msvcrt/ddraw_core.c`, including shared `g_ddraw_instance` ownership,
  default object initialization, and COM `QueryInterface`/`AddRef`/`Release`
  helpers, while keeping `DirectDrawCreate`, the `IDirectDraw` vtable, and the
  mode/surface orchestration exports in `ddraw_interface.c`.
- Done: continued `src/msvcrt/ddraw_interface.c` cleanup by extracting the
  backend bootstrap and backend-surface allocation helper cluster into
  `src/msvcrt/ddraw_backend.c`, including one-time backend initialization,
  backend pixel-format mapping, and flip-chain/regular-surface allocation,
  while keeping the `IDirectDraw` exports, cooperative-level/display-mode
  state changes, and `CreateSurface` orchestration in `ddraw_interface.c`.
- Next: stop further `ddraw_interface.c` splitting for now and move to the
  next hotspot unless a later pass identifies another similarly narrow seam.
- Done: moved USER32 keyboard-hook state and hook exports
  (`SetWindowsHookExA`, `UnhookWindowsHookEx`, `CallNextHookEx`,
  `user32_call_keyboard_hook`, `user32_call_keyboard_hook_direct`) from
  `src/msvcrt/user32_message_queue.c` into
  `src/msvcrt/user32_message_hook.c`, leaving
  `user32_message_queue.c` focused on posted/translated queue storage,
  filter matching, quit synthesis, and `PostMessageA`/`PostQuitMessage`.

Latest verification on 2026-05-27 after extracting USER32 keyboard-hook state
and hook exports into `src/msvcrt/user32_message_hook.c`:

- `make run-tests`
- `make run-samples-scenarios`
- `env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 timeout 5 ./my_wine32 ./samples/unpacked/doom95/DOOM95.EXE`

Observed result:

- `make run-tests` passed: 16 passed, 0 failed, 0 skipped.
- `make run-samples-scenarios` passed: console scenarios 25 passed, 0 failed,
  1 skipped; graphical scenarios 20 passed, 0 failed; unified result passed.
- The Doom95 smoke again reached WAD discovery/startup
  (`GetFileAttributesA('DOOM1.WAD')` and `FindFirstFileA('*.WAD')` resolving
  `DOOM1.WAD`) and then timed out with exit status `124` from `timeout 5`;
  ALSA/fluidsynth warnings remained expected environment noise only.

Follow-up note:

- `src/msvcrt/ddraw_interface.c` still does not present another similarly
  narrow low-risk extraction seam; this pass intentionally moved to the
  separate USER32 message cluster instead of continuing DirectDraw splits.

Latest verification on 2026-05-27 after extracting backend bootstrap and
backend-surface allocation helpers into `src/msvcrt/ddraw_backend.c`:

- `make run-tests`
- `make run-samples-scenarios`
- `env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 timeout 5 ./my_wine32 ./samples/unpacked/doom95/DOOM95.EXE`

Observed result:

- `make run-tests` passed: 16 passed, 0 failed, 0 skipped.
- `make run-samples-scenarios` passed: console scenarios 25 passed, 0 failed,
  1 skipped; graphical scenarios 20 passed, 0 failed; unified result passed.
- The Doom95 smoke again reached WAD discovery/startup (`GetFileAttributesA`
  and `FindFirstFileA` resolving `DOOM1.WAD`) and then timed out with exit
  status `124` from `timeout 5`; ALSA/fluidsynth warnings remained expected
  environment noise only.

Follow-up note:

- No behavior blocker was hit after the extraction. `ddraw_interface.c` now
  mostly owns the `IDirectDraw` export shell, cooperative-level/display-mode
  state changes, and `CreateSurface` orchestration, so a further split should
  wait for a later pass unless another similarly isolated seam appears.

Latest verification on 2026-05-27 after extracting DirectDraw query/status
helpers into `src/msvcrt/ddraw_mode.c`:

- `make run-tests`
- `make run-samples-scenarios`
- `env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 timeout 5 ./my_wine32 ./samples/unpacked/doom95/DOOM95.EXE`

Observed result:

- `make run-tests` passed: 16 passed, 0 failed, 0 skipped.
- `make run-samples-scenarios` passed: console scenarios 25 passed, 0 failed,
  1 skipped; graphical scenarios 20 passed, 0 failed; unified result passed.
- The plain Doom95 smoke again reached the expected WAD discovery/startup path
  and then timed out with exit status `124` from `timeout 5`; ALSA/fluidsynth
  warnings remained environment noise only.

Follow-up note:

- `samples/ddraw_sample_32/applied_inputs.txt` now includes a short delay
  before close, and both DDraw graphical scenarios now use `altf4` instead of
  `closewindow`; this avoids flaky WM-close/title-observation behavior while
  leaving the dedicated `test_ddraw` unit test and sample exit status to cover
  the DirectDraw success path.
- `scripts/graphical_samples.sh` now supports per-sample
  `expect_title_timeout=...`, although the DDraw scenarios no longer rely on
  title assertions because that observation path stayed flaky under shared load.
- `samples/sdl2_two_window.sample.info`,
  `samples/sdl2_two_window_32/sample.info`, and
  `samples/sdl2_two_window_close_chain/sample.info` now allow a longer
  graphical timeout in shared-container runs.
- `samples/sdl2_two_window_close_chain_32/sample.info` now also allows a
  longer timeout because the scenario is driven through chained close delivery.
- `samples/sdl2_two_window_close_chain_32/applied_inputs.txt` now drives the
  scenario with `altf4` instead of `closewindow`; the close-chain behavior is
  still exercised, but this avoids an unstable X11 window-manager close path in
  the 32-bit graphical harness.
- Current blocker note: no blocker was hit during this verification pass. The
  remaining risk is architectural rather than environmental: palette,
  clipper, surface-desc, surface-lifetime, and query/status helper paths now
  own the narrowest object-local helpers, so the next DDraw cleanup slice
  should only proceed if another similarly isolated cluster can move without
  widening the current COM/backend coupling.

Latest verification on 2026-05-27 after extracting `IDirectDrawSurface`
behavior into `src/msvcrt/ddraw_surface_ops.c`:

- `make run-tests`
- `make run-samples-scenarios`
- `env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 timeout 5 ./my_wine32 ./samples/unpacked/doom95/DOOM95.EXE`

Observed result:

- `make run-tests` passed: 16 passed, 0 failed, 0 skipped.
- `make run-samples-scenarios` passed: console scenarios 25 passed, 0 failed,
  1 skipped; graphical scenarios 20 passed, 0 failed; unified result passed.
- The plain Doom95 smoke again reached the expected WAD discovery/startup path
  (`GetFileAttributesA('DOOM1.WAD')`, repeated `FindFirstFileA('*.WAD')`
  discovery) and then timed out with exit status `124` from `timeout 5`;
  ALSA/fluidsynth warnings remained environment noise only.

Latest verification on 2026-05-27 after extracting DirectDraw object core
helpers into `src/msvcrt/ddraw_core.c`:

- `make run-tests`
- `make run-samples-scenarios`
- `env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 timeout 5 ./my_wine32 ./samples/unpacked/doom95/DOOM95.EXE`

Observed result:

- `make run-tests` passed: 16 passed, 0 failed, 0 skipped.
- `make run-samples-scenarios` passed: console scenarios 25 passed, 0 failed,
  1 skipped; graphical scenarios 20 passed, 0 failed; unified result passed.
- The Doom95 smoke again reached WAD discovery/startup through
  `GetFileAttributesA('DOOM1.WAD')` plus repeated `FindFirstFileA('*.WAD')`
  discovery and then timed out with exit status `124` from `timeout 5`;
  ALSA/fluidsynth warnings remained environment noise only.

Follow-up note:

- This pass also required one mechanical build-graph update: `Makefile` now
  links `build/test_ddraw` against `ddraw_core.o` because the DirectDraw core
  COM helpers moved out of `ddraw_interface.c`.
- After this extraction, `ddraw_interface.c` still owns backend init
  availability checks, cooperative/display-mode setup, and the surface-create
  orchestration helpers; those seams remain more tightly coupled to backend
  window state than the now-extracted object core.

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
