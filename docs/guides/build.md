# Build System Reference

Reference for building `my_wine`: prerequisites, Makefile targets, compiler flags, the three-binary layout, Docker-based cross-compilation, and generated files.

---

## Prerequisites

Install the following system packages (Debian/Ubuntu):

```bash
sudo apt install gcc gcc-multilib docker.io pkg-config
```

| Package | Purpose |
|---|---|
| `gcc` | Native 64-bit C compiler for `my_wine` and `my_wine64` |
| `gcc-multilib` | 32-bit support (`gcc -m32`) for `my_wine32` |
| `docker.io` | Cross-compile Windows PE samples with mingw-w64 |
| `pkg-config` | SDL2 and fluidsynth library detection |

**Optional — SDL2** (for graphical samples and the SDL2 render backend):

```bash
sudo apt install libsdl2-dev libsdl2-dev:i386
```

The 32-bit SDL2 package (`libsdl2-dev:i386`) is required if you want `my_wine32` to include the graphical backend. Without it, `my_wine32` builds without SDL2 support.

**Optional — FluidSynth** (for MIDI support in DOOM95):

```bash
sudo apt install libfluidsynth-dev libfluidsynth-dev:i386
```

---

## Build Targets

| Target | Description |
|---|---|
| `make` / `make all` | Build everything: all three binaries, native tests, and sample PE binaries (requires Docker) |
| `make my_wine` | Build the wrapper binary only |
| `make my_wine64` | Build the PE32+ runtime binary only |
| `make my_wine32` | Build the PE32 runtime binary only |
| `make my_wine my_wine64 my_wine32` | Build the three runtime binaries (no tests, no samples) |
| `make tests` | Build all three binaries, sample .exe files, and all native test binaries |
| `make run-tests` | Build all tests then run the full test suite via `scripts/run_tests.sh` |
| `make debug-tests` | Build all tests then run with `MY_WINE_DEBUG_LEVEL=1` |
| `make clean` | Remove build directories (`build/`, `build32/`) and generated files |
| `make fclean` | Full clean: `clean` plus all end targets (binaries, sample .exe/.dll files, `samples/unpacked/`) |
| `make re` | `fclean` then `make all` (full rebuild from scratch) |
| `make samples` | Cross-compile all sample PE binaries via Docker and unpack registered archives |
| `make samples SAMPLE=hello_world` | Build a single sample PE binary |
| `make graphical-samples` | Build graphical sample PE binaries via Docker |
| `make graphical-samples SAMPLE=doom95` | Build a single graphical sample |
| `make run-samples-scenarios` | Run all sample scenarios through the loader (builds samples first if needed) |
| `make run-samples-scenarios SAMPLE=hello_world` | Run a single sample scenario |
| `make build-docker-image` | Build the `my_wine-samples` Docker image manually |
| `make screenshot-doom95` | Capture a DOOM95 screenshot via `scripts/capture_screenshot.sh` |
| `make gen` / `make gen-dispatcher` | Regenerate `src/syscall/dispatcher_generated.c` from `include/nt_syscalls.def` |
| `make check-generated` | Verify that generated files are up to date |

### Filtered Test Execution

The `run-tests` and `debug-tests` targets forward the `TEST` variable to `scripts/run_tests.sh`:

```bash
make run-tests TEST=test_parse
```

This runs only the specified test binary.

---

## Three Binaries

`my_wine` produces three ELF binaries that work together:

| Binary | PE Support | Build | Entry | Description |
|---|---|---|---|---|
| `my_wine` | Both (detects) | `make my_wine` | `src/wrapper_main.c` | Wrapper: reads a PE file, detects PE32 vs PE32+, then `exec`s the matching backend. Resolves its own directory via `/proc/self/exe` to find siblings. |
| `my_wine64` | PE32+ (64-bit) | `make my_wine64` | `src/entry.c` | Native 64-bit ELF that loads PE32+ images directly. Dynamically linked, linked against `libSDL2` (if available). |
| `my_wine32` | PE32 (32-bit) | `make my_wine32` | `src/loader/pe32_entry.c` | Standalone 32-bit ELF (`-m32`) that loads PE32 images. Dynamically linked with glibc CRT. Linked against 32-bit `libSDL2` (if available). |

