# Source Inventory Audit

Generated from `docs/production_tasks/01-audit-and-inventory.md`.

Date: 2026-05-14

## Scope Notes

- This is an inventory only. No code, generated files, docs, or sample outputs were removed.
- `git status --short` was clean before creating this audit.
- The Makefile builds three runtime binaries: `my_wine` wrapper, `my_wine64` for PE32+, and `my_wine32` for PE32.
- Ignored build outputs are present locally, including `build/`, `build32/`, sample `.exe/.dll` outputs, `src/syscall/dispatcher_generated.c`, and `src/loader/import_resolve.d`.
- `audit/architecture-boundaries.md` defines the current ownership layers,
  allowed dependencies, and libc-safe versus syscall-only regions derived from
  this inventory.

## Source Folders And Responsibilities

| Folder | Responsibility | Architecture | Glibc status |
|---|---|---|---|
| `src/` | Root PE parser helpers, wrapper, PE32+ main, guest trampoline, shared debug/common helpers. | Mixed: wrapper/shared plus PE32+ entry. | Host/setup code may use glibc before FS/GS switch; guest transition paths must be audited. |
| `src/loader/` | PE image mapping, imports/exports, DLL loading, TEB/PEB, guest setup, entry handoff, crash handling, PE32 entry. | Mixed PE32, PE32+, shared loader core. | Some files are explicitly glibc-free or post-switch sensitive; others are setup-only. |
| `src/syscall/` | Runtime thunk generation, dispatcher entry, dispatcher argument decoding, inline syscall wrappers, clone/mmap assembly. | Mixed PE32/PE32+ with architecture-specific assembly. | Dispatcher and direct syscall helpers must be glibc-free after guest handoff. |
| `src/msvcrt/` | MSVCRT, kernel32, ntdll, handle, CRT startup, and syscall handler stubs exposed to guest code. | Shared with PE32-specific exceptions. | Guest-facing stubs must not rely on glibc after FS/GS changes unless proven safe for that build. |
| `src/heap/` | Heap backend exposed through Windows heap APIs and CRT allocation stubs. | PE32+ musl oldmalloc; PE32 mmap compatibility allocator. | Guest-callable heap path must avoid glibc allocator/TLS assumptions. |
| `src/crt/` | CRT module abstraction and MinGW/Watcom-specific CRT patching logic. | Shared policy with CRT-specific behavior; PE32/PE32+ aware through header data. | Setup-time code uses glibc; any calls after guest handoff need review. |
| `include/` | Public and semi-public headers for PE, ABI, stubs, dispatcher, constants, render backend. | Mixed. | Header declarations must encode guest ABI and glibc-free constraints where relevant. |
| `tests/` | Native unit/integration tests for parser, loader submodules, dispatcher, PE32 parsing, relocations. | Test-only; mostly host/native. | Glibc allowed. |
| `samples/` | PE programs and metadata used as integration samples. | PE32+ and PE32 sample-only inputs. | Not runtime host code. |
| `scripts/` | Build/test/sample/generation support scripts. | Host tooling. | Glibc not relevant. |
| `docs/` | Project docs and production task plan. | Documentation. | Not runtime code. |
| `examples/` | Small example source. | Sample-only. | Not runtime host code. |

## Runtime Source Inventory

### Root `src/`

