# Onboarding

Welcome to my_wine. This guide walks you through the docs and helps you
understand the project so you can start contributing.

---

## 1. What Is my_wine?

my_wine is a minimal user-space PE loader for Linux x86_64. It runs
mingw-w64-compiled Windows executables without Wine. It maps the PE
image into memory, patches CRT globals, resolves imports to stub
implementations, sets up the Windows TEB/PEB environment, and jumps to
the entry point. NT syscalls are intercepted via a seccomp-filtered
SIGSYS trampoline.

The key differentiator: no Wine, no virtual machine — just mmap + fork
+ seccomp.

---

## 2. Quick Start

```bash
make                        # build the loader
make samples                # build sample PE binaries (requires Docker)
./my_wine samples/hello_world/hello_world.exe   # run a sample
```

See [README](../README.md) for prerequisites and full build instructions.
