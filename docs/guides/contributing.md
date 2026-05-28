# Contributing Guide

Code standards, source layout, testing conventions, and practical advice for contributing to `my_wine`.

---

## Getting Started

### Clone and Build

```bash
git clone https://github.com/.../my_wine.git
cd my_wine
make
make run-tests
```

`make` compiles `my_wine` (64-bit host), `my_wine32` (32-bit child loader), and `my_wine64` (64-bit child loader). The first build is the gate — if tests pass, the environment is healthy.

### Install Dependencies

```bash
sudo apt install gcc gcc-multilib docker.io pkg-config
```

| Package | Purpose |
|---|---|
| `gcc` | Native 64-bit compiler |
| `gcc-multilib` | 32-bit support (`gcc -m32`) |
| `docker.io` | Cross-compile Windows PE samples via mingw-w64 |
| `pkg-config` | SDL2 / fluidsynth detection |

**Optional — SDL2** (for graphical backends like DOOM95):

```bash
sudo apt install libsdl2-dev libsdl2-dev:i386
```

**Optional — FluidSynth** (for MIDI in DOOM95):

```bash
sudo apt install libfluidsynth-dev libfluidsynth-dev:i386
```

See [build.md](build.md) for full build system reference.

---

## Code Standards

### Compiler Flags

All C files compile under strict flags. There are **no warnings disabled**:

```makefile
CFLAGS = -Wall -Wextra -Werror -O2 -g -I. -Iinclude -MMD -MP -mno-sse
```

| Flag | Rationale |
|---|---|
| `-Wall -Wextra` | Catch every reasonable warning |
| `-Werror` | **All warnings are errors** — no code ships with warnings |
| `-mno-sse` | No SSE instructions — the guest may clobber SSE state; avoid depending on it |
| `-O2 -g` | Optimized builds with debug info |
| `-MMD -MP` | Automatic dependency tracking |

### Special Files

Entry points, loader core, syscall dispatchers, and low-level CRT files use stricter flags:

```makefile
SPECIAL_CFLAGS = $(CFLAGS) -mno-red-zone -fno-stack-protector -fno-exceptions
```

| Flag | Rationale |
|---|---|
| `-mno-red-zone` | x86-64 ABI reserves 128 bytes below %rsp; disabled for code that manages the stack directly |
| `-fno-stack-protector` | Stack canaries conflict with manual stack frame management in entry/loader code |
| `-fno-exceptions` | No C++ exception unwinding tables — these files are pure C with controlled flow |

**Rule of thumb:** If the file touches the stack, manages registers, or sits between the PE image and native syscalls, it gets `SPECIAL_CFLAGS`. The Makefile already applies these to `src/loader/pe32_*.c`, `src/syscall/`, `src/crt/`, and assembly files.

### 32-Bit Child Binary (`my_wine32`)

The 32-bit loader has additional constraints:

```makefile
MY_WINE32_CFLAGS = $(CFLAGS) -DMY_WINE32 -mno-red-zone -fno-stack-protector \
	-fno-exceptions -mno-sse -fno-pie -no-pie -fno-builtin -Werror
```

| Flag | Rationale |
|---|---|
| `-m32` | Compile as 32-bit ELF |
| `-fno-pie -no-pie` | Fixed load address for predictable thunk generation |
| `-fno-builtin` | No builtin function substitutions — every call must go through our stubs |

### Style Conventions

- **No C++** — pure C99
- **K&R-style** function definitions (opening brace on its own line)
- **`#include "..."`** for project headers, `#include <...>` for system headers
- Public headers in `include/`; internal headers co-located with source or in `src/pe_priv.h`
- No raw `malloc`/`free` — use `wine_heap_*` APIs
- No SSE intrinsics in any file compiled with `-mno-sse`

---

## Source Layout