| Files | Responsibility | Constraint |
|---|---|---|
| `src/wrapper_main.c` | Thin wrapper that detects PE32 vs PE32+ and `execvp`s `my_wine32` or `my_wine64`. | Shared wrapper; glibc allowed because it runs before backend handoff. |
| `src/main.c` | `my_wine64` PE32+ orchestration: map image, reject PE32, resolve imports, setup CRT/TEB/PEB, enter guest. | PE32+-only entry. Avoid new glibc calls after GS setup and guest handoff. |
| `src/run_guest.S` | PE32+ stack switch and entry trampoline. | PE32+-only; no glibc. |
| `src/trampoline.S` | Assembly trampoline symbol `trampoline_jump`; not referenced by Makefile or code search. | Dead-code candidate until proven used externally. |
| `src/common.c`, `include/common.h` | Shared debug flag and `with_mprotect_rw`; architecture support guard. | Shared; `common.c` currently uses libc/string/mprotect and is in `SPECIAL_CFLAGS`. |
| `include/syscall_safe_utils.h` | Header-only syscall-safe string/memory, bounded copy, path, formatting/debug-write, checked range, and guest pointer write helpers. | Shared PE32/PE32+; no libc calls or out-of-line helper calls in guest-sensitive paths. |
| `src/debug.c`, `include/debug.h` | Shared debug infrastructure/macros. | Shared; `DEBUG` uses `fprintf`, so do not add DEBUG calls in glibc-free guest paths. Runtime traces are controlled by level-based `MY_WINE_DEBUG_LEVEL` values. |
| `src/pe_headers.c`, `src/pe_priv.h`, `include/pe.h`, `include/pe_parser.h` | DOS/NT header parsing, RVA conversion, section helpers, PE structs. | Shared PE32/PE32+; glibc allowed in parser tests/setup unless called post-switch. `src/pe_priv.h` is an implementation/test helper, not a public header. |
| `src/pe_imports.c` | Import descriptor parsing and thunk-size-aware walking. | Shared PE32/PE32+. |
| `src/pe_rip_scan.c` | Scans PE32+ RIP-relative and PE32 absolute import jump patterns. | Shared with arch-specific scan modes. |
| `src/pe_symbols.c` | COFF symbol table parsing from PE files. | Shared setup-time helper; returns malloc-owned data. |

### `src/loader/`

| Files | Responsibility | Constraint |
|---|---|---|
| `src/loader/entry.c` | Calls `setup_guest_and_run()` and exits if guest returns. | PE32+ path; post-handoff sensitive. |
| `src/loader/guest_setup.c`, `src/loader/guest_setup.h` | Guest setup: signal handlers, thunk generation, CRT patches, `.bss` protections, GS base, guest entry. | PE32+ guest setup; must avoid glibc after GS switch except clearly pre-switch diagnostics. |
| `src/loader/pe32_entry.c` | Standalone 32-bit `my_wine32` entry; maps PE32, initializes PEB/TEB, resolves imports, runs 32-bit guest. | PE32-only; avoids glibc TLS after FS/GS sensitive transitions using custom/env/syscall helpers. Large refactor candidate. |
| `src/loader/pe32_run_guest.S` | 32-bit ESP switch and PE32 entry jump. | PE32-only; no glibc. |
| `src/loader/image_mapper.c`, `src/loader/image_mapper.h` | Map PE files, copy sections, set protections, choose PE32 low base. | Shared PE32/PE32+; uses direct syscall wrappers for some operations. |
| `src/loader/relocations.c`, `src/loader/relocations.h` | Apply PE32 and PE32+ base relocations. | Shared; risky parser/memory code. |
| `src/loader/import_table.c`, `src/loader/import_table.h` | Static DLL/function import table, sorting/lookup, writeback helpers. | Shared; PE32 path has local no-libc sort/strcmp logic. Large file. |
| `src/loader/import_init.c`, `src/loader/import_init.h` | Fill dynamic MSVCRT import entries. | Shared setup. |
| `src/loader/import_resolve.c`, `src/loader/import_resolve.h` | IAT resolution passes, thunk scanning, import writes. | Shared and explicitly glibc-free; do not add libc, PLT calls, or DEBUG/fprintf. Large refactor candidate. |
| `src/loader/export_table.c`, `src/loader/export_table.h` | Export cache parsing and name/ordinal lookup. | Shared and documented glibc-free. |
| `src/loader/ordinal_table.c`, `src/loader/ordinal_table.h` | Static ordinal-to-name lookup. | Shared data/helper. |
| `src/loader/dll_loader.c`, `src/loader/dll_loader.h` | DLL map/relocate/register/import resolution. | Shared and documented glibc-free. |
| `src/loader/dll_path.c`, `src/loader/dll_path.h` | DLL path search and cached path handling. | Shared; cached before GS switch per `main.c`. |
| `src/loader/module_list.c`, `src/loader/module_list.h` | Loaded module registry and fixed-size module metadata. | Shared and documented glibc-free. |
| `src/loader/peb_ldr.c`, `src/loader/peb_ldr.h` | PEB loader list state. | Shared; `init_peb_ldr()` uses malloc on host/setup path. |
| `src/loader/teb_peb.c`, `src/loader/teb_peb.h` | TEB/PEB allocation, guest stack allocation, process parameters. | Shared PE32/PE32+; GS/FS sensitive. `remap_stack_below_4gb` is marked unused but is called. |
| `src/loader/gs_base.c`, `src/loader/gs_base.h` | GS base set/get with FSGSBASE fallback. | PE32+ GS-sensitive; no casual libc additions. |
| `src/loader/crash_handlers.c`, `src/loader/crash_handlers.h` | POSIX signal/SEH crash handlers and alternate signal stack. | Shared crash path; signal-safety matters. |
| `src/loader/loader_state.h`, `src/loader/loader_priv.h` | Loader globals and internal includes. | Shared internal API. |

