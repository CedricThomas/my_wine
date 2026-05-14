# Runtime Logging And Diagnostics

## Goal

Make normal execution quiet and make diagnostic output intentional.

## Why

Some runtime paths currently write traces unconditionally. That pollutes stdout
or stderr, makes tests brittle, and hides the difference between user-visible
errors and debug diagnostics.

## Scope

- Gate import resolution traces behind a debug flag.
- Gate syscall trace output behind a debug flag.
- Keep real errors visible.
- Preserve syscall-safe logging for code that cannot call glibc.

## Audit Inputs

Use the audit's "Files That Must Not Call Glibc After FS/GS Switch" section
before touching diagnostics. In particular:

- Do not add `DEBUG`, `fprintf`, `perror`, or other libc-backed logging to
  `src/loader/import_resolve.c`, `src/loader/dll_loader.c`,
  `src/loader/module_list.c`, `src/loader/export_table.c`,
  `src/syscall/dispatcher.c`, dispatcher entry code, or guest-facing
  `src/msvcrt/*.c` stubs.
- Prefer direct syscall/write helpers for guest paths and signal/crash paths.
- Treat `include/debug.h` as libc-backed unless the call site is clearly
  pre-switch host/setup code.

## Suggested Steps

1. Identify all unconditional trace writes.
2. Create or reuse syscall-safe debug-write helpers.
3. Separate `TRACE`, `DEBUG`, `WARNING`, and `ERROR` behavior.
4. Add tests or sample assertions that normal execution is quiet.
5. Update the audit if a file's glibc-safety classification changes.

## Done Criteria

- Normal sample runs do not emit internal traces.
- `MY_WINE_DEBUG=1` still exposes useful diagnostics.
- Guest stdout is not mixed with loader diagnostics.
