# my_wine Documentation

my_wine is a minimal user-space PE loader for Linux. It runs PE32+ and PE32 Windows executables natively without Wine — mapping the image into memory, resolving imports to stub implementations, setting up the TEB/PEB environment, and intercepting NT syscalls via dynamically generated thunks.

This directory is the documentation hub. Start at the User Guide if you want to run the loader, consult the Glossary for key terms, or dive into the Developer Guide for understanding and extending the codebase.

## Contents

- **User Guide** — Quickstart, getting the project up and running
- **Glossary** — Key terms used across all docs and source code
- **Developer Guide** — Architecture deep-dives and how-to guides

---

## User Guide

| Document | Description |
|---|---|
| [Quickstart](./quickstart.md) | Build, run a sample, and see it work |

---

## Glossary

| Document | Description |
|---|---|
| [Glossary](./glossary.md) | Terminology used across the codebase and docs |

---

## Developer Guide

### Architecture Deep-Dives

| Document | Description |
|---|---|
| [Architecture Overview](./architecture/overview.md) | Three-binary layout, PE loading pipeline, subsystem overview, data flow, and PE32 vs PE32+ distinctions |
| [Loader Architecture](./architecture/loader.md) | Deep dive into the five stages of the loading pipeline: image mapping, import resolution, TEB/PEB setup, entry point, and PE32 bootstrap |
| [Syscall Dispatcher](./architecture/syscall.md) | NT syscall interception, thunk generation, ABI translation, dispatcher pipeline, and handler implementations |
| [Windows API Stubs](./architecture/stubs.md) | Reference for all ~80 stub implementations across kernel32, user32, ntdll, ddraw, dsound, gdi32, and winmm |
| [Heap Management](./architecture/heap.md) | Heap allocation backends: musl malloc (PE32+) and custom mmap-based allocator (PE32) |
| [CRT Handling](./architecture/crt.md) | C runtime detection and patching for MinGW and Watcom; .refptr patching, BSS offset discovery, entry symbol resolution |
| [SDL2 Backend](./architecture/backend.md) | DOOM95 rendering backend: DirectDraw/DirectSound surfaces, window management, input, event queues, audio mixing, palette support |

### Practical Guides

| Document | Description |
|---|---|
| [Build System Reference](./guides/build.md) | Prerequisites, Makefile targets, compiler flags, three-binary layout, Docker cross-compilation, generated files |
| [Debugging](./guides/debugging.md) | Diagnostic output levels, crash handling, and running under a debugger |
| [Samples](./guides/samples.md) | Catalog of sample programs, scenario system, and how to add new samples |
| [Contributing Guide](./guides/contributing.md) | Code standards, source layout, testing conventions, and practical advice for contributors |