```
my_wine/
├── src/                      # Core loader source
│   ├── main.c                # Host entry point
│   ├── common.c              # Shared utilities (logging, string helpers)
│   ├── debug.c               # Debug infrastructure
│   ├── run_guest.S           # Assembly entry for guest transitions
│   ├── wrapper_main.c        # Wrapper process entry
│   ├── pe_*.c                # PE parsing, imports, relocations, RIP scanning
│   ├── pe_priv.h             # Internal PE types and helpers
│   ├── loader/               # PE loading and execution
│   │   ├── pe32_entry.c      # 32-bit PE entry point
│   │   ├── pe32_process.c    # Process initialization
│   │   ├── pe32_bootstrap.c  # Bootstrap sequence
│   │   ├── pe32_guest_launch.c
│   │   ├── pe32_doom95_*.c   # DOOM95-specific compatibility
│   │   ├── dispatcher.c      # Syscall dispatcher
│   │   ├── thunk_gen.c       # Thunk code generation
│   │   ├── module_list.c     # Module tracking
│   │   ├── image_mapper.c    # mmap-based image mapping
│   │   ├── teb_peb.c         # TEB/PEB construction
│   │   ├── peb_ldr.c         # PEB loader data
│   │   ├── crash_handlers.c  # SEH / signal handlers
│   │   └── ...
│   ├── syscall/              # Syscall dispatcher infrastructure
│   │   ├── dispatcher_generated.c  # Auto-generated from nt_syscalls.def
│   │   ├── safe_utils.c      # Syscall safety utilities
│   │   └── ...
│   ├── heap/                 # Memory allocation
│   │   ├── wine_heap.c       # Wine-compatible heap API
│   │   ├── pe32_mmap_heap_backend.c  # mmap-based allocator for 32-bit
│   │   └── ...
│   ├── crt/                  # C runtime
│   │   ├── crt_globals.c     # Global CRT state
│   │   ├── crt_offset_discovery.c
│   │   ├── crt_refptrs.c     # Reference counting
│   │   └── ...
│   ├── msvcrt/               # API stubs (msvcrt, kernel32, user32, etc.)
│   │   ├── crt_32_stub.c     # 32-bit CRT stub
│   │   ├── kernel32_*.c      # kernel32.dll function stubs
│   │   ├── user32_*.c        # user32.dll function stubs
│   │   ├── msvcrt_*.c        # msvcrt.dll function stubs
│   │   ├── ddraw_*.c         # DirectDraw stubs
│   │   ├── dsound_*.c        # DirectSound stubs
│   │   └── ...
│   └── backend/              # Render backends
│       └── sdl2/             # SDL2-based graphics backend
│           └── ...
├── include/                  # Public headers
│   ├── common.h              # Shared types and declarations
│   ├── pe.h                  # PE format structures
│   ├── pe_parser.h           # PE parsing API
│   ├── ntdll.h               # Nt* syscall declarations
│   ├── wine_abi.h            # Wine ABI types
│   └── ...
├── tests/                    # Unit tests
│   ├── test_parse.c          # PE parsing tests
│   ├── test_import_resolution.c
│   ├── test_syscall_dispatch.c
│   ├── test_teb_peb.c
│   ├── test_handle_manager.c
│   └── ... (27 test files total)
├── samples/                  # End-to-end scenarios
│   ├── hello_world/          # Minimal PE sample
│   ├── doom95/               # DOOM95 game
│   └── ... (43 samples total)
├── scripts/                  # Build/dev tooling
│   ├── unpack_samples.sh     # Unpack game archives
│   └── ...
├── build/                    # 64-bit build output (generated)
├── build32/                  # 32-bit build output (generated)
└── Makefile                  # Single Makefile, auto-discovers .c files
```

### Adding New Stub Files

New API stubs go in `src/msvcrt/` following the naming pattern `<dllname>_<functionname>.c`:

```c
// src/msvcrt/kernel32_CreateFileA.c
#include "pe_priv.h"
#include "kernel32.h"

DECLSPEC_IMPORT BOOL CreateFileA(
    const char *path, unsigned access, unsigned share,
    void *security, unsigned disp, unsigned attrs,
    void *template)
{
    stub_entry("CreateFileA", 7);
    return (void *)0xFFFFFFFF;
}
```

- Use `stub_entry()` for logging
- Return the appropriate sentinel value for unimplemented functions
- The Makefile auto-discovers new `.c` files — no manual edits needed

### Documentation

- Keep docs in `docs/guides/` — they are the developer-facing reference
- Any architectural change should update `docs/` before or with the code
- `docs/` is **stale by default** — always verify against `src/` and `git log`

---

## Testing

### Running Tests

```bash
make tests       # Build all test binaries
make run-tests   # Run all tests
```