### `src/syscall/`

| Files | Responsibility | Constraint |
|---|---|---|
| `src/syscall/thunk_gen.c`, `include/syscall/thunk_gen.h` | Runtime NT syscall thunk generation and lookup. | Shared PE32/PE32+; uses mmap and diagnostics during setup, thunks are guest-facing. |
| `src/syscall/dispatcher_entry.c`, `include/syscall/dispatcher_entry.h` | Pre-allocated UNIX stack for dispatcher. | Shared; dispatcher-critical and glibc-sensitive after setup. |
| `src/syscall/dispatcher_entry_asm.S` | Assembly dispatcher entry for x86_64 and i386 guest calls. | Shared architecture-specific; no glibc. |
| `src/syscall/dispatcher.c`, `include/syscall/dispatcher.h` | NT syscall dispatch, argument decode, pointer read/writeback. | Shared; must be glibc-free while servicing guest syscalls. Large/risky. |
| `src/syscall/dispatcher_generated.c` | Generated switch bodies included by `dispatcher.c`. | Generated, ignored by git, present locally. |
| `src/syscall/abi_wrappers.c`, `src/syscall/abi_wrappers.h` | SysV wrappers around direct syscalls and memory helpers. | Shared guest-safe support. |
| `src/syscall/syscalls_inline.h` | Inline raw Linux syscall wrappers for 64-bit and 32-bit. | Shared glibc-free primitive. |
| `src/syscall/clone64.S` | x86_64 clone wrapper. | PE32+ host/guest-thread support; no glibc. |
| `src/syscall/clone.S` | i386 clone wrapper. | PE32-only; no glibc. |
| `src/syscall/mmap2_asm.S` | i386 direct `mmap2` wrapper to avoid GS-relative libc syscall path. | PE32-only; no glibc. |

### `src/msvcrt/`

