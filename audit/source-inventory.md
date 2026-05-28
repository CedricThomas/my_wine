# Source Inventory Audit

Date: 2026-05-26

This inventory is derived from the current tree under `src/`, `include/`,
`tests/`, `scripts/`, `samples/`, and `Makefile`. It intentionally does not
trust most historical markdown in `docs/` or `samples/doom95/docs/`.

## Current Scope

- Runtime source files: 86 C files, 6 assembly files.
- Public/shared headers: 20 files under `include/`.
- Native test sources: 28 C files under `tests/`.
- Host scripts: 7 top-level files under `scripts/`.
- Sample directories: 47 top-level directories under `samples/`.
- The primary runtime binaries are `my_wine`, `my_wine64`, and `my_wine32`.
- `src/syscall/dispatcher_generated.c` is generated from
  `include/nt_syscalls.def` by `scripts/gen_dispatcher.py`.
- Verification on this tree: `make run-tests` passed on 2026-05-26
  with 14 passed, 0 failed, 0 skipped.

## Top-Level Responsibilities

| Path | Role | Notes |
|---|---|---|
| `src/` | Runtime implementation. | Split across wrapper, loader core, guest API stubs, syscall dispatcher, heap, CRT policy, and SDL2 backend. |
| `include/` | Public and cross-module headers. | Contains ABI, PE, NT, backend, and guest-facing type contracts. |
| `tests/` | Native test programs. | Focused on PE parsing, loader logic, relocations, dispatcher, TEB/PEB, backend, ddraw/dsound, and user32 behavior. |
| `scripts/` | Build/test/sample tooling. | Includes dispatcher generation, test runner, sample runner, and archive unpacking. |
| `samples/` | Guest PE inputs and sample assets. | Includes small validation programs and the DOOM95 integration sample. |
| `audit/` | Current refactor-oriented documentation. | Should be kept aligned with code moves and ownership changes. |
| `docs/` | Historical docs. | Useful only as background; not source of truth. |

## Runtime Layout

### Wrapper and Root Runtime

| Files | Responsibility | Constraints |
|---|---|---|
| `src/wrapper_main.c` | Detects PE32 vs PE32+ and `execvp`s `my_wine32` or `my_wine64`. | Host-only wrapper. Glibc is fine. |
| `src/main.c` | `my_wine64` entry: environment config, image mapping, CRT detection, imports, TEB/PEB/stack setup, guest launch. | Transition-sensitive after guest segment state is prepared. |
| `src/run_guest.S` | PE32+ guest entry trampoline. | Assembly-only, no libc. |
| `src/common.c` | Shared support such as protection helpers and loader global state definition. | Mixed setup/runtime utility code. |
| `src/debug.c` | Debug-level parsing and `DEBUG` helpers. | Uses stdio; not safe in syscall-only paths. |
| `src/pe_headers.c` | DOS/NT header parsing, RVA helpers, section lookup. | Shared parser layer. |
| `src/pe_imports.c` | Import descriptor and thunk parsing. | Shared parser layer. |
| `src/pe_rip_scan.c` | Import thunk pattern scanning for x86 and x86_64. | Shared parser/loader support. |
| `src/pe_symbols.c` | COFF symbol parsing used by CRT/entry discovery. | Setup-time helper. |
| `src/pe_priv.h` | Internal PE helper declarations shared by parser/loader/tests. | Internal-only header. |

### Loader Core (`src/loader/`)

