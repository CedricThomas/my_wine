# Architecture Boundaries

Date: 2026-05-26

This document defines the current runtime ownership boundaries in the code as it
exists today. It is not a target architecture. It is a map of what must stay
stable while the project is cleaned up.

## Current Runtime Layers

| Layer | Main files | Responsibility | Hard boundary |
|---|---|---|---|
| Wrapper | `src/wrapper_main.c` | Select `my_wine32` vs `my_wine64` and re-exec. | Must not depend on loader internals or guest API stubs. |
| PE parsing | `src/pe_headers.c`, `src/pe_imports.c`, `src/pe_rip_scan.c`, `src/pe_symbols.c` | Parse PE/COFF structures and helper metadata. | Keep free of loader global state and guest subsystem behavior. |
| Loader core | `src/loader/image_mapper.c`, `relocations.c`, `import_*`, `dll_*`, `module_list.c`, `export_table.c`, `peb_ldr.c` | Map images, relocate, resolve imports/exports, maintain loaded-module state. | Should not absorb USER32/DDRAW/DSOUND policy beyond import/export needs. |
| Guest setup and segment switch | `src/main.c`, `src/loader/entry.c`, `guest_setup.c`, `teb_peb.c`, `gs_base.c`, `crash_handlers.c`, `src/run_guest.S` | Build PE32+ guest execution context and jump into guest code. | After guest selector/base switch, libc usage must stop. |
| PE32 runtime entry | `src/loader/pe32_entry.c`, `pe32_process.c`, `pe32_run_guest.S` | Build PE32-specific execution context, FS setup, and guest launch. | Must stay isolated from PE32+-only assumptions. |
| Syscall dispatcher | `src/syscall/*` | Bridge guest thunks to NT handler implementations. | No libc, stdio, or host-library calls on dispatcher path. |
| Guest NT/CRT/kernel32 APIs | Most of `src/msvcrt/` | Implement guest-facing Windows API behavior. | Guest-callable functions must stay syscall-safe unless they explicitly switch back to host context. |
| Backend bridge | `src/msvcrt/user32_*`, `ddraw_*`, `dsound_*`, `src/backend/sdl2/*` | Translate guest window/input/graphics/audio semantics to SDL2 and host APIs. | Host-library calls must go through explicit host-context boundaries. |
| Heap | `src/heap/*` | Back guest allocations and Win32 heap APIs. | Must remain independent of guest API policy above it. |
| CRT policy | `src/crt/*` | Detect CRT flavor and apply startup/BSS/refptr rules. | Setup-only concern; avoid leaking into generic loader or guest API layers. |
| Sample compatibility | `kernel32_doom95.c`, `gdi32_doom95.c`, `winmm_doom95.c`, launcher stubs, registry/resource helpers | DOOM95- and sample-driven behavior not yet generalized. | Should not become the default home for generic runtime behavior. |

## Dependency Rules

| From | Allowed | Avoid |
|---|---|---|
| Wrapper | Host libc, raw PE header inspection. | Loader core, stubs, backend, heap internals. |
| PE parsing | Common headers, parser-local helpers, tests. | Mutable loader globals, subsystem-specific behavior, SDL2/backend code. |
| Loader core | PE parsing, CRT policy during setup, thunk generation contracts, heap/process setup interfaces. | Backend-specific behavior, sample-specific shims, test-only code. |
| Guest setup | Loader core, CRT policy, heap/process setup, dispatcher setup. | USER32/DDRAW/DSOUND policy except via resolved imports. |
| PE32 runtime entry | Shared parser/loader code that is explicitly `MY_WINE32` safe. | x86_64-only guest setup logic, musl allocator internals, 64-bit calling-convention assumptions. |
| Syscall dispatcher | ABI wrappers, NT handler entrypoints, syscall-safe helpers. | stdio/debug macros that call libc, loader setup helpers, SDL2/backend code. |
| Guest API stubs | Dispatcher-safe helpers, loader export/module queries, handle manager, heap. | Wrapper code, setup-only helpers after guest switch, arbitrary libc allocation/TLS dependencies. |
| Backend bridge | USER32/DDRAW/DSOUND guest code on one side, SDL2 backend on the other. | Direct loader-core mutation, implicit selector switching, guest-visible global state leakage. |
| Heap | Raw mmap/syscall helpers, allocator backend internals. | USER32/kernel32 policy, backend concerns, glibc malloc on guest path. |
| Sample compatibility | Generic guest API declarations and backend contracts where required. | Becoming a dumping ground for unrelated Win32 behavior. |