| Files | Responsibility | Constraint |
|---|---|---|
| `src/msvcrt/msvcrt_priv.h`, `include/msvcrt.h` | MSVCRT internal/public declarations and CRT state. | Shared; declarations must preserve ABI split between host and guest. |
| `src/msvcrt/handler_abi.h` | ABI attributes for syscall handlers. | Shared. |
| `src/msvcrt/crt_globals.c` | Global CRT state. | Shared. |
| `src/msvcrt/crt_startup.c` | MinGW CRT startup stubs and selected libc-shadowing wrappers. | PE32+ biased; guest-facing. Current `_m_malloc/_m_free/...` forward to libc and need architecture/path scrutiny before refactor. |
| `src/msvcrt/crt_32_stub.c` | Minimal CRT startup and libc-like stubs for 32-bit and some 64-bit builds. | PE32-required; deliberately avoids TLS/glibc where needed. |
| `src/msvcrt/crt_file.c` | Fake FILE structures for `__iob_func`/`__acrt_iob_func`. | Shared guest-facing. |
| `src/msvcrt/crt_stdio.c` | `fprintf`, `fwrite`, `vfprintf`, stdio stubs using direct writes. | Shared guest-facing; intended glibc-free. |
| `src/msvcrt/crt_stdlib.c` | stdlib stubs and allocation/exit helpers. | Shared guest-facing; verify allocator/exit behavior before cleanup. |
| `src/msvcrt/crt_misc.c` | Locale, errno, locking, conversion, misc CRT stubs. | Shared guest-facing; explicitly must not use glibc after GS switch. |
| `src/msvcrt/crt_refptrs.c` | CRT refptr patch mapping/orchestration. | Shared setup/patching; duplicate mapping entry is intentional per docs. |
| `src/msvcrt/crt_offset_discovery.c` | COFF/text-scan CRT offset discovery with generated fallback. | Shared setup-time; large/risky parser code. |
| `src/msvcrt/handle_manager.c`, `include/handle_manager.h` | Windows handle table and spinlock. | Shared guest-facing; no pthread mutex. |
| `src/msvcrt/kernel32_priv.h`, `include/kernel32.h` | kernel32 internal/public declarations. | Shared ABI-sensitive. |
| `src/msvcrt/kernel32_console.c` | Console `ReadFile`/`WriteFile` stubs. | Shared guest-facing; uses direct syscalls. |
| `src/msvcrt/kernel32_misc.c` | Misc kernel32 APIs: memory protection, paths/env, file helpers, TLS, exceptions, string conversion. | Shared guest-facing; large/risky; comments note no libc in PE32 after GS switch. |
| `src/msvcrt/kernel32_module.c` | Module loading and `GetProcAddress`/`LoadLibrary` style behavior. | Shared guest-facing; imported loader interactions. |
| `src/msvcrt/kernel32_process.c` | Process exit and process-level kernel32 stubs. | Shared guest-facing. |
| `src/msvcrt/kernel32_sync.c` | Event/mutex/critical-section kernel32 stubs. | Shared guest-facing. |
| `src/msvcrt/ntdll_priv.h`, `include/ntdll.h`, `include/nt_constants.h`, `include/nt_syscalls.def` | ntdll declarations, NT constants, syscall definition input. | Shared; `nt_syscalls.def` drives generated dispatcher. |
| `src/msvcrt/ntdll_handle.c` | `NtClose` and handle helpers. | Shared guest-facing. |
| `src/msvcrt/ntdll_io.c` | `NtWriteFile`, `NtReadFile`, `NtOpenFile`. | Shared guest-facing; uses direct syscalls. |
| `src/msvcrt/ntdll_memory.c` | `NtAllocateVirtualMemory`, sections, map/unmap view. | Shared guest-facing; uses direct syscalls/wrappers. |
| `src/msvcrt/ntdll_objects.c` | Event/thread/context object handlers. | Shared guest-facing; clone and direct mmap/munmap. |
| `src/msvcrt/ntdll_process.c` | `NtTerminateProcess`, callback/query process. | Shared guest-facing; cleanup order is thunk/stack sensitive. |
| `src/msvcrt/ntdll_synchronization.c` | NT event/mutex/semaphore wait/set/reset/release. | Shared guest-facing; `find_semaphore` is unused candidate. |
| `src/msvcrt/ntdll_time.c` | Time-related NT handlers. | Shared guest-facing; uses time/syscall helpers. |

### `src/heap/`

| Files | Responsibility | Constraint |
|---|---|---|
| `src/heap/wine_heap.c`, `src/heap/wine_heap.h` | Windows heap API backed by musl malloc or PE32 allocator. | Shared guest-facing; `wine_heap.h` includes pthread but handle locking is custom elsewhere. |
| `src/heap/musl_malloc_wrapper.c` | Integrates musl oldmalloc for 64-bit heap backend. | PE32+-only in Makefile. |
| `src/heap/musl_malloc_32_compat.c` | Minimal mmap-per-allocation allocator for 32-bit builds. | PE32-only. |
| `src/heap/musl_src/aligned_alloc.c`, `src/heap/musl_src/malloc.c`, `src/heap/musl_src/malloc_usable_size.c` | Vendored musl oldmalloc source pieces. | PE32+ heap internals; treat as vendored/generated-adjacent. |
| `src/heap/musl_stubs/atomic.h`, `src/heap/musl_stubs/dynlink.h`, `src/heap/musl_stubs/fork_impl.h`, `src/heap/musl_stubs/libc.h`, `src/heap/musl_stubs/malloc_impl.h`, `src/heap/musl_stubs/pthread_impl.h` | Stub headers required to compile vendored musl code. | PE32+ heap internals. |

### `src/crt/`

