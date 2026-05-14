# Build System Cleanup

## Goal

Reorganize the Makefile into a logical, maintainable structure without changing
build behavior.

## Why

The Makefile currently mixes toolchain setup, object discovery, target rules,
tests, samples, generated files, and cleanup. Production work needs predictable
targets and a clear separation between local build, tests, samples, and release
artifacts.

## Scope

- Reorder sections in the Makefile.
- Clarify target names and comments.
- Separate default build from sample generation if needed.
- Keep existing command behavior stable unless changing it is deliberate and
  documented.

## Audit Inputs

Use `audit/source-inventory.md` to preserve the current build groups:

- Three binaries: `my_wine`, `my_wine64`, and `my_wine32`.
- PE32-only files such as `src/loader/pe32_entry.c`,
  `src/loader/pe32_run_guest.S`, `src/syscall/clone.S`,
  `src/syscall/mmap2_asm.S`, and `src/heap/musl_malloc_32_compat.c`.
- PE32+-only files such as `src/main.c`, `src/run_guest.S`,
  `src/syscall/clone64.S`, and the musl oldmalloc files.
- Shared guest-sensitive groups in `src/loader/`, `src/syscall/`,
  `src/msvcrt/`, `src/heap/`, and `src/crt/`.
- Ignored local artifacts: `build/`, `build32/`, sample `.exe/.dll` files,
  `src/syscall/dispatcher_generated.c`, `include/crt_offsets_generated.h`, and
  the stray `src/loader/import_resolve.d`.

## Suggested Steps

1. Group variables: toolchain, directories, source lists, object lists.
2. Group generated file rules before objects that depend on them.
3. Group 64-bit target rules.
4. Group 32-bit target rules.
5. Group test target rules.
6. Group sample targets.
7. Group clean/rebuild helper targets.
8. Keep audit architecture classifications intact or update the audit if a
   build grouping changes.

## Done Criteria

- `make run-tests` passes.
- `make all`, `make tests`, `make samples SAMPLE=hello_world`, and clean targets
  still behave as documented.
- The Makefile can be read top-to-bottom without jumping between unrelated
  sections.