### Usage

Users invoke `my_wine` — the wrapper handles format detection:

```bash
./my_wine samples/hello_world/hello_world.exe          # PE32+ → my_wine64
./my_wine samples/hello_world_32/hello_world_32.exe   # PE32 → my_wine32
```

You can also invoke the backends directly:

```bash
./my_wine64 samples/hello_world/hello_world.exe
./my_wine32 samples/hello_world_32/hello_world_32.exe
```

---

## Compiler Flags

The Makefile applies two sets of CFLAGS:

### Default Flags

```
-Wall -Wextra -Werror -O2 -g -I. -Iinclude -MMD -MP -mno-sse
```

- `-mno-sse` — disables SSE instructions (guest code runs without SSE; host entry points must not use SSE registers that overlap with the guest ABI).

### Special Flags

Applied to entry points, loader core, stubs, syscall infrastructure, and heap (`SPECIAL_CFLAGS` in the Makefile):

```
-mno-red-zone -fno-stack-protector -fno-exceptions
```

| Flag | Reason |
|---|---|
| `-mno-red-zone` | The SysV x86-64 ABI reserves a 128-byte "red zone" below `%rsp` that leaf functions may overwrite. Entry points, thunk generators, and syscall dispatchers must not assume the red zone is preserved because they manipulate `%rsp` directly. |
| `-fno-stack-protector` | Stack canary checks trigger false positives when inline assembly switches to a guest stack. Functions that call `rb_call_on_host_stack` or perform stack context switches need this. |
| `-fno-exceptions` | Ensures a consistent ABI across the loader; prevents the compiler from emitting unwinding tables that could confuse raw stack manipulation. |

### Per-file Overrides

The Makefile defines `CFLAGS_<basename>.o` for files with special needs:

- `winmm_doom95.o` — keeps SSE enabled (`-mstackrealign`) for host multimedia library calls
- `backend/sdl2/*.o` — adds `$(SDL2_CFLAGS)` from `pkg-config`
- `pe32plus_musl_malloc_backend.o` — adds musl stub and source include paths

### 32-bit Build

`my_wine32` uses `gcc -m32` with additional flags:

```
-DMY_WINE32 -mno-red-zone -fno-stack-protector -fno-exceptions -mno-sse -fno-pie -no-pie -fno-builtin -Werror
```

The 32-bit build links with glibc CRT (`-no-pie`) and uses a `mmap`-based heap backend instead of musl (musl atomics are x86_64-only).

### SDL2 Detection

SDL2 is detected via `pkg-config`:

```makefile
SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || echo "-I/usr/include/SDL2")
SDL2_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null || echo "-lSDL2")
```

**32-bit SDL2** requires a separate trial link to verify that a 32-bit `libSDL2` library is installed (without `lib32-sdl2`, the linker rejects 64-bit `.so` files when compiling with `-m32`). The variable `SDL2_LIBS_32` is empty when 32-bit SDL2 is unavailable — in that case, `my_wine32` builds without the graphical backend.

---

## Build Directories

| Directory | Contents |
|---|---|
| `build/` | 64-bit object files (`my_wine`, `my_wine64`, test binaries) |
| `build32/` | 32-bit object files (`my_wine32`) |

Object files are organized to mirror source paths (e.g., `src/loader/entry.c` → `build/loader/entry.o`). Auto-generated `.d` header dependency files are included via `-MMD -MP` and `-include` at the bottom of the Makefile.

---

## Docker

### Image

`Dockerfile` defines the `my_wine-samples` image. Based on Ubuntu 24.04, it installs:

- `gcc-mingw-w64-x86-64` / `gcc-mingw-w64-i686` — cross-compiler toolchain for building PE binaries
- `gcc-multilib` — 32-bit support within the container
- `libsdl2-dev` / `libsdl2-dev:i386` — SDL2 headers and libraries (both 64-bit and 32-bit)
- `libfluidsynth-dev` / `libfluidsynth-dev:i386` — MIDI synthesis support
- `xvfb`, `openbox`, `xdotool`, `wmctrl` — headless graphical testing infrastructure
- `wine64` / `wine32` — for reference comparisons
- `gdb`, `gdb-multiarch` — debugging support
- `imagemagick` — screenshot capture

### Building the Image

```bash
make build-docker-image
```

Or via `docker build` directly:

```bash
DOCKER_BUILDKIT=0 docker build -t my_wine-samples .
```

The build disables BuildKit (`DOCKER_BUILDKIT=0`) because `docker build` is invoked from within `docker run` sub-containers during multi-sample builds.

### Sample Cross-Compilation

`scripts/samples.sh` drives PE sample compilation inside the Docker image:

1. Discovers samples from `samples/<name>/` subdirectories
2. Reads `arch=32|64` from `sample.info` to select the mingw-w64 compiler
3. Builds DLLs (if `dlls/*.def` exists) and EXEs (if `*.c` exists)
4. Writes `.exe` and `.dll` output back to the sample directory via volume mount

```bash
make samples                        # build all samples
make samples SAMPLE=hello_world    # build one sample
```

### Graphical Samples

`scripts/graphical_samples.sh` runs graphical samples inside Docker with Xvfb + openbox:

- Each sample gets its own virtual display
- Window interaction is driven by `xdotool` (via `applied_inputs.txt` scripts)
- Samples can emit `HARNESS:` markers to stdout for readiness milestones

```bash
make graphical-samples SAMPLE=doom95
make run-samples-scenarios SAMPLE=doom95
```

---

## Generated Files

### `src/syscall/dispatcher_generated.c`

Auto-generated from `include/nt_syscalls.def` by `scripts/gen_dispatcher.py`.

The `.def` file is the source of truth for NT syscall dispatch. Each block declares:
- Syscall number (e.g. `0x05 NtCallbackReturn`)
- Handler function name
- Optional argument specifications (`ptr(wb)`, `ptr(ro)`, `ptr(wb32)`, `stack`)
- Call template

The generator expands this into C switch-case blocks that `dispatcher.c` includes via `#include "dispatcher_generated.c"`.

```bash
# Regenerate (writes the file)
make gen

# Regenerate directly
python3 scripts/gen_dispatcher.py --generate

# Verify generated file is up to date (used in CI)
make check-generated
python3 scripts/gen_dispatcher.py --check

# Validate .def file structure (no output file written)
python3 scripts/gen_dispatcher.py
```

The generated file is git-ignored but **required** for compilation. The Makefile automatically regenerates it when `include/nt_syscalls.def` or `scripts/gen_dispatcher.py` change (tracked via file timestamps).

---

## Quick Reference

### Common Build Commands

```bash
# Build everything
make

# Build runtimes only (no Docker needed)
make my_wine my_wine64 my_wine32

# Run the test suite
make run-tests

# Run a single sample
make samples SAMPLE=hello_world && make run-samples-scenarios SAMPLE=hello_world

# Clean everything and rebuild
make re

# Debug-mode test run
make debug-tests
```

### Full Workflow

```bash
# 1. Build
make

# 2. Run tests
make run-tests

# 3. Run a sample
make run-samples-scenarios SAMPLE=hello_world

# 4. Run DOOM95 (requires display + SDL2)
./my_wine32 ./samples/unpacked/doom95/DOOM95.EXE

# 5. Run DOOM95 without audio
env SDL_AUDIODRIVER=dummy ./my_wine32 ./samples/unpacked/doom95/DOOM95.EXE
```
