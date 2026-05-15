# Architecture Boundaries

This document defines the current ownership layers in `my_wine`. It is a
working boundary map for refactors: keep behavior stable, preserve the PE32 and
PE32+ splits from `audit/source-inventory.md`, and update the audit whenever a
file move or ownership change changes those classifications.

## Runtime Layers

| Layer | Owner files | Responsibility | Architecture | Libc status |
|---|---|---|---|---|
| Wrapper | `src/wrapper_main.c` | Read enough PE headers to choose `my_wine64` or `my_wine32`, then `execvp` the backend. | Wrapper-only. | Glibc allowed. It exits before backend runtime starts. |
| PE parsing | `src/pe_headers.c`, `src/pe_imports.c`, `src/pe_rip_scan.c`, `src/pe_symbols.c`, `src/pe_priv.h`, `include/pe.h`, `include/pe_parser.h` | Parse PE/COFF headers, imports, sections, symbols, RVA conversion, and thunk scan patterns. | Shared PE32/PE32+. | Glibc allowed for setup/tests unless called from a documented guest-sensitive path. |
| Image mapping and loader core | `src/loader/image_mapper.c`, `relocations.c`, `import_table.c`, `import_init.c`, `import_resolve.c`, `dll_loader.c`, `dll_path.c`, `export_table.c`, `ordinal_table.c`, `module_list.c`, `peb_ldr.c` | Map PE/DLL images, apply relocations, maintain module/export/import state, resolve and patch IAT entries. | Shared PE32/PE32+ with local architecture branches. | Mixed. Import resolution, DLL loading, export lookup, and module registry code are guest-sensitive and must avoid libc in post-switch paths. |
| Guest setup and handoff | `src/main.c`, `src/loader/entry.c`, `guest_setup.c`, `teb_peb.c`, `gs_base.c`, `crash_handlers.c`, `src/run_guest.S` | Orchestrate PE32+ setup, allocate TEB/PEB/stack, install crash/SEH state, set GS, and jump to guest. | PE32+ entry with shared setup helpers. | Glibc allowed before GS is switched; no glibc after GS points at the guest TEB or once guest handoff begins. |
| PE32 runtime entry | `src/loader/pe32_entry.c`, `src/loader/pe32_process.c`, `src/loader/pe32_process.h`, `src/loader/pe32_run_guest.S` | Native 32-bit loader entry, PE32-specific process parameter setup, TEB/PEB setup, FS setup, thunk generation, and 32-bit guest jump. | PE32-only. | Glibc allowed before FS/GS-sensitive setup; use direct syscall/custom helpers after FS points at the guest TEB. |
| Syscall dispatch | `src/syscall/*`, `include/syscall/*`, generated `src/syscall/dispatcher_generated.c` | Generate NT syscall thunks, switch to the UNIX stack, decode guest arguments, dispatch NT handlers, and provide direct Linux syscall wrappers. | Shared with PE32/PE32+ assembly splits. | Syscall-only after guest entry. Do not introduce libc, PLT calls, or `DEBUG` logging in dispatcher-critical paths. |
| Windows API stubs | `src/msvcrt/*`, `include/msvcrt.h`, `include/kernel32.h`, `include/ntdll.h`, `include/nt_constants.h`, `include/nt_syscalls.def` | Implement guest-facing msvcrt, kernel32, and ntdll APIs; bridge Windows handles, memory, synchronization, process, file, and CRT behavior to host primitives. | Shared with PE32-specific CRT exceptions. | Guest-callable code must be syscall-safe. Setup-only helpers may use libc when they are not reachable after segment-base switching. |
| Heap | `src/heap/*`, `src/heap/wine_heap.h` | Provide Windows heap APIs and CRT allocator backing. | Shared API; PE32+ uses musl malloc pieces, PE32 uses mmap-per-allocation compatibility allocator. | Guest-facing allocator path must not depend on glibc allocator or TLS. Treat vendored musl files as heap internals. |
| CRT module policy | `src/crt/*`, `include/crt.h` | Detect CRT flavor and apply CRT-specific setup/refptr/BSS policy. | Shared policy, PE32/PE32+ aware through PE metadata. | Setup layer. Glibc is allowed before guest handoff; review any guest-reachable calls. |
| Shared diagnostics and support | `src/common.c`, `src/debug.c`, `include/common.h`, `include/debug.h`, `include/syscall_safe_utils.h`, `include/wine_abi.h` | Debug flags, protection helpers, syscall-safe leaf utilities, guest ABI macros, and common support definitions. | Shared. | `DEBUG` uses `fprintf`; do not use it from glibc-free paths. Helpers named `syscall_safe_*` are header-only and intended for glibc-free paths. |
| Tests, samples, tools, docs | `tests/`, `samples/`, `examples/`, `scripts/`, `docs/`, `audit/` | Test coverage, sample PE inputs, generated-file tooling, and documentation. | Test-only, sample-only, or host tooling. | Runtime libc restrictions do not apply. |

## Dependency Rules