| Files | Responsibility | Constraint |
|---|---|---|
| `src/crt/crt.c`, `src/crt/crt_priv.h`, `include/crt.h` | CRT module interface, detection, active module dispatch. | Shared setup layer. |
| `src/crt/crt_mingw.c` | MinGW-w64 CRT detection/refptr/BSS behavior. | Shared setup; glibc allowed before guest switch. |
| `src/crt/crt_watcom.c` | Watcom CRT detection/refptr/BSS behavior for DOOM95. | PE32/sample-driven setup; TODO offsets are copied from MinGW and need tests. |

### Headers Without Dedicated Source

| Files | Responsibility | Constraint |
|---|---|---|
| `include/wine_abi.h` | Guest ABI macros for x86_64 `ms_abi`, i386 cdecl/stdcall, and pointer return workaround. | Architecture-critical. |

## Architecture Classification

### PE32-only files

- `src/loader/pe32_entry.c`
- `src/loader/pe32_run_guest.S`
- `src/syscall/clone.S`
- `src/syscall/mmap2_asm.S`
- `src/heap/musl_malloc_32_compat.c`
- PE32 sample sources: all `samples/*_32/*.c`, `samples/dispatcher_regs_32/dispatcher_regs_32.s`, and `samples/doom95/*`.

### PE32+-only files

- `src/main.c`
- `src/run_guest.S`
- `src/syscall/clone64.S`
- `src/heap/musl_malloc_wrapper.c`
- `src/heap/musl_src/aligned_alloc.c`
- `src/heap/musl_src/malloc.c`
- `src/heap/musl_src/malloc_usable_size.c`
- `src/heap/musl_stubs/atomic.h`
- `src/heap/musl_stubs/dynlink.h`
- `src/heap/musl_stubs/fork_impl.h`
- `src/heap/musl_stubs/libc.h`
- `src/heap/musl_stubs/malloc_impl.h`
- `src/heap/musl_stubs/pthread_impl.h`

### Shared PE32/PE32+ files

- `src/common.c`
- `src/debug.c`
- `src/pe_headers.c`
- `src/pe_imports.c`
- `src/pe_rip_scan.c`
- `src/pe_symbols.c`
- `src/loader/crash_handlers.c`
- `src/loader/dll_loader.c`
- `src/loader/dll_path.c`
- `src/loader/entry.c` (built for PE32+ but calls shared setup)
- `src/loader/export_table.c`
- `src/loader/gs_base.c`
- `src/loader/guest_setup.c`
- `src/loader/image_mapper.c`
- `src/loader/import_init.c`
- `src/loader/import_resolve.c`
- `src/loader/import_table.c`
- `src/loader/module_list.c`
- `src/loader/ordinal_table.c`
- `src/loader/peb_ldr.c`
- `src/loader/relocations.c`
- `src/loader/teb_peb.c`
- `src/syscall/abi_wrappers.c`
- `src/syscall/dispatcher.c`
- `src/syscall/dispatcher_entry.c`
- `src/syscall/dispatcher_entry_asm.S`
- `src/syscall/syscalls_inline.h`
- `src/syscall/thunk_gen.c`
- `src/msvcrt/*.c`
- `src/crt/*.c`
- active public headers in `include/`.

### Wrapper-only

- `src/wrapper_main.c`

### Test-only files

- `tests/test_exec.c`
- `tests/test_exec2.c`
- `tests/test_exec3.c`
- `tests/test_exec4.c`
- `tests/test_exec_same_stack.c`
- `tests/test_export_parsing.c`
- `tests/test_helpers.c`
- `tests/test_helpers.h`
- `tests/test_import_resolution.c`
- `tests/test_loadlib_debug.c`
- `tests/test_module_registry.c`
- `tests/test_parse.c`
- `tests/test_pe32.c`
- `tests/test_pe_exec.c`
- `tests/test_pe_exec2.c`
- `tests/test_pe_exec3.c`
- `tests/test_pe_exec4.c`
- `tests/test_pe_exec5.c`
- `tests/test_pe_exec6.c`
- `tests/test_relocations.c`
- `tests/test_syscall_safe_utils.c`
- `tests/test_syscall_dispatch.c`
- `tests/test_teb_peb.c`

### Sample-only files

