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

## Suggested Steps

1. Group variables: toolchain, directories, source lists, object lists.
2. Group generated file rules before objects that depend on them.
3. Group 64-bit target rules.
4. Group 32-bit target rules.
5. Group test target rules.
6. Group sample targets.
7. Group clean/rebuild helper targets.

## Done Criteria

- `make run-tests` passes.
- `make all`, `make tests`, `make samples SAMPLE=hello_world`, and clean targets
  still behave as documented.
- The Makefile can be read top-to-bottom without jumping between unrelated
  sections.

