# Quickstart

Get up and running with `my_wine` in five minutes. This guide covers building the loader, running sample PE binaries, and understanding known limitations.

---

## Prerequisites

Install the following system packages (Debian/Ubuntu):

```bash
sudo apt install gcc gcc-multilib docker.io
```

| Package | Purpose |
|---|---|
| `gcc` | Native C compiler for building the loader |
| `gcc-multilib` | 32-bit support for `my_wine32` (`gcc -m32`) |
| `docker.io` | Cross-compile Windows PE samples with mingw-w64 |

**Optional — SDL2** (for graphical samples like DOOM95 and draw tests):

```bash
sudo apt install libsdl2-dev
```

SDL2 is not required for console samples or building the loader itself.

---

## Build the Loader

Build the three runtime binaries — `my_wine` (wrapper), `my_wine64` (PE32+), and `my_wine32` (PE32):

```bash
make my_wine my_wine64 my_wine32
```

Build everything including tests and sample binaries (requires Docker):

```bash
make
```

Useful build targets:

```bash
make clean                # remove build artifacts
make tests                # build native test binaries
make run-tests            # build and run the test suite
```

---

## Run a Sample

Build and run a single sample end-to-end:

```bash
make samples SAMPLE=hello_world
make run-samples-scenarios SAMPLE=hello_world
```

Expected output:

```
Hello from Windows!
```

Build all samples at once:

```bash
make samples
```

Run all sample scenarios:

```bash
make run-samples-scenarios
```

**Graphical samples** (e.g. DOOM95, `sdl2_window`) require SDL2 and run under Xvfb:

```bash
make run-samples-scenarios SAMPLE=sdl2_window
```

Capture a screenshot of DOOM95:

```bash
make screenshot-doom95
```

---

## Run a PE Binary Directly

```bash
./my_wine <path_to_pe>
```

The `my_wine` wrapper detects the PE format automatically:

| PE Type | Backend |
|---|---|
| PE32+ (64-bit) | `my_wine64` |
| PE32 (32-bit) | `my_wine32` |

Examples:

```bash
./my_wine samples/hello_world/hello_world.exe
./my_wine samples/hello_world_32/hello_world_32.exe
```

---

## Limitations

`my_wine` is a minimal loader for research and educational use. It is not a general-purpose Windows compatibility layer.

- **Threading** — partial support. No robust per-thread TEB/SEH/TLS model; `NtTerminateThread` is not complete.
- **TLS** — incomplete. `__declspec(thread)` semantics are not fully implemented.
- **No `NtCreateFile`** — only `NtOpenFile` is implemented. `CreateFileA`/`CreateFileW` are not registered.
- **No `NtProtectVirtualMemory`** — `VirtualProtect` uses `mprotect` as a kernel32 stub, but the NT syscall is unregistered.
- **No `NtWaitForMultipleObjects`** — only `NtWaitForSingleObject` is available.
- **Limited Unicode conversion** — `MultiByteToWideChar`/`WideCharToMultiByte` handle only basic ASCII; return `0` for unsupported code pages. `NtOpenFile` handles ASCII only.
- **Narrow API coverage** — the implemented Windows API surface is enough for the included samples, not arbitrary Windows applications.
- **Selective CRT support** — MinGW is the best-supported CRT. Watcom support exists for DOOM95 but is not a general-purpose compatibility layer.

---

## Next Steps

- [README.md](./README.md) — full project overview
- [glossary.md](./glossary.md) — terminology and key concepts