- `examples/hello.c`
- `samples/cmdline/cmdline.c`, `samples/cmdline/sample.info`
- `samples/dispatcher_regs_32/dispatcher_regs_32.c`, `samples/dispatcher_regs_32/dispatcher_regs_32.s`, `samples/dispatcher_regs_32/expected_output.txt`, `samples/dispatcher_regs_32/sample.info`
- `samples/dll_loader/dll_loader.c`, `samples/dll_loader/dlls/exportlib.c`, `samples/dll_loader/dlls/exportlib.def`, `samples/dll_loader/expected_output_regex.txt`, `samples/dll_loader/sample.info`
- `samples/dll_loader_32/dll_loader_32.c`, `samples/dll_loader_32/dlls/exportlib.c`, `samples/dll_loader_32/dlls/exportlib.def`, `samples/dll_loader_32/sample.info`
- `samples/doom95/doom95.zip`, `samples/doom95/sample.info`, `samples/doom95/docs/**/*.md`
- `samples/entry_test_32/entry_test_32.c`, `samples/entry_test_32/sample.info`
- `samples/file_io/file_io.c`, `samples/file_io/expected_output_regex.txt`, `samples/file_io/sample.info`
- `samples/file_io_32/file_io_32.c`, `samples/file_io_32/sample.info`
- `samples/heap_test/heap_test.c`, `samples/heap_test/expected_output_regex.txt`, `samples/heap_test/sample.info`
- `samples/heap_test_32/heap_test_32.c`, `samples/heap_test_32/sample.info`
- `samples/hello_world/hello.c`, `samples/hello_world/hello_world.c`, `samples/hello_world/expected_output.txt`, `samples/hello_world/sample.info`
- `samples/hello_world_32/hello_world_32.c`, `samples/hello_world_32/expected_output.txt`, `samples/hello_world_32/sample.info`
- `samples/infinite_loop/infinite_loop.c`, `samples/infinite_loop/sample.info`
- `samples/multi_import_32/multi_import_32.c`, `samples/multi_import_32/expected_output.txt`, `samples/multi_import_32/sample.info`
- `samples/multi_syscall/multi_syscall.c`, `samples/multi_syscall/expected_output.txt`, `samples/multi_syscall/sample.info`
- `samples/multi_syscall_32/multi_syscall_32.c`, `samples/multi_syscall_32/expected_output.txt`, `samples/multi_syscall_32/sample.info`
- `samples/null_deref/null_deref.c`, `samples/null_deref/sample.info`
- `samples/null_deref_32/null_deref_32.c`, `samples/null_deref_32/sample.info`
- `samples/sync_test/sync_test.c`, `samples/sync_test/expected_output_regex.txt`, `samples/sync_test/sample.info`
- `samples/sync_test_32/sync_test_32.c`, `samples/sync_test_32/sample.info`
- `samples/time_test/time_test.c`, `samples/time_test/sample.info`
- `samples/time_test_32/time_test_32.c`, `samples/time_test_32/sample.info`
- `samples/virtual_mem/virtual_mem.c`, `samples/virtual_mem/expected_output_regex.txt`, `samples/virtual_mem/sample.info`
- `samples/virtual_mem_32/virtual_mem_32.c`, `samples/virtual_mem_32/sample.info`

## Generated And Ignored Artifacts

Tracked generator inputs/tools:

- `include/nt_syscalls.def`
- `scripts/gen_dispatcher.py`
- `scripts/gen_crt_offsets.sh`

Generated outputs:

- `src/syscall/dispatcher_generated.c`: generated by `scripts/gen_dispatcher.py --generate` or `make gen`; ignored by git; freshness-checked by `make check-generated`; present locally.
- `include/crt_offsets_generated.h`: generated by `make gen-crt-offsets`; ignored by git; not present in the current tree.

Ignored local build outputs observed:

- `build/*.d`
- `build32/*.d`
- `samples/**/*.exe`
- `samples/**/*.dll`
- `src/loader/import_resolve.d`

Generated-workflow risk:

- `src/syscall/dispatcher_generated.c` is ignored but required before compiling `dispatcher.o`.
- The Makefile has a dependency to generate it, so clean-checkout builds regenerate it automatically; CI can run `make gen check-generated` before tests.
- The stray `src/loader/import_resolve.d` is outside `build/` and should be cleaned by artifact policy, not by source refactors.

