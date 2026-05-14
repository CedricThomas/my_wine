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

## Suggested Steps

1. Identify all unconditional trace writes.
2. Create or reuse syscall-safe debug-write helpers.
3. Separate `TRACE`, `DEBUG`, `WARNING`, and `ERROR` behavior.
4. Add tests or sample assertions that normal execution is quiet.

## Done Criteria

- Normal sample runs do not emit internal traces.
- `MY_WINE_DEBUG=1` still exposes useful diagnostics.
- Guest stdout is not mixed with loader diagnostics.