| Files | Responsibility | Constraints |
|---|---|---|
| `entry.c` | PE32+ guest launch entrypoint into `setup_guest_and_run()`. | Post-setup sensitive. |
| `guest_setup.c` | Crash handlers, thunk generation, dispatcher UNIX stack, GS setup, CRT finalization, guest jump. | Transition-sensitive. |
| `gs_base.c` | Host/guest GS handling for x86_64. | Segment-base sensitive. |
| `crash_handlers.c` | POSIX signal handling, SEH diagnostics, altstack management. | Signal-safety matters. |
| `teb_peb.c` | TEB/PEB allocation, stack setup, process parameters, host selector bookkeeping. | Architecture-sensitive. |
| `image_mapper.c` | PE image mapping, section copy, protections, preferred-base logic. | Shared setup/runtime support. |
| `relocations.c` | PE32 and PE32+ relocation application. | High-risk memory patching logic. |
| `import_table.c` | Static import table and lookup/writeback helpers. | Large shared registry; mixed PE32/PE32+ logic. |
| `import_init.c` | Runtime initialization of import entries. | Shared setup. |
| `import_resolve.c` | IAT resolution, thunk handling, ordinal/name resolution, module export lookup. | Explicitly glibc-free in sensitive paths. |
| `dll_loader.c` | Maps and relocates DLLs, resolves DLL imports, registers modules. | Shared loader core, glibc-free in guest-sensitive usage. |
| `dll_path.c` | DLL search path resolution and cache. | Shared setup/runtime support. |
| `module_list.c` | Loaded-module registry. | Shared runtime state, glibc-free usage expected. |
| `export_table.c` | Export cache parsing and lookup. | Shared runtime support. |
| `ordinal_table.c` | Static ordinal-name lookup table. | Data/helper layer. |
| `peb_ldr.c` | PEB loader list population and bookkeeping. | Shared setup/runtime support. |
| `pe32_entry.c` | Standalone `my_wine32` entrypoint with PE32 setup, FS setup, imports, DOOM95-specific stack seeding, guest jump. | Largest single PE32 integration unit. |
| `pe32_process.c` | Extracted PE32 process-parameter, argv/env, and PEB/LDR setup helpers. | PE32-only. |
| `pe32_run_guest.S` | PE32 guest trampoline. | Assembly-only. |
| `loader_state.h` | Central loader-global struct and accessors. | Shared mutable global surface. |
| `loader_priv.h` and other loader headers | Internal contracts among loader modules. | Keep private; avoid bleeding into unrelated layers. |

### Syscall Dispatcher (`src/syscall/`)

| Files | Responsibility | Constraints |
|---|---|---|
| `thunk_gen.c` | Generates NT syscall thunks for guest code. | Shared setup/runtime boundary. |
| `dispatcher_entry.c` | Prepares dispatcher UNIX stack state. | Dispatcher-critical. |
| `dispatcher_entry_asm.S` | ABI bridge from guest thunk into host dispatcher. | Assembly-only. |
| `dispatcher.c` | Decodes arguments and routes NT syscall handlers. | Syscall-only path. |
| `dispatcher_generated.c` | Generated switch bodies included by `dispatcher.c`. | Generated artifact. |
| `abi_wrappers.c` / `abi_wrappers.h` | ABI-safe wrappers around low-level helpers and Linux syscalls. | Shared syscall-safe support. |
| `syscalls_inline.h` | Inline raw Linux syscall wrappers. | Primitive no-libc layer. |
| `clone64.S` | x86_64 clone helper. | Assembly-only. |
| `clone.S` | i386 clone helper. | Assembly-only. |
| `mmap2_asm.S` | i386 `mmap2` helper to avoid libc/TLS issues. | PE32-only assembly. |

### Guest API Stubs (`src/msvcrt/`)

This directory is the largest runtime area and mixes several concerns:
CRT emulation, kernel32/ntdll APIs, handle management, user32, ddraw, dsound,
launcher helpers, and DOOM95-specific compatibility behavior.

