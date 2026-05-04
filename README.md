# my_wine — Minimal PE Loader for Linux x86_64

A minimal user-space PE (Portable Executable) loader that runs
mingw-w64-compiled Windows x86_64 executables on Linux without Wine.

It maps the PE image into memory, resolves imports to our stub
implementations, sets up the Windows TEB/PEB environment, and
jumps to the entry point — intercepting NT syscalls via a
seccomp-filtered `SIGSYS` trampoline.

---

## Build

```
make              # build the my_wine binary
make clean        # remove build artifacts
make test         # build and run the test suite
```

Dependencies: `gcc`, `libseccomp-dev` (for `libseccomp`).

To compile a test Windows binary (requires `x86_64-w64-mingw32-gcc`):

```
make hello.exe
```

## Usage

```
./my_wine <pe_binary>
```

Example:

```
./my_wine examples/hello.exe
```

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

## Project Structure

```
├── Makefile                    # build system
├── src/
│   ├── main.c                  # orchestrator: map, resolve, run
│   ├── pe_parser.c             # PE header parsing (DOS/NT/sections)
│   ├── run_guest.S             # naked assembly trampoline to entry
│   ├── loader/
│   │   ├── image_mapper.c      # mmap image, copy sections, mprotect
│   │   ├── import_resolver.c   # resolve IAT (pass 1 + pass 2 thunk patch)
│   │   ├── teb_peb.c           # TEB, PEB, guest stack allocation
│   │   ├── entry.c             # fork(), child setup, __acrt_iob patch
│   │   └── loader_priv.h       # internal loader declarations
│   ├── stubs/
│   │   ├── kernel32.c          # kernel32.dll stubs (GetStdHandle, etc.)
│   │   ├── ntdll_*.c           # ntdll.dll syscall handlers (split by
│   │   │                        #   category: handle, io, memory, process)
│   │   ├── crt_globals.c       # CRT global variables (argv, envp, etc.)
│   │   ├── crt_file.c          # wine_FILE / __wine_iob implementation
│   │   ├── crt_stdio.c         # fprintf, fwrite, vfprintf stubs
│   │   ├── crt_stdlib.c        # malloc, free, exit, strlen, etc. stubs
│   │   ├── crt_startup.c       # __getmainargs, __initenv, _initterm
│   │   ├── crt_refptrs.c       # .refptr section patching
│   │   └── msvcrt_priv.h       # private CRT declarations
│   └── syscall/
│       ├── thunk_gen.c         # generate syscall thunks (mov r10,rcx;
│       │                        #   mov eax,NR; syscall; ret)
│       ├── signal_handler.c    # SIGSYS handler + seccomp filter setup
│       └── dispatcher.c        # NT syscall dispatcher (switch on NR)
├── include/
│   ├── pe.h                    # PE format structures (IMAGE_*)
│   ├── pe_parser.h             # header parsing declarations
│   ├── ntdll.h                 # ntdll API declarations
│   ├── kernel32.h              # kernel32 API declarations
│   ├── msvcrt.h                # msvcrt API declarations
│   ├── wine_abi.h              # WINE_STUB / WINE_STUB_STATIC macros
│   ├── nt_constants.h          # NT syscall numbers, TEB/PEB offsets,
│   │                           # Wine syscall offset as named constants
│   └── syscall/
│       ├── thunk_gen.h         # thunk generation declarations
│       ├── signal_handler.h    # signal handler declarations
│       └── dispatcher.h        # dispatcher function type
├── tests/
│   ├── test_parse.c            # PE header parsing tests
│   ├── test_import_resolution.c # import resolution tests
│   ├── test_teb_peb.c          # TEB/PEB setup tests
│   └── test_syscall_dispatch.c # syscall dispatch tests
├── docs/
│   ├── architecture.md         # architecture diagrams & data flow
│   └── refptr.md               # .refptr patching deep-dive
├── examples/
│   └── hello.c                 # sample Windows hello-world program
└── scripts/
    └── build_test.sh           # compile examples/hello.c with mingw
```

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
- **CRT refptr patching** — argc/argv/envp offsets are discovered
  from the PE's COFF symbol table. A hardcoded fallback
  (0x018/0x020/0x028) is used when the symbol table is absent or
  stripped. Different CRT versions may require updated fallback
  offsets.

## License

This project is provided as-is for educational and research purposes.
