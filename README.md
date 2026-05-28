# my_wine

A minimal user-space PE loader for Linux. Runs PE32 and PE32+ Windows executables natively — without Wine — by mapping the image into memory, resolving imports to stub implementations, setting up the TEB/PEB environment, and intercepting NT syscalls via dynamically generated thunks.

## What It Does

```
./my_wine samples/hello_world/hello_world.exe
# Hello from Windows!
```

The `my_wine` wrapper auto-detects the PE format:

| PE Type | Backend | Description |
|---|---|---|
| **PE32+** (64-bit) | `my_wine64` | Native x86_64 ELF loader for 64-bit Windows binaries |
| **PE32** (32-bit) | `my_wine32` | Standalone 32-bit ELF loader for 32-bit Windows binaries |

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                    my_wine (wrapper)                         │
│         Detects PE format → execvp(my_wine64|32)             │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│                     my_wine64 / my_wine32                    │
│                                                              │
│  1. Map PE image into memory (image_mapper)                  │
│  2. Resolve imports → stub implementations (pe_imports)      │
│  3. Patch .refptr CRT globals (crt_refptrs)                  │
│  4. Set up TEB/PEB + GS or FS base (teb_peb)                │
│  5. Generate syscall thunks (thunk_gen)                      │
│  6. Jump to entry point on guest stack (run_guest)           │
│                                                              │
│  ┌─────────────┐  ┌──────────────┐  ┌────────────────────┐  │
│  │  Stub APIs   │  │  Syscall     │  │  SDL2 Backend      │  │
│  │  kernel32   │  │  Dispatcher  │  │  (DirectDraw/      │  │
│  │  user32     │  │  ~25 NT      │  │   DirectSound)     │  │
│  │  ntdll      │  │  Handlers    │  │                    │  │
│  │  msvcrt     │  │              │  │                    │  │
│  │  ddraw      │  │              │  │                    │  │
│  │  dsound     │  │              │  │                    │  │
│  │  gdi32      │  │              │  │                    │  │
│  │  winmm      │  │              │  │                    │  │
│  └─────────────┘  └──────────────┘  └────────────────────┘  │
└──────────────────────────────────────────────────────────────┘
```

**Single-process model:** The loader and guest code share the same address space. No forking, no separate preloader.

## Quickstart

### Prerequisites

```bash
# Debian/Ubuntu
sudo apt install gcc gcc-multilib docker.io

# Optional — for graphical samples (DOOM95, draw tests)
sudo apt install libsdl2-dev
```

### Build

```bash
# Build the three runtime binaries
make my_wine my_wine64 my_wine32

# Build everything (requires Docker for cross-compiled samples)
make

# Run the test suite
make run-tests
```

### Run a PE Binary

```bash
./my_wine samples/hello_world/hello_world.exe
./my_wine samples/hello_world_32/hello_world_32.exe

# Run a sample with scenarios
make samples SAMPLE=hello_world
make run-samples-scenarios SAMPLE=hello_world
```

### Run DOOM95

Unpack the bundled archive and run directly:

```bash
./scripts/unpack_samples.sh doom95
./my_wine samples/unpacked/doom95/DOOM95.EXE
```

## Samples

The project ships with ~48 sample programs (both 32-bit and 64-bit variants):

| Sample | Description |
|---|---|
| `hello_world` / `hello_world_32` | Basic console output |
| `file_io` / `file_io_32` | `CreateFile`, `ReadFile`, `WriteFile` |
| `heap_test` / `heap_test_32` | `HeapAlloc`, `HeapFree` |
| `multi_syscall` / `multi_syscall_32` | Multiple NT syscall dispatching |
| `dll_loader` / `dll_loader_32` | `LoadLibrary` / `GetProcAddress` |
| `sdl2_window` / `sdl2_window_32` | SDL2 window creation |
| `ddraw_sample` / `ddraw_sample_32` | DirectDraw rendering |
| `dsound_sample` / `dsound_sample_32` | DirectSound playback |
| `doom95` | Full DOOM95 game (uses SDL2 backend) |

## Project Structure

```
my_wine/
├── Makefile                # Build system (wrapper + PE32+ + PE32 backends)
├── Dockerfile              # Cross-compilation environment for samples
├── include/                # 19 header files (PE format, NT constants, types)
├── src/
│   ├── main.c              # Entry point (PE32+)
│   ├── wrapper_main.c      # Format detection wrapper
│   ├── loader/             # PE image mapping, import resolution, TEB/PEB
│   ├── syscall/            # Syscall dispatcher, thunk generation
│   ├── msvcrt/             # ~80 Windows API stubs (kernel32, user32, ntdll, ddraw, ...)
│   ├── heap/               # musl malloc (PE32+) / mmap allocator (PE32)
│   ├── crt/                # CRT detection and patching (MinGW, Watcom)
│   ├── backend/            # SDL2 rendering (DirectDraw/DirectSound emulation)
│   └── *.S                 # Assembly: run_guest, dispatcher_entry, clone64
├── tests/                  # 33 unit tests
├── samples/                # 48 sample programs (with scenarios)
├── docs/                   # Architecture docs, quickstart, glossary
└── scripts/                # Build and test automation
```

## Limitations

`my_wine` is a **research and educational** project, not a general-purpose Windows compatibility layer.

- **Threading** — partial support; no robust per-thread TEB/SEH/TLS
- **TLS** — `__declspec(thread)` not fully implemented
- **File I/O** — `NtOpenFile` only (no `NtCreateFile`, no `CreateFileA`/`W`)
- **Memory** — no `NtProtectVirtualMemory` syscall (stub via `mprotect`)
- **Synchronization** — `NtWaitForSingleObject` only (no `NtWaitForMultipleObjects`)
- **Unicode** — basic ASCII conversion only; unsupported code pages return `0`
- **API coverage** — enough for the included samples, not for arbitrary Windows applications
- **CRT** — MinGW is best-supported; Watcom support exists for DOOM95

## Documentation

- [Quickstart](docs/quickstart.md) — Build and run in five minutes
- [Glossary](docs/glossary.md) — PE format, Windows runtime, and project terminology
- [Architecture Overview](docs/architecture/overview.md) — Three-binary layout, subsystem design
- [Loader Pipeline](docs/architecture/loader.md) — Image mapping, import resolution, entry point
- [Syscall Dispatcher](docs/architecture/syscall.md) — Thunk generation, ABI translation, handlers
- [Windows API Stubs](docs/architecture/stubs.md) — Reference for all stub implementations
- [Heap Management](docs/architecture/heap.md) — musl vs mmap backends
- [CRT Handling](docs/architecture/crt.md) — MinGW and Watcom patching
- [SDL2 Backend](docs/architecture/backend.md) — DOOM95 rendering and audio
- [Build System](docs/guides/build.md) — Makefile targets, Docker, cross-compilation
- [Debugging](docs/guides/debugging.md) — Diagnostic levels, crash handling
- [Samples Guide](docs/guides/samples.md) — Catalog and scenario system
- [Contributing](docs/guides/contributing.md) — Code standards and conventions
