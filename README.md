# my_wine — Minimal PE Loader for Linux

A minimal user-space PE (Portable Executable) loader that runs
selected PE32+ and PE32 Windows executables on Linux without Wine.

It maps the PE image into memory, resolves imports to our stub
implementations, sets up the Windows TEB/PEB environment, and
jumps to the entry point — intercepting NT syscalls via dynamically
generated thunks that call `__wine_dispatcher`.

---

## Getting Started

New to the project? Here's everything you need to get up and running.

### Prerequisites

- **gcc** — native C compiler.
- **32-bit libc/toolchain support** — required for `my_wine32` (`gcc -m32`).
- **Docker** — required only when rebuilding sample Windows binaries.

Install the system dependencies (Debian/Ubuntu):

```bash
sudo apt install gcc gcc-multilib libc6-dev-i386 docker.io
```

### Build the Loader

```bash
make my_wine my_wine64 my_wine32  # build the wrapper and both backends
make tests                         # build native test binaries
make run-tests                     # build and run the test suite
make clean                         # remove build artifacts
```

`make` / `make all` builds the runtime binaries, tests, and samples. Use the
explicit runtime targets above when Docker is not available.

### Build and Run a Sample

Samples are cross-compiled to PE `.exe` via a Docker container (mingw-w64):

```bash
make samples SAMPLE=hello_world                 # build one e2e sample binary
make run-samples-scenarios SAMPLE=hello_world   # unified console/graphical runner
make run-samples-scenarios SAMPLE=sdl2_window   # graphical samples run in Docker/Xvfb
GRAPHICAL_RUNTIME=wine make run-samples-scenarios SAMPLE=sdl2_window  # run graphical sample under real Wine as reference
```

You should see `Hello from Windows!` printed to the terminal.

### Run a PE Binary Directly

```bash
./my_wine <path_to_pe_binary>
```

For example:

```bash
./my_wine samples/hello_world/hello_world.exe
```

---

## Build

### Targets

| Target | Description |
|---|---|
| `make` or `make all` | Build runtime binaries, native tests, and samples |
| `make my_wine my_wine64 my_wine32` | Build only the wrapper and PE backends |
| `make clean` | Remove the `build/` directory |
| `make tests` | Build native test binaries |
| `make run-tests` | Build and run unit tests |
| `make samples` | Cross-compile all e2e sample binaries via Docker |
| `make samples SAMPLE=foo` | Cross-compile one e2e sample binary |
| `make graphical-samples SAMPLE=foo` | Cross-compile one graphical sample binary |
| `make run-samples-scenarios SAMPLE=foo` | Build and run one sample scenario, dispatching graphical samples to Xvfb |
| `make run-samples-scenarios` | Unified run for console and graphical sample scenarios |

Graphical samples are marked with `type=graphical` in `sample.info`. Add
`applied_inputs.txt` beside the sample to replay deterministic events after the
window appears. Supported commands are `sleep MS`, `focus`, `key KEY`,
`type TEXT`, `click X Y`, `mousemove X Y`, `status LABEL`, and `altf4`.
`altf4` replays the keyboard shortcut and remains the harness default for
scripted shutdown.
Set `GRAPHICAL_RUNTIME=wine` to run the same graphical harness against real
Wine inside the Docker image for reference behavior; the default remains
`GRAPHICAL_RUNTIME=my_wine`. The Wine reference path keeps the same window/input
checks but skips strict geometry assertions, since Wine window-manager sizing
does not match the loader's SDL window sizing exactly.

### Dependencies

- **gcc** — C compiler with 64-bit and 32-bit support.
- **Docker** — sample cross-compilation with mingw-w64.

---

## Usage

```
./my_wine <pe_binary>
```

Example:

```
./my_wine samples/hello_world/hello_world.exe
```

The `my_wine` wrapper detects PE type and dispatches to `my_wine64`
(PE32+) or `my_wine32` (PE32). For PE32+, the loader:

1. Open and parse the PE file.
2. Map sections at the preferred image base with correct protections.
3. Patch CRT `.refptr` entries to point at our global stubs.
4. Resolve all imports (ntdll, kernel32, msvcrt) to our stub
   implementations.
5. Set up the TEB (Thread Environment Block), PEB (Process
   Environment Block), and guest stack.
6. Install the direct dispatch trampoline and jump to the PE
   entry point.
7. Return from the entry point and clean up guest resources.

---

## Architecture Overview

