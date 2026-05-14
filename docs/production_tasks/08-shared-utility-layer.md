# Shared Utility Layer

## Goal

Centralize repeated low-level helpers.

## Why

The codebase has repeated hand-rolled string, formatting, pointer, and debug
helpers because some paths cannot call glibc. That constraint is real, but the
duplication makes correctness harder to maintain.

## Scope

- Syscall-safe string helpers.
- Syscall-safe formatting helpers.
- Debug/write helpers.
- Checked arithmetic and range helpers.
- Guest pointer validation helpers.

## Suggested Steps

1. Identify duplicate helper implementations.
2. Split helpers into libc-allowed and syscall-safe groups.
3. Replace callers gradually.
4. Add tests for utility functions with edge cases.

## Done Criteria

- Duplicate helper implementations are reduced.
- Utility names clearly state whether they are syscall-safe.
- Callers in post-FS/GS-switch paths do not accidentally call glibc.