## Files That Must Not Call Glibc After FS/GS Switch

High-confidence no-glibc zones:

- `src/syscall/dispatcher_entry_asm.S`
- `src/syscall/dispatcher_entry.c`
- `src/syscall/dispatcher.c`
- `src/syscall/syscalls_inline.h`
- `src/syscall/abi_wrappers.c`
- `src/syscall/clone.S`
- `src/syscall/clone64.S`
- `src/syscall/mmap2_asm.S`
- `src/loader/import_resolve.c`
- `src/loader/dll_loader.c`
- `src/loader/module_list.c`
- `src/loader/export_table.c`
- `src/loader/guest_setup.c` after `set_gs_base()`/guest handoff begins
- `src/loader/pe32_entry.c` after PE32 FS/GS-sensitive setup begins
- `src/loader/pe32_run_guest.S`
- `src/loader/gs_base.c`
- Guest-facing `src/msvcrt/*.c` stubs called from PE imports
- `src/heap/wine_heap.c`
- `src/heap/musl_malloc_wrapper.c`
- `src/heap/musl_malloc_32_compat.c`

Known warning signs:

- `include/debug.h` uses `fprintf` in `DEBUG`; do not use it in files documented as glibc-free. Use syscall-safe writes gated on `g_debug_level` thresholds for guest-sensitive diagnostics.
- `src/msvcrt/crt_startup.c` wrappers `_m_malloc`, `_m_free`, `_m_calloc`, `_m_realloc`, `_m_abort`, and `_m_exit` forward to libc names. Keep tests around PE32+ CRT startup before changing this.
- `src/loader/import_table.c` uses libc sort/strcmp on non-PE32 paths and local no-libc versions under `MY_WINE32`. Preserve this split until architecture boundaries are explicit.
- `src/loader/teb_peb.c` comments note setting FS too early corrupts glibc TLS access.
- `src/loader/pe32_entry.c` has a custom getenv to avoid glibc TLS.

## Duplicate Helpers And Dead-Code Candidates

Do not remove these during the audit. Treat this as a queue for task 08 and task 09.

Duplicate helper candidates:

- Hand-rolled string/memory helpers have an initial shared home in `include/syscall_safe_utils.h`; loader callers use `syscall_safe_*` names directly. Remaining candidates still exist in `src/loader/import_table.c`, `src/msvcrt/kernel32_misc.c`, and `src/loader/pe32_entry.c`.
- Direct stderr/syscall logging helpers for loader DLL resolution and `kernel32_module.c` now use `syscall_safe_debug_write_*`; remaining candidates still exist in `src/msvcrt/crt_stdio.c`, `src/msvcrt/crt_stdlib.c`, and crash/setup paths.
- CRT BSS pre-seeding logic exists in `src/main.c`, `src/crt/crt_mingw.c`, `src/crt/crt_watcom.c`, `src/msvcrt/crt_startup.c`, and `src/loader/pe32_entry.c`.
- PE header parsing/copying is centralized in `src/pe_headers.c`, but partial parsing also exists in `src/loader/guest_setup.c`, `src/loader/pe32_entry.c`, and `src/wrapper_main.c`.
- Architecture-dependent pointer writes in `src/loader/teb_peb.c` now use `syscall_safe_guest_write_ptr`; remaining candidates occur in `src/loader/import_table.c`, `src/msvcrt/crt_refptrs.c`, and `src/loader/pe32_entry.c`.

Dead or stale candidates:

- `src/trampoline.S`: defines `trampoline_jump`, but Makefile does not build it and code search found no references.
- `src/msvcrt/ntdll_synchronization.c`: `find_semaphore` is marked `__attribute__((unused))`.
- `src/crt/crt_watcom.c`: TODOs say Watcom offsets are copied from MinGW and Watcom symbol discovery remains incomplete.

## Risky Files Needing Tests Before Refactor

