# Testing And CI

## Goal

Create confidence that cleanup and hardening do not break runtime behavior.

## Why

The existing unit tests pass, but production-grade runtime code needs coverage
for samples, malformed inputs, architecture-specific behavior, generated files,
and build reproducibility.

## Scope

- Unit tests.
- Sample execution tests.
- PE32 and PE32+ integration tests.
- Malformed PE tests.
- CI configuration.
- Static analysis and shell checks.

## Audit Inputs

The audit moved this task earlier because risky refactors need tests first.
Prioritize coverage for:

- PE32 and PE32+ sample runs: `hello_world`, `file_io`, `heap_test`,
  `sync_test`, `virtual_mem`, and `dll_loader` variants.
- Existing targeted tests: `test_pe32`, `test_syscall_dispatch`,
  `test_import_resolution`, `test_relocations`, `test_module_registry`, and
  `test_export_parsing`.
- Generated dispatcher freshness.
- Malformed PE parser coverage before hardening.
- Guest-safe/glibc-free paths that the audit marks high risk.

## Suggested Steps

1. Add a CI job for `make run-tests`.
2. Add a CI job for generated-file freshness.
3. Add a shellcheck job for scripts.
4. Add selected sample-run tests.
5. Add malformed PE corpus tests.
6. Add sanitizer builds where compatible.
7. Record any untestable audit risk explicitly before refactoring that area.

## Done Criteria

- CI runs on every pull request or push.
- CI does not require local-only state.
- Failing tests produce actionable logs.