```
┌────────────────────────────────────────────────────────────┐
│   my_wine (wrapper) ──► my_wine64 (PE32+) / my_wine32     │
│   Thin wrapper reads PE headers, dispatches to correct    │
│   backend via execvp(). The diagram below shows the       │
│   PE32+ path (my_wine64).                                 │
│                                                            │
│                     my_wine64 (single process)             │
│                                                            │
│  main()                                                    │
│   │                                                        │
│   ├──► map_image()              # PE file → mmap at base   │
│   ├──► patch_crt_refptrs()      # .refptr → our stubs      │
│   ├──► resolve_imports()        # IAT → our functions      │
│   ├──► setup_teb_peb()          # TEB + PEB + GS base      │
│   ├──► setup_stack()            # guest stack (mmap)       │
│   └──► jump_to_entry()          # stack-switch + jump      │
│                        │                                    │
│                        └──► guest_setup()                   │
│                             ├── patch __acrt_iob_func      │
│                             ├── install dispatcher trampoline │
│                             └── run_guest() → entry point  │
│                                                      │
│              ┌─────────────────────────────────────┐       │
│              │   Guest PE code runs here           │       │
│              │                                     │       │
│              │   PE code → syscall                 │       │
│              │     │                               │       │
│              │     ├─ syscall < 0xF000 → Linux OS  │       │
│              │     └─ syscall >= 0xF000 → __wine_dispatcher │       │
│              │                          │           │       │
│              │                   dispatcher()       │       │
│              │                   │                   │       │
│              │                   └─ handler_NtXXX()  │       │
│              │                       │               │       │
│              │               stub (mprotect, syscall  │       │
│              │                to Linux kernel, etc.)  │       │
│              └─────────────────────────────────────┘       │
└────────────────────────────────────────────────────────────┘
```

### Data Flow

```
PE file ──► mmap(file) ──► parse headers
                  │
                  ▼
          mmap(image_base)  ← preferred base from PE header
                  │
                  ├──► copy section data (from file mmap to image)
                  ├──► set per-section protections (mprotect)
                  └──► unmap file (no longer needed)
                           │
                           ▼
                  patch_crt_refptrs()  ← fix .refptr → our stubs
                           │
                           ▼
                  resolve_imports()    ← IAT entries → our functions
                           │
                           ▼
                  setup_teb_peb()      ← TEB @ GS:0, PEB, GS base
                  setup_stack()        ← guest stack (downward)
                           │
                           ▼
                  guest_setup()  ← single-process, stack-switch
                   ├── patch __acrt_iob_func
                   └── jump to entry point via dispatcher trampoline
```

See [docs/architecture.md](docs/architecture.md) for a detailed
architectural walkthrough.

---

## Project Structure

```
├── Makefile                    # build system
├── src/
│   ├── wrapper_main.c          # my_wine wrapper: detect PE32 vs PE32+, execvp backend
│   ├── main.c                  # my_wine64: PE32+ entry point — map, resolve, run
│   ├── common.c                # shared utilities
│   ├── pe_headers.c            # PE DOS/NT header parsing
│   ├── pe_imports.c            # import descriptor chain traversal
│   ├── pe_symbols.c            # COFF symbol table parsing
│   ├── pe_rip_scan.c           # RIP-relative thunk scanning
│   ├── pe_priv.h               # internal PE parser declarations
│   ├── run_guest.S             # naked assembly trampoline
│   ├── loader/
│   │   ├── image_mapper.c      # mmap image, copy sections, mprotect
│   │   ├── import_table.c      # stub function registration table
│   │   ├── import_resolve.c    # IAT resolution (pass 1 + pass 2)
│   │   ├── import_init.c       # import resolution orchestrator
│   │   ├── teb_peb.c           # TEB + PEB allocation and setup
│   │   ├── entry.c             # call setup_guest_and_run() → jump to PE entry point
│   │   ├── pe32_entry.c        # my_wine32: PE32 entry point (32-bit, glibc CRT + FS→TEB)
│   │   ├── pe32_process.c      # PE32 process parameters, PEB wiring, CRT BSS setup
│   │   ├── pe32_run_guest.S    # PE32 stack switch and entry jump
│   │   ├── dll_loader.c        # LoadLibraryA/FreeLibraryA DLL mapping support
│   │   ├── export_table.c      # PE export cache and lookup
│   │   ├── relocations.c       # PE32/PE32+ base relocations
│   │   ├── guest_setup.c       # guest process initialization
│   │   ├── crash_handlers.c    # POSIX signal + SEH crash handling
│   │   ├── gs_base.c           # GS segment base setup via arch_prctl
│   │   └── loader_priv.h       # internal loader declarations
│   ├── msvcrt/                 # Windows API stub implementations
│   │   ├── ntdll_*.c           # ntdll handlers (handle, io, memory,
│   │   │                        #   process, objects)
│   │   ├── kernel32_*.c        # kernel32 stubs (console, process,
│   │   │                        #   module, misc)
│   │   ├── crt_*.c             # CRT globals, stdio, stdlib, file I/O,
│   │   │                        #   startup, refptrs, offset discovery
│   │   ├── handler_abi.h       # handler calling convention macros
│   │   ├── ntdll_priv.h        # private ntdll declarations
│   │   ├── kernel32_priv.h     # private kernel32 declarations
│   │   └── msvcrt_priv.h       # private msvcrt declarations
│   ├── crt/                    # CRT flavor detection and patch policy
│   ├── heap/                   # guest heap backends
│   └── syscall/
│       ├── dispatcher_entry_asm.S # assembly entry into dispatcher
│       ├── dispatcher_entry.c  # dispatcher entry glue
│       ├── dispatcher.c        # NT syscall number → handler dispatch
│       ├── thunk_gen.c         # PE32+/PE32 syscall thunk generation
│       └── syscalls_inline.h   # inline syscall helpers
├── include/                    # public headers
│   ├── pe.h                    # PE format structures (IMAGE_*)
│   ├── pe_parser.h             # header parsing declarations
│   ├── ntdll.h                 # ntdll API declarations
│   ├── kernel32.h              # kernel32 API declarations
│   ├── msvcrt.h                # msvcrt API declarations
│   ├── nt_constants.h          # NT syscall numbers, TEB/PEB offsets,
│   │                           # Wine syscall offset as named constants
│   ├── wine_abi.h              # WINE_STUB / WINE_STUB_STATIC macros
│   ├── common.h                # shared utility declarations
│   ├── syscall_safe_utils.h    # no-libc helper functions for guest paths
│   └── syscall/
│       ├── dispatcher_entry.h  # dispatcher entry declarations
│       └── dispatcher.h        # dispatcher function type
├── tests/                      # unit/integration tests
│   ├── test_parse.c            # PE header parsing tests
│   ├── test_import_resolution.c # import resolution tests
│   ├── test_teb_peb.c          # TEB/PEB setup tests
│   └── test_syscall_dispatch.c # syscall dispatch tests
├── samples/                    # sample Windows programs
│   ├── hello_world/
│   │   └── hello_world.c       # minimal PE32+ target
│   ├── hello_world_32/
│   │   └── hello_world_32.c    # minimal PE32 target
│   └── doom95/                 # DOOM95 sample input and planning docs
└── docs/
    ├── architecture.md         # architecture diagrams & data flow
    ├── onboarding.md           # contributor reading path and workflow
    ├── PE32.md                 # PE32 backend notes
    ├── debug.md                # runtime diagnostics and debugging commands
    └── refptr.md               # .refptr patching deep-dive
```

