# my_wine — Minimal PE Loader for Linux x86_64

A minimal user-space PE (Portable Executable) loader that runs
mingw-w64-compiled Windows x86_64 executables on Linux without Wine.

It maps the PE image into memory, resolves imports to our stub
implementations, sets up the Windows TEB/PEB environment, and
jumps to the entry point — intercepting NT syscalls via a
seccomp-filtered `SIGSYS` trampoline.

---

## Getting Started

New to the project? Here's everything you need to get up and running.

### Prerequisites

- **gcc** — the C compiler
- **libseccomp-dev** — provides the seccomp filter library (`-lseccomp`)
- **Docker** — required to cross-compile sample Windows binaries with mingw-w64

Install the system dependencies (Debian/Ubuntu):

```bash
sudo apt install gcc libseccomp-dev docker.io
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
- **libseccomp-dev** — seccomp filter support (`-lseccomp`)
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

### Options

```
./my_wine [--watchdog=N] <pe_binary>
```

- `--watchdog=N` — Set watchdog timeout in seconds (default: 60, range: 1-3600)
  Can also be set via the `MY_WINE_WATCHDOG` environment variable.

The loader will:

1. Open and parse the PE file.
2. Map sections at the preferred image base with correct protections.
3. Patch CRT `.refptr` entries to point at our global stubs.
4. Resolve all imports (ntdll, kernel32, msvcrt) to our stub
   implementations.
5. Set up the TEB (Thread Environment Block), PEB (Process
   Environment Block), and guest stack.
6. Fork a child process, install the syscall interception, and
   jump to the PE entry point.
7. Wait for the child to exit and clean up guest resources.

---

## Architecture Overview

```
┌────────────────────────────────────────────────────────────┐
│                    my_wine (host process)                  │
│                                                            │
│  main()                                                    │
│   │                                                        │
│   ├──► map_image()              # PE file → mmap at base   │
│   ├──► patch_crt_refptrs()      # .refptr → our stubs      │
│   ├──► resolve_imports()        # IAT → our functions      │
│   ├──► setup_teb_peb()          # TEB + PEB + GS base      │
│   ├──► setup_stack()            # guest stack (mmap)       │
│   └──► jump_to_entry()          # fork()                   │
│                        │                                    │
│                        ├──► parent: waitpid() → exit code   │
│                        │                                    │
│                        └──► child:                         │
│                             ├── install signal handlers     │
│                             ├── generate_all_thunks()       │
│                             ├── setup_sigsys_handler()      │
│                             ├── patch __acrt_iob_func       │
│                             └── run_guest() → entry point   │
│                                                      │
│              ┌─────────────────────────────────────┐       │
│              │   Guest PE code runs here           │       │
│              │                                     │       │
│              │   PE code → syscall                 │       │
│              │     │                               │       │
│              │     ├─ syscall < WINE_SYSCALL_OFFSET → Linux OS  │       │
│              │     └─ syscall >= WINE_SYSCALL_OFFSET (0xF000) → SIGSYS   │       │
│              │                          │           │       │
│              │                   sigsys_handler()   │       │
│              │                   │                   │       │
│              │                   ├─ validate thunk   │       │
│              │                   └─ dispatcher()      │       │
│              │                       │               │       │
│              │                   handler_NtXXX()      │       │
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
                  fork()
                   ├── parent: waitpid() + cleanup
                   └── child:  signal handlers → thunks → entry
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
│   │   ├── child_setup.c       # child process initialization
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
│       ├── thunk_gen.c         # runtime syscall thunk generation
│       ├── signal_handler.c    # SIGSYS handler + seccomp filter
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
│       ├── thunk_gen.h         # thunk generation declarations
│       ├── signal_handler.h    # signal handler declarations
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

- **Single-thread SEH** — the Structured Exception Handling chain is
  set up globally; there is no per-thread cleanup or walk support.
- **No relocation support** — the PE must be loadable at its preferred
  image base. If the base is unavailable, a fallback `MAP_STACK` mapping
  is attempted, but relocations are never applied.
- **No dynamic loading** — `LoadLibraryA` is stubbed (returns `NULL`).
  Only the DLLs referenced in the static import table are resolved at
  load time.
- **No TLS support** — thread-local storage is not implemented;
  `TlsGetValue` returns `NULL`. `__dyn_tls_init_callback` is stubbed.
- **Stubbed synchronization primitives** — `InitializeCriticalSection`,
  `EnterCriticalSection`, `LeaveCriticalSection`, and
  `DeleteCriticalSection` are no-op or single-thread stubs. They do
  not provide real mutual exclusion.
- **Only works with mingw-w64 compiled executables** — the loader
  assumes the specific CRT layout and import patterns produced by
  mingw-w64 with GCC. MSVC-compiled binaries or other toolchains may
  not work.

---

## License

This project is provided as-is for educational and research purposes.