## Libc Zones

### Host/setup allowed

Normal libc is acceptable because Linux TLS and host stack assumptions still
hold.

Typical files:

- `src/wrapper_main.c`
- most PE parsing code
- setup portions of `src/main.c`
- CRT detection and offset discovery
- tests and scripts

### Transition-sensitive

Libc may be valid at function entry but not after selector/base switching or
guest handoff begins.

Typical files:

- `src/loader/guest_setup.c`
- `src/loader/teb_peb.c`
- `src/loader/gs_base.c`
- `src/loader/pe32_entry.c`
- portions of `src/main.c`

### Syscall-only or explicit host-context only

Guest-sensitive code must either:

- stay entirely no-libc and syscall-safe, or
- switch to host context explicitly before touching SDL2/glibc/pthreads.

Typical files:

- `src/syscall/*`
- `src/loader/import_resolve.c`
- guest-callable `src/msvcrt/ntdll_*.c`
- `src/heap/*`
- SDL2 bridge paths using `rb_call_on_host_stack()` and related helpers

## Boundary Violations Already Present

These are not immediate bugs, but they are current architectural debt and
should guide the cleanup plan.

| Area | Evidence | Why it matters |
|---|---|---|
| `src/msvcrt/` mixes unrelated domains | CRT, kernel32, ntdll, USER32, DDRAW, DSOUND, launcher stubs, registry/resource helpers, and DOOM95 shims all live together. | Refactors become risky because domain boundaries are implicit. |
| `pe32_entry.c` owns too much | File combines PE32 setup, env handling, CRT seeding, entry discovery, DOOM95 argument shaping, selector switch logic, and guest launch. | Hard to test or split PE32 behavior incrementally. |
| Loader/global state is wide | `loader_state.h` centralizes image path/base, module registry, selector state, and architecture flags. | Cross-module coupling encourages hidden dependencies. |
| Backend bridge is partially implicit | Backend code uses weak globals and selector helpers from `rb_sdl2_priv.h`. | Host/guest context boundaries are correct but scattered and hard to enforce. |
| Generic vs sample-specific policy is blurred | `kernel32_doom95.c`, `winmm_doom95.c`, `gdi32_doom95.c`, `crt_watcom.c`, and launcher stubs coexist with generic runtime code. | Future sample work risks contaminating generic runtime layers. |
| Large catch-all files remain | `kernel32_misc.c`, `user32_window.c`, `ddraw_interface.c`, `rb_event.c`, `import_table.c`. | Size reflects mixed responsibility and weak ownership seams. |

## Refactor-Safe Seams

These are the seams that are stable enough to split against without changing
runtime behavior first.

| Seam | Existing anchor |
|---|---|
| PE parsing vs loader | `pe_*` helpers already separate from most loader state. |
| PE32 process setup vs PE32 launch | `pe32_process.c` already started extracting process-parameter setup from `pe32_entry.c`. |
| Generic loader vs sample policy | DOOM95-specific files are named and mostly localized. |
| Guest API layer vs host backend | `render_backend.h`, `rb_*` APIs, and `rb_call_on_host_stack()` already form an explicit bridge. |
| Import/export logic vs stub registry | `import_table.c`, `import_init.c`, `import_resolve.c`, `export_table.c` are separate compilation units already. |
| CRT detection vs runtime execution | `src/crt/*` is already isolated from most guest-callable code. |

## Refactor Rules

1. Do not move code across libc zones without documenting the new safety model.
2. Do not hide PE32-specific behavior inside shared files when it can stay in
   `pe32_*`, `clone.S`, or `mmap2_asm.S`.
3. Do not let DOOM95/sample compatibility become the default home for new
   generic Win32 behavior.
4. Prefer extracting by responsibility first, renaming second, and deeper API
   redesign third.
5. When a move changes ownership or the libc boundary of a file, update
   `audit/source-inventory.md` and `audit/refactor-readiness.md` in the same
   change.