---

## Known Limitations

- **Threading is limited** — PE32+ thread support is still partial. There is
  no robust per-thread TEB/SEH/TLS model and `NtTerminateThread` is not
  complete.
  `NtGetContextThread`/`NtSetContextThread` are stubs.
- **TLS support is incomplete** — basic TLS APIs exist, but Windows TLS and
  `__declspec(thread)` semantics are not complete.
- **No `NtCreateFile`** — only `NtOpenFile` is implemented.
  `CreateFileA`/`CreateFileW` are not registered.
- **No `NtTerminateThread`** — threads exit via `INLINE_SYSCALL_EXIT(0)`
  which kills the entire process.
- **No `NtProtectVirtualMemory`** — `VirtualProtect` is a kernel32 stub
  using `mprotect`, but the NT syscall is unregistered.
- **No `NtWaitForMultipleObjects`** — only `NtWaitForSingleObject` is
  implemented.
- **No `NtQueryAttributesFile`** — no file attribute queries.
- **No Unicode conversion** — `MultiByteToWideChar`/`WideCharToMultiByte`
  return `0`. `NtOpenFile` handles ASCII only.
- **Windows API coverage is narrow** — the implemented surface is enough for
  current samples, not arbitrary Windows applications.
- **CRT coverage is selective** — MinGW is the best-supported CRT. Watcom
  support exists for DOOM95-oriented setup work but is not a general-purpose
  compatibility layer.

### Capabilities

PE loading (preferred base + fallback mapping), base relocations (PE32 and
PE32+), import resolution (Pass 1 IAT + Pass 2 thunk scanning), dynamic loading
(`LoadLibraryA`/`FreeLibraryA`), export table parsing + lookup,
module registry + PEB LDR, TEB/PEB + GS base, heap management (musl malloc
backend with `HeapCreate/Alloc/Free/ReAlloc/Destroy/Size/GetProcessHeap`),
synchronization (CRITICAL_SECTION, Events, Mutexes), thread creation
(`NtCreateThreadEx` via `clone()`), 25 NT syscall handlers (auto-generated
from `nt_syscalls.def`), file I/O (`NtOpenFile`, `NtReadFile`, `NtWriteFile`),
virtual memory (`NtAllocate/FreeVirtualMemory`, `NtCreateSection`,
`NtMapViewOfSection`), time (`NtQuerySystemTime`,
`NtQueryPerformanceCounter/Frequency`, `NtDelayExecution`), CRT startup
(`__getmainargs`, `_initterm`, `__iob_func`, `__acrt_iob_func`),
ordinal imports (ntdll/kernel32/msvcrt), SEH + POSIX signal crash handlers.

---

## License

This project is provided as-is for educational and research purposes.
