# Testing And Local Quality Gates

## Goal

Create confidence that cleanup and hardening do not break runtime behavior,
even before the project has hosted CI.

## Why

The existing unit tests pass, but production-grade runtime code needs coverage
for samples, malformed inputs, architecture-specific behavior, generated files,
and build reproducibility.

There is no hosted CI yet. Treat this task as two layers:

1. A local, repeatable verification gate that developers can run before risky
   changes.
2. CI-ready commands and scripts that can later be wired into GitHub Actions,
   GitLab CI, Buildkite, or any other runner without changing the checks.

## Scope

- Unit tests.
- Sample execution tests.
- PE32 and PE32+ integration tests.
- Malformed PE tests.
- Local quality-gate commands.
- Optional CI configuration.
- Static analysis and shell checks.

## Audit Inputs

The audit moved this task earlier because risky refactors need tests first.
Use `audit/source-inventory.md` for risky files and
`audit/architecture-boundaries.md` for ownership/dependency and libc-zone
coverage. Prioritize coverage for:

- PE32 and PE32+ sample runs: `hello_world`, `file_io`, `heap_test`,
  `sync_test`, `virtual_mem`, and `dll_loader` variants.
- Existing targeted tests: `test_pe32`, `test_syscall_dispatch`,
  `test_import_resolution`, `test_relocations`, `test_module_registry`, and
  `test_export_parsing`.
- Generated dispatcher freshness.
- Malformed PE parser coverage before hardening.
- Guest-safe/glibc-free paths that the audit marks high risk.
- Boundary-sensitive transitions: wrapper backend dispatch, PE32 vs PE32+
  entry, GS/FS handoff, syscall dispatch, Windows API stubs, and heap calls.

## Suggested Steps

1. Add or document one local gate command, such as `make verify`, that runs the
   checks expected before production-task work lands.
2. Include `make run-tests` in that gate.
3. Include `make check-generated` in that gate.
4. Add a shellcheck step for scripts when `shellcheck` is installed; if the tool
   is unavailable, document the missing dependency rather than silently passing.
5. Add selected sample-run tests. Start with the audit-prioritized samples and
   allow targeted runs while the full sample set is still expensive.
6. Add malformed PE corpus tests.
7. Add sanitizer builds where compatible.
8. Record any untestable audit risk explicitly before refactoring that area.
9. Add targeted tests before changing any boundary-owned layer that lacks
   coverage.
10. Once a hosted CI provider exists, wire the same local gate command into the
    CI job instead of creating separate CI-only behavior.

## Local Gate Candidates

Minimum gate:

```sh
make run-tests
make check-generated
```

Broader gate:

```sh
make run-tests
make check-generated
make run-samples-scenarios
```

Optional local static checks:

```sh
shellcheck scripts/*.sh
```

Docker is required for sample builds. If Docker is unavailable on a developer
machine, keep `make run-tests` and `make check-generated` as the required gate
and record the skipped sample coverage in the change notes.

## Done Criteria

- A documented local gate exists and can be run before risky changes.
- The local gate does not require local-only state beyond documented toolchain
  dependencies.
- Failing tests produce actionable logs.
- CI integration is either added or explicitly deferred until a provider exists.