- `src/loader/pe32_entry.c`: 969 lines; standalone PE32 runtime, FS/GS/TLS-sensitive.
- `src/msvcrt/kernel32_misc.c`: 641 lines; broad guest-facing kernel32 API surface.
- `src/syscall/dispatcher.c`: 446 lines; ABI and pointer writeback critical.
- `src/loader/import_resolve.c`: 441 lines; glibc-free IAT mutation and thunk scanning.
- `src/msvcrt/crt_offset_discovery.c`: 438 lines; PE/COFF parser and fallback offset logic.
- `src/loader/import_table.c`: 430 lines; central import mapping and PE32/PE32+ writeback.
- `src/loader/teb_peb.c`: 452 lines; TEB/PEB/stack layout, low-address PE32 constraints.
- `src/loader/guest_setup.c`: 361 lines; handoff boundary into guest execution.
- `src/msvcrt/crt_32_stub.c`: 348 lines; PE32 CRT compatibility surface.
- `src/loader/relocations.c`: shared malformed-input and architecture-specific relocation logic.
- `src/heap/*`: allocator behavior is guest-visible and easy to regress.

Suggested tests to pin before editing:

- PE32 and PE32+ sample runs for `hello_world`, `file_io`, `heap_test`, `sync_test`, `virtual_mem`, `dll_loader`.
- `make run-tests` plus targeted `test_pe32`, `test_syscall_dispatch`, `test_import_resolution`, `test_relocations`, `test_module_registry`, `test_export_parsing`.
- Generated dispatcher freshness check once task 03 adds it.
- Negative/malformed PE parser tests before task 05 hardening.

## Stale Docs Candidates

- `samples/doom95/docs/reference/risks.md`: claims no PE32 support and recommends parser-only PE32 support, but current code has `my_wine32` and substantial PE32 runtime support.
- `samples/doom95/docs/reference/loader_notes.md`: says no PE32 support in current limitations; stale relative to `src/loader/pe32_entry.c` and `docs/PE32.md`.
- `samples/doom95/docs/*`: many files describe planned SDL2/render/backend work not present in current source; archive or clearly label as DOOM95 planning docs.
- `docs/debug.md`: crash-status examples and kernel notes may be historical; verify against current direct-dispatch and three-binary runtime.
- `README.md`, `docs/onboarding.md`, `docs/architecture.md`, `docs/rationale.md`, `docs/PE32.md`: broadly aligned with current three-binary/direct-dispatch model, but should be refreshed after code cleanup rather than first.

## Cleanup Dependency Map

Recommended adjusted order:

1. Keep task 02, build-system cleanup, before file moves. It must preserve current object groups and generated dependencies.
2. Move task 03, generated-files workflow, before any dispatcher/syscall refactor. `dispatcher_generated.c` is ignored but build-critical.
3. Do task 11 testing/CI earlier than originally listed for risky areas. At minimum, add generated freshness, PE32 sample, PE32+ sample, and malformed parser coverage before large refactors.
4. Do task 06 architecture boundaries before task 08/09. PE32-only, PE32+-only, and glibc-free zones need explicit ownership first.
5. Do task 08 shared utility layer before task 09 duplicate removal. Several duplicates are intentional no-libc variants; centralize only after constraints are encoded.
6. Do task 05 PE bounds hardening before or alongside large parser/loader splits, because parser behavior should be pinned before moving code.
7. Keep task 14 docs refresh late, but add a small interim stale-doc marker for DOOM95 planning docs once task 09 handles stale code.

## Follow-up Task Adjustments

- Task 03 should explicitly inventory `include/crt_offsets_generated.h`, `src/syscall/dispatcher_generated.c`, sample `.exe/.dll`, build `.d` files, and the stray `src/loader/import_resolve.d`.
- Task 06 produced `audit/architecture-boundaries.md`; later tasks should use it
  for ownership, dependency, and libc/syscall-only boundary checks.
- Task 08 should define a guest-safe utility layer separate from host/setup utilities. A single shared string helper layer is not sufficient unless it has no-libc guarantees.
- Task 09 should review `src/trampoline.S` first because it appears disconnected.
- Task 10 should prioritize `src/loader/pe32_entry.c`, `src/msvcrt/kernel32_misc.c`, `src/syscall/dispatcher.c`, `src/loader/import_resolve.c`, and `src/msvcrt/crt_offset_discovery.c`.
- Task 14 should treat `samples/doom95/docs` as planning/reference docs, not current architecture docs, unless they are rewritten against current PE32 support.