| Area | Representative files | Notes |
|---|---|---|
| CRT startup/state | `crt_startup.c`, `crt_32_stub.c`, `crt_globals.c`, `crt_refptrs.c`, `crt_offset_discovery.c`, `crt_file.c`, `crt_stdio.c`, `crt_stdlib.c`, `crt_misc.c` | Mixed generic CRT emulation plus architecture-specific compatibility. |
| Handle and object state | `handle_manager.c`, `ntdll_handle.c`, `ntdll_objects.c` | Core guest object model; shared across subsystems. |
| ntdll syscall handlers | `ntdll_io.c`, `ntdll_memory.c`, `ntdll_process.c`, `ntdll_synchronization.c`, `ntdll_time.c` | Guest-callable, expected to stay syscall-safe. |
| kernel32 generic APIs | `kernel32_console.c`, `kernel32_file.c`, `kernel32_memory.c`, `kernel32_misc.c`, `kernel32_module.c`, `kernel32_path.c`, `kernel32_process.c`, `kernel32_resource.c`, `kernel32_sync.c`, `kernel32_system.c`, `kernel32_time.c`, `kernel32_tls.c` | Broad Win32 surface; `kernel32_misc.c` is a shrinking catch-all, while `kernel32_path.c` owns current-directory/path normalization/path-oriented exports, `kernel32_file.c` owns generic file-handle metadata/seek/directory-enumeration helpers, `kernel32_resource.c` owns the resource-wrapper export surface, `kernel32_tls.c` owns file-local TLS slot state, and `kernel32_system.c` owns generic process/system/codepage wrappers. |
| USER32 | `user32_window.c`, `user32_window_lifecycle.c`, `user32_window_ops.c`, `user32_window_state.c`, `user32_paint.c`, `user32_class_registry.c`, `user32_focus.c`, `user32_message.c`, `user32_message_dispatch.c`, `user32_message_hook.c`, `user32_message_queue.c`, `user32_message_priv.h`, `user32_input.c`, `user32_dialog.c`, `user32_weak_stub.c`, `user32_priv.h` | Window/message/input implementation tied to SDL2 backend and host/guest selector switching, with message dispatch separated from queue/filter/quit state and keyboard-hook state/export handling kept in a narrow helper file. |
| DirectDraw | `ddraw_backend.c`, `ddraw_core.c`, `ddraw_interface.c`, `ddraw_clipper.c`, `ddraw_mode.c`, `ddraw_palette.c`, `ddraw_surface.c`, `ddraw_surface_desc.c`, `ddraw_surface_ops.c`, `ddraw_priv.h` | Minimal maintained path focused on DOOM95 and samples, with backend initialization and backend-surface allocation helpers in `ddraw_backend.c`, DirectDraw object allocation/reference/lifetime core in `ddraw_core.c`, guest `DDSURFACEDESC` translation, palette support, clipper object/surface-attachment helpers, surface object lifetime/owner-list mechanics, surface COM/backend operation methods, and narrow query/status helpers split from the remaining DirectDraw exports plus cooperative-level/display-mode/surface-create orchestration in `ddraw_interface.c`. |
| DirectSound | `dsound_interface.c`, `dsound_buffer.c`, `dsound_buffer_create.c`, `dsound_priv.h` | Guest-facing audio buffers plus backend glue, with DirectSound object ownership/backend-open logic in `dsound_interface.c`, buffer creation/allocation/backend-buffer setup in `dsound_buffer_create.c`, and buffer COM methods/status/control operations in `dsound_buffer.c`. |
| DOOM95-specific shims | `kernel32_doom95.c`, `gdi32_doom95.c`, `winmm_doom95.c`, `launcher_ui_stubs.c`, `launcher_kernel32_stubs.c`, `dplay_stub.c`, `advapi32_registry.c`, `resource_win32.c` | Compatibility surface that should be isolated during future cleanup. `kernel32_doom95.c` is now an intentionally empty seam reserved for any future Doom95-only `kernel32` behavior rather than a home for generic runtime helpers. |

### Heap (`src/heap/`)

| Files | Responsibility | Constraints |
|---|---|---|
| `wine_heap.c`, `wine_heap.h`, `heap_backend.h` | Windows heap API and backend dispatch. | Guest-facing allocator layer. |
| `pe32plus_musl_malloc_backend.c` | x86_64 heap backend using vendored musl allocator pieces. | PE32+-only backend. |
| `pe32_mmap_heap_backend.c` | Simple PE32 mmap-per-allocation backend. | PE32-only backend. |
| `musl_src/*`, `musl_stubs/*` | Vendored allocator implementation and compile stubs. | Treat as vendored internals. |

### CRT Policy (`src/crt/`)

| Files | Responsibility | Notes |
|---|---|---|
| `crt.c`, `crt_priv.h` | Selects active CRT module and dispatches CRT policy hooks. | Setup-time coordination layer. |
| `crt_mingw.c` | MinGW-specific CRT offset/refptr/BSS logic. | Generic sample/runtime support. |
| `crt_watcom.c` | Watcom/DOOM95-specific CRT handling. | Sample-driven compatibility logic. |

### SDL2 Backend (`src/backend/sdl2/`)

