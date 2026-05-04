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

---

## Tracing a PE Execution

Step-by-step source code walkthrough showing the execution flow.

**Parent process (before fork):**

1. **`main()` in `src/main.c`** — Entry point. Opens the PE file, orchestrates the loading process.
2. **`map_image()` in `src/loader/image_mapper.c`** — Opens the PE file, maps it read-only, parses DOS/NT headers, maps the image at the preferred base, copies section data, sets per-section protections via `mprotect`.
3. **`patch_crt_refptrs()` in `src/stubs/crt_refptrs.c`** — Fixes CRT `.refptr` pointers to point at our Linux-side stubs.
4. **`resolve_imports()` in `src/loader/import_resolve.c`** — Walks the import descriptor chain and patches IAT entries to point at our stub functions.
5. **`setup_teb_peb()` in `src/loader/teb_peb.c`** — Allocates and populates the TEB and PEB, sets the GS base via `arch_prctl(ARCH_SET_GS)`.
6. **`jump_to_entry()` in `src/loader/entry.c`** — Calls `fork()`.

**Child process (after fork):**

1. **`child_setup.c`** — Child-specific initialization: installs signal handlers, applies seccomp filter.
2. **`thunk_gen.c`** — Generates syscall thunks in PROT_EXEC pages.
3. **`signal_handler.c`** — Sets up the SIGSYS handler that validates thunk addresses and dispatches to handlers.
4. **`run_guest.S`** — Naked assembly trampoline that switches to the guest stack and jumps to the PE entry point. No prologue/epilogue.

**Syscall interception (when guest calls NT syscall):**

1. Guest code → thunk (`mov r10,rcx; mov eax,NR+0xF000; syscall; ret`)
2. `syscall >= 0xF000` → seccomp TRAP → SIGSYS delivered
3. `sigsys_handler()` validates thunk address (±4096 of registered thunks)
4. **`dispatcher.c`** — Looks up the syscall number, dispatches to the appropriate handler function

For the full flow diagram, see [Architecture](architecture.md) §1.

---

## Contributing Tips

Practical advice for new contributors:

- **Build a sample** with `make samples SAMPLE=hello_world` to get a test binary.
- **Run with** `./my_wine samples/hello_world/hello_world.exe` to see it working.
- **Add a new stub:** Create `src/stubs/ntdll_new.c` with a `WINE_STUB` function (defined in `include/wine_abi.h`), register it in `src/loader/import_table.c`, and add a handler case in `src/syscall/dispatcher.c`.
- **Note:** All stubs use `ms_abi` (Windows x64 calling convention: RCX, RDX, R8, R9 for the first four args), not the Linux System V ABI.
- **Run tests** with `make test` after any changes.
- **Debug tip:** Start with `make samples SAMPLE=hello_world` as your test case — it's the simplest PE and exercises the core flow.

---

## Related Documents

- [README](../README.md) — Build, run, project structure
- [PE Format Primer](pe_format.md) — PE format primer
- [Rationale](rationale.md) — Design decisions, requirements, limitations
- [Architecture](architecture.md) — Deep-dive architecture
- [CRT refptr Patching](refptr.md) — CRT .refptr patching deep-dive
