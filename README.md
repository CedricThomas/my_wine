# my_wine — Minimal PE Loader for Linux x86_64

A minimal user-space PE (Portable Executable) loader that runs
mingw-w64-compiled Windows x86_64 executables on Linux without Wine.

It maps the PE image into memory, resolves imports to our stub
implementations, sets up the Windows TEB/PEB environment, and
jumps to the entry point — intercepting NT syscalls via dynamically
generated thunks that call `__wine_dispatcher`.

---

## Getting Started

New to the project? Here's everything you need to get up and running.

### Prerequisites

- **gcc** — the C compiler
- **Docker** — required to cross-compile sample Windows binaries with mingw-w64

Install the system dependencies (Debian/Ubuntu):

```bash
sudo apt install gcc docker.io
```

### Build the Loader

```bash
make           # compile the my_wine binary
make clean     # remove build artifacts
make test      # build and run the test suite
```

### Build and Run a Sample

Samples are cross-compiled to PE `.exe` via a Docker container (mingw-w64):

```bash
make samples                  # build all samples
make samples SAMPLE=hello_world # build one sample
make run-sample SAMPLE=hello_world  # build + run under ./my_wine
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
| `make` or `make all` | Build the `my_wine` binary |
| `make clean` | Remove the `build/` directory |
| `make test` | Build and run unit tests |
| `make samples` | Cross-compile all samples via Docker |
| `make samples SAMPLE=foo` | Cross-compile one sample |
| `make run-sample SAMPLE=foo` | Build sample + run it under `./my_wine` |

### Dependencies

- **gcc** — C compiler
- **Docker** — cross-compilation with `x86_64-w64-mingw32-gcc`

---

## Usage

```
./my_wine <pe_binary>
```

Example:

```
./my_wine samples/hello_world/hello_world.exe
```

The loader will:

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
│                     my_wine (single process)               │
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
│   ├── main.c                  # entry point: map, resolve, run
│   ├── common.c                # shared utilities
│   ├── pe_headers.c            # PE DOS/NT header parsing
│   ├── pe_imports.c            # import descriptor chain traversal
│   ├── pe_symbols.c            # COFF symbol table parsing
│   ├── pe_rip_scan.c           # RIP-relative thunk scanning
│   ├── pe_priv.h               # internal PE parser declarations
│   ├── run_guest.S             # naked assembly trampoline
│   ├── syscalls_inline.h       # inline syscall helpers
│   ├── loader/
│   │   ├── image_mapper.c      # mmap image, copy sections, mprotect
│   │   ├── import_table.c      # stub function registration table
│   │   ├── import_resolve.c    # IAT resolution (pass 1 + pass 2)
│   │   ├── import_init.c       # import resolution orchestrator
│   │   ├── teb_peb.c           # TEB + PEB allocation and setup
│   │   ├── entry.c             # fork(), child setup, __acrt_iob patch
│   │   ├── guest_setup.c       # guest process initialization
│   │   ├── crash_handlers.c    # exception/crash handling in child
│   │   ├── gs_base.c           # GS segment base setup via arch_prctl
│   │   └── loader_priv.h       # internal loader declarations
│   ├── stubs/                  # Windows API stub implementations
│   │   ├── ntdll_*.c           # ntdll handlers (handle, io, memory,
│   │   │                        #   process, objects)
│   │   ├── kernel32_*.c        # kernel32 stubs (console, process,
│   │   │                        #   module, misc)
│   │   ├── crt_*.c             # CRT globals, stdio, stdlib, file I/O,
│   │   │                        #   startup, refptrs, offset discovery
│   │   ├── abi_wrappers.c      # ABI compatibility wrappers
│   │   ├── handler_abi.h       # handler calling convention macros
│   │   ├── ntdll_priv.h        # private ntdll declarations
│   │   ├── kernel32_priv.h     # private kernel32 declarations
│   │   └── msvcrt_priv.h       # private msvcrt declarations
│   └── syscall/
│       ├── dispatcher_entry.S  # assembly entry into dispatcher
│       ├── dispatcher_entry.c  # dispatcher entry glue
│       └── dispatcher.c        # NT syscall number → handler dispatch
├── include/                    # public headers
│   ├── pe.h                    # PE format structures (IMAGE_*)
│   ├── pe_parser.h             # header parsing declarations
│   ├── ntdll.h                 # ntdll API declarations
│   ├── kernel32.h              # kernel32 API declarations
│   ├── msvcrt.h                # msvcrt API declarations
│   ├── nt_constants.h          # NT syscall numbers, TEB/PEB offsets,
│   │                           # Wine syscall offset as named constants
│   ├── wine_abi.h              # WINE_STUB / WINE_STUB_STATIC macros
│   ├── abi_wrappers.h          # ABI wrapper declarations
│   ├── common.h                # shared utility declarations
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
│   │   └── hello.c             # minimal hello-world PE target
│   ├── Dockerfile              # mingw-w64 cross-compile container
│   └── samples.sh              # build/run samples via Docker
└── docs/
    ├── architecture.md         # architecture diagrams & data flow
    └── refptr.md               # .refptr patching deep-dive
```

---

## Known Limitations

- **Shared-TEB threading model** — all threads share the same TEB and GS
  base. No per-thread SEH, no per-thread TLS, no `NtTerminateThread`.
  `NtGetContextThread`/`NtSetContextThread` are stubs.
- **No TLS support** — `TlsGetValue` returns `NULL`;
  `__declspec(thread)` is not supported.
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
- **Missing kernel32 stubs** — `CreateFileA/W`, `CloseHandle`,
  `GetTickCount`, `GetModuleFileNameA/W`, `GetFileAttributesA/W`,
  `GetStartupInfoW`, `GetModuleHandleW`, `SetUnhandledExceptionFilter`
  are not registered.
- **Only mingw-w64 executables** — the loader assumes the specific CRT
  layout and import patterns produced by mingw-w64 with GCC.

### Capabilities

PE loading (preferred base + MAP_STACK fallback), base relocations (DIR64),
import resolution (Pass 1 IAT + Pass 2 thunk scanning), dynamic loading
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