The test framework uses plain C with a simple assertion model. Each test binary compiles independently and links against the core library objects.

### Test Files by Subsystem

| Test File | Subsystem |
|---|---|
| `test_parse.c` | PE format parsing |
| `test_import_resolution.c` | Import table resolution |
| `test_export_parsing.c` | Export table parsing |
| `test_relocations.c` | Relocation processing |
| `test_entry_symbols.c` | Entry point symbol lookup |
| `test_syscall_dispatch.c` | Syscall dispatcher |
| `test_syscall_safe_utils.c` | Syscall safety utilities |
| `test_teb_peb.c` | TEB/PEB construction |
| `test_handle_manager.c` | Windows handle management |
| `test_module_registry.c` | Module loading/tracking |
| `test_exec*.c` | PE execution flow |
| `test_pe_exec*.c` | PE execution integration |
| `test_pe32*.c` | 32-bit PE loading |
| `test_ddraw.c`, `test_dsound.c` | DirectDraw / DirectSound |
| `test_user32_*.c` | User32 message handling |
| `test_doom95_paths.c` | DOOM95 path resolution |
| `test_sdl2_backend.c` | SDL2 render backend |
| `test_loadlib_debug.c` | DLL loading debug |

### Adding Tests

Place new tests in `tests/` with the naming convention `test_<subsystem>.c`. The Makefile auto-discovers files matching `test_*.c` in the `tests/` directory.

```c
// tests/test_my_feature.c
#include <stdio.h>
#include <assert.h>
#include "common.h"

static void test_something(void)
{
    assert(my_feature_works() == 1);
    printf("  PASS\n");
}

int main(void)
{
    printf("test_my_feature: ");
    test_something();
    return 0;
}
```

### End-to-End Testing

Use `make samples` to cross-compile sample PE binaries, then run them:

```bash
make samples SAMPLE=hello_world
./my_wine32 samples/hello_world_32/hello_world_32.exe
```

---

## Adding Code

### New Stub Function

1. Create `src/msvcrt/<dllname>_<functionname>.c`
2. Implement with `stub_entry()` logging and appropriate return
3. Run `make` — the file is auto-discovered
4. Run `make run-tests` — ensure nothing breaks

### New DLL Support

1. Add stubs to `src/msvcrt/`
2. Update import resolution if the DLL appears in guest PE import tables
3. Add types to `include/` if the API has structured types

### New Backend

1. Add source to `src/backend/<name>/`
2. Hook into the render backend interface (`include/render_backend.h`)
3. Add corresponding tests to `tests/`

### Documentation Updates

**Always** update `docs/` alongside code changes:
- New subsystems → add to [build.md](build.md) and [debugging.md](debugging.md)
- New samples → update [samples.md](samples.md)
- API changes → update relevant guide or add a new one

---

## Continuous Integration

There is **no CI/CD pipeline**. All testing runs locally:

```bash
make && make run-tests
```

Before submitting changes, run the full build and test suite on the same platform you develop on. If the change affects 32-bit code, verify with `gcc -m32` available. If it touches SDL2 backend code, verify with SDL2 development packages installed.

---

## Commit Conventions

- **Conventional commit style** is encouraged but not enforced:

  ```
  loader: add pe32 relocations pass for IMAGE_REL_I386_DIR32NB
  msvcrt: stub CreateFileA/WriteFile for DOOM95 file I/O
  tests: add test_pe32.c for 32-bit PE image loading
  ```

- Prefix with the subsystem (`loader:`, `syscall:`, `msvcrt:`, `tests:`, `build:`, `docs:`) when applicable
- Keep commits focused on one change

## Common Pitfalls

- **SSE instructions in loader code:** `-mno-sse` is enforced. If the compiler emits SSE, the guest will crash. Use `volatile` or barriers when passing data across the host/guest boundary.
- **Red zone violations:** Files using `SPECIAL_CFLAGS` must never assume the red zone is preserved. Manual stack operations are expected.
- **Missing 32-bit libraries:** `gcc-multilib` is required for `my_wine32`. Without it, the Makefile skips 32-bit targets silently in some configurations — check `build32/` after building.
- **Generated files:** `src/syscall/dispatcher_generated.c` is auto-generated from `include/nt_syscalls.def`. Never edit it directly — update the `.def` file and rebuild.
