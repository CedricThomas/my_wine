# Onboarding

Welcome to my_wine. This guide walks you through the documentation and helps you understand the project structure, so you can start contributing quickly.

---

## What Is my_wine?

my_wine is a minimal user-space PE (Portable Executable) loader for Linux x86_64. It runs mingw-w64-compiled Windows executables without Wine — no virtual machine, no translation layer, just a thin loader that maps the PE image into memory and intercepts the few syscalls the guest needs.

The loader maps the PE at its preferred base address, patches CRT globals (`.refptr` section), resolves imports by wiring them to stub functions, and sets up the TEB/PEB structures expected by Windows x64 code. It then forks a child process and intercepts NT syscalls via a seccomp-BPF filter and `SIGSYS` handler, dispatching them to lightweight Linux-based implementations.

The result: Windows executables compiled with mingw-w64 can run natively on Linux with minimal overhead.

## Quick Start

```bash
make                    # build the loader
make samples            # build sample Windows binaries (requires Docker)
./my_wine samples/hello_world/hello_world.exe   # run a sample
```

For full build instructions, see the [README](../README.md).

---

## Reading Order

The docs below form a learning path. Each step builds on the previous — start with the README to get things building, then learn PE basics before diving into how the loader works.

1. **[README](../README.md)** — Build, run, project structure. Get things compiling first.
2. **[PE Format Primer](pe_format.md)** — Understand PE file structure (headers, sections, imports, RVA). You need this before reading the architecture.
3. **[Rationale](rationale.md)** — Why fork, why seccomp, requirements, limitations. Understand the design choices.
4. **[Architecture](architecture.md)** — How it works: data flow, fork model, syscall interception, TEB/PEB. The deep-dive.
5. **[CRT refptr Patching](refptr.md)** — CRT `.refptr` patching deep-dive. Specialized topic.

```
README.md → pe_format.md → rationale.md → architecture.md → refptr.md
  (build)      (format)        (why)           (how)         (deep-dive)
```

---

## Project Structure

High-level directory layout (not the full tree from README):

| Directory | Description | Start Here |
|---|---|---|
| `src/main.c` | Entry point: orchestrates map, patch, resolve, fork | Read first |
| `src/loader/` | Image mapping, import resolution, TEB/PEB, fork/child setup | `image_mapper.c`, `entry.c` |
| `src/stubs/` | Windows API stub implementations (ntdll, kernel32, msvcrt) | `crt_refptrs.c` |
| `src/syscall/` | Thunk generation, SIGSYS handler, NT syscall dispatcher | `thunk_gen.c`, `dispatcher.c` |
| `src/pe_*.c` | PE format parsing (headers, imports, symbols) | `pe_headers.c` |
| `include/` | Public headers (PE structs, ABI macros, syscall constants) | `pe.h`, `nt_constants.h` |
| `samples/` | Mingw-w64 test programs | `hello_world/` |
| `tests/` | Unit tests | `test_parse.c` |

Two files worth exploring early: `run_guest.S` (naked assembly trampoline) and `syscalls_inline.h` (inline syscall helpers).

For the full tree, see the [README](../README.md) Project Structure section.