| From layer | May depend on | Must not depend on |
|---|---|---|
| Wrapper | PE header structs/constants, host libc/syscalls. | Loader, syscall dispatcher, Windows API stubs, heap, CRT module policy. |
| PE parsing | PE structs, common helpers, tests. | Loader mutable state, Windows API stubs, heap, syscall dispatcher. |
| Image mapping and loader core | PE parsing, CRT module policy during setup, syscall-safe helpers, Windows API import/export declarations. | Wrapper, tests/samples, guest dispatcher internals except thunk/import interfaces. |
| Guest setup and handoff | Loader core, PE parsing, syscall thunk/UNIX stack setup, CRT policy, Windows API import table, heap process-heap setup. | Wrapper and test-only code. Post-GS code must not call libc-backed helpers. |
| PE32 runtime entry | Shared PE parsing/loader pieces that compile under `MY_WINE32`, PE32 process helpers, PE32 syscall primitives, PE32 heap backend, guest-facing stubs. | PE32+-only entry/trampoline code, musl malloc backend, 64-bit-only ABI assumptions. |
| Syscall dispatch | Generated syscall switch input, syscall-safe ABI wrappers, guest-facing ntdll handlers, handle/heap helpers where explicitly designed for guest calls. | Wrapper, loader setup-only helpers, libc diagnostics, parser allocation helpers. |
| Windows API stubs | Syscall-safe wrappers, handle manager, heap API, loader module/export interfaces for `LoadLibrary`/`GetProcAddress`. | Wrapper, tests/samples, setup-only libc helpers from guest-callable functions. |
| Heap | Direct mmap/munmap/syscall wrappers, architecture-selected allocator backend. | Windows API stubs above heap policy, glibc malloc/free in guest-facing paths. |
| CRT module policy | PE parsing, loader metadata, CRT refptr APIs. | Syscall dispatcher internals and guest stack/segment switching logic. |
| Shared diagnostics/support | Leaf declarations and helpers only. | Layer-specific state that would make common headers pull in loader, stubs, or dispatcher ownership. |

When a dependency exception is needed, keep it local and document why in the
owning source file. Do not normalize exceptions by adding broad includes to
public headers.

## Libc Boundaries

There are three libc zones:

| Zone | Rule | Typical files |
|---|---|---|
| Host/setup allowed | Normal libc calls are acceptable because Linux TLS still belongs to glibc and guest code is not running. | `src/wrapper_main.c`, PE parsers, most setup portions of `src/main.c`, CRT detection code, tests/tools. |
| Transition-sensitive | Libc may be valid at the start of the function but becomes invalid after FS/GS is switched or guest handoff starts. Keep the transition point obvious and avoid adding calls after it. | `src/loader/guest_setup.c`, `src/loader/teb_peb.c`, `src/loader/gs_base.c`, `src/loader/pe32_entry.c`. |
| Syscall-only | Code can run while guest segment state, guest stack state, or dispatcher state is active. Use direct syscall wrappers or local no-libc helpers only. | `src/syscall/*`, `src/loader/import_resolve.c`, `src/loader/dll_loader.c`, `src/loader/module_list.c`, `src/loader/export_table.c`, guest-facing `src/msvcrt/*.c`, `src/heap/*`. |

Specific warning signs from the audit:

- `include/debug.h` expands `DEBUG` to `fprintf`; it is not allowed in
  syscall-only paths.
- `src/msvcrt/crt_startup.c` still has allocation/exit wrappers that forward
  to libc names. Keep PE32+ CRT startup tests around before changing them.
- `include/syscall_safe_utils.h` owns shared no-libc string/memory,
  formatting/debug-write, checked range, and simple guest pointer write helpers.
  Use those names in guest-sensitive code instead of local duplicates.
- `src/loader/import_table.c` intentionally uses libc sorting/string helpers on
  non-PE32 paths and local no-libc helpers under `MY_WINE32`.
- `src/loader/pe32_entry.c` uses custom environment handling because glibc TLS
  is unsafe after PE32 FS setup.

## Architecture Ownership

Keep PE32-only and PE32+-only responsibilities easy to locate:

| Classification | Files |
|---|---|
| Wrapper-only | `src/wrapper_main.c` |
| PE32-only | `src/loader/pe32_entry.c`, `src/loader/pe32_process.c`, `src/loader/pe32_process.h`, `src/loader/pe32_run_guest.S`, `src/syscall/clone.S`, `src/syscall/mmap2_asm.S`, `src/heap/pe32_mmap_heap_backend.c`, PE32 samples. |
| PE32+-only | `src/main.c`, `src/run_guest.S`, `src/syscall/clone64.S`, `src/heap/pe32plus_musl_malloc_backend.c`, `src/heap/musl_src/*`, `src/heap/musl_stubs/*`. |
| Shared runtime | Root PE helpers, most `src/loader/`, `src/syscall/dispatcher*`, `src/syscall/abi_wrappers.c`, `src/syscall/thunk_gen.c`, guest-facing `src/msvcrt/`, `src/crt/`, and most public headers. |
| Test/sample/tooling | `tests/`, `samples/`, `examples/`, `scripts/`, generated artifacts. |

Shared files are shared intentionally only when they preserve both ABI models:

- Pointer-size behavior must be explicit through PE header data, `MY_WINE32`,
  or helper APIs; do not infer architecture from host pointer size in shared
  parser/loader logic.
- Guest ABI declarations belong in `include/wine_abi.h` and architecture-owned
  syscall/entry assembly, not scattered through unrelated headers.
- PE32-only low-address, `int $0x80`, `mmap2`, and `set_thread_area` behavior
  stays in PE32 entry/syscall/heap ownership.
- PE32+ GS setup, Microsoft x64 calling convention, and 23-byte thunk behavior
  stays in PE32+ entry/setup/syscall ownership.

## Review Checklist

Before moving files or changing includes:

1. Identify the owning layer from the tables above.
2. Check whether the file is PE32-only, PE32+-only, shared, wrapper-only,
   test-only, or sample-only.
3. Check whether the edited function is host/setup allowed,
   transition-sensitive, or syscall-only.
4. Keep guest-facing code free of libc, `DEBUG`, pthread mutexes, and glibc
   allocator dependencies unless the existing path already proves it is setup
   only.
5. If a move changes ownership, classification, generated artifacts, or
   glibc-safety constraints, update `audit/source-inventory.md` in the same
   change.