| Files | Responsibility | Notes |
|---|---|---|
| `rb_init.c` | SDL init/shutdown, driver fallback, signal handling, event thread setup. | Host-library boundary. |
| `rb_event.c` | SDL event translation to guest message flow. | Large message/backend seam. |
| `rb_window.c` | Window lifecycle and host window state. | USER32 bridge. |
| `rb_surface.c` | Surface creation, lock/unlock, blit, flip. | DirectDraw bridge. |
| `rb_palette.c` | Palette management. | DirectDraw bridge. |
| `rb_audio.c` | Audio device and audio buffer mixing. | DirectSound bridge. |
| `rb_input.c` | Input state and cursor glue. | USER32 bridge. |
| `rb_sdl2_priv.h` | Shared backend-private structs and host-context switching helpers. | Critical backend seam. |

## Public Header Surface

| Header group | Files | Role |
|---|---|---|
| PE and parser | `pe.h`, `pe_parser.h` | PE structures and parser contracts. |
| ABI/common/debug | `wine_abi.h`, `common.h`, `debug.h`, `syscall_safe_utils.h` | Shared runtime conventions and safe helper surface. |
| Guest APIs | `msvcrt.h`, `kernel32.h`, `ntdll.h`, `nt_constants.h`, `handle_manager.h` | Guest-visible API declarations and NT constants. |
| CRT policy | `crt.h` | CRT-module abstraction. |
| Backend and media | `render_backend.h`, `ddraw_types.h`, `dsound_types.h`, `user32_types.h` | Shared types for SDL2-backed subsystems. |
| Syscall support | `syscall/dispatcher.h`, `syscall/dispatcher_entry.h`, `syscall/thunk_gen.h`, `nt_syscalls.def` | Dispatcher contracts and generator input. |

## Test Surface

The tree contains both core-runtime tests and subsystem tests.

| Area | Representative tests |
|---|---|
| PE parsing and symbols | `test_parse.c`, `test_pe32.c`, `test_entry_symbols.c` |
| Loader/imports/relocations | `test_import_resolution.c`, `test_relocations.c`, `test_module_registry.c`, `test_export_parsing.c`, `test_teb_peb.c` |
| Dispatcher and low-level helpers | `test_syscall_dispatch.c`, `test_syscall_safe_utils.c`, `test_exec*.c`, `test_pe_exec*.c` |
| Graphics/audio/backend | `test_ddraw.c`, `test_dsound.c`, `test_sdl2_backend.c` |
| USER32 behavior | `test_user32_handle_ownership.c`, `test_user32_message_dispatch.c`, `test_loadlib_debug.c` |

The `make run-tests` path currently executes 14 compiled test binaries via
`scripts/run_tests.sh`; several additional test sources are ad hoc or targeted
helpers and are not part of that default sweep.

## Architecture Classification

### Wrapper-only

- `src/wrapper_main.c`

### PE32-only

- `src/loader/pe32_entry.c`
- `src/loader/pe32_process.c`
- `src/loader/pe32_process.h`
- `src/loader/pe32_run_guest.S`
- `src/syscall/clone.S`
- `src/syscall/mmap2_asm.S`
- `src/heap/pe32_mmap_heap_backend.c`
- 32-bit sample directories under `samples/*_32/`

### PE32+-only

- `src/main.c`
- `src/run_guest.S`
- `src/syscall/clone64.S`
- `src/heap/pe32plus_musl_malloc_backend.c`
- `src/heap/musl_src/*`
- `src/heap/musl_stubs/*`

### Shared Runtime

- Root PE helpers in `src/`
- Most of `src/loader/`
- Most of `src/syscall/`
- All guest API areas in `src/msvcrt/`
- CRT policy in `src/crt/`
- SDL2 backend in `src/backend/sdl2/`
- Cross-module contracts in `include/`

## Inventory Conclusions

- `src/msvcrt/` is the main architectural pressure point: it owns too many
  unrelated responsibilities and contains both generic Win32 surface and
  DOOM95/sample-specific compatibility.
- `src/loader/pe32_entry.c`, `src/loader/import_table.c`,
  `src/msvcrt/user32_window.c`, `src/msvcrt/ddraw_interface.c`,
  `src/msvcrt/kernel32_misc.c`, and `src/backend/sdl2/rb_event.c` are the most
  obvious extraction candidates by size and mixed responsibility.
- `loader_state.h` and the weak-symbol patterns in backend/stub code show that
  subsystem boundaries still rely heavily on global state rather than narrow
  interfaces.
- The current test surface is strong enough to support staged refactors, but
  some important behavior remains covered indirectly through integration-style
  tests rather than small ownership-focused units.
