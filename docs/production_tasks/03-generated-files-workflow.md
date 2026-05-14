# Generated Files Workflow

## Goal

Make generated files predictable and verifiable.

## Why

Generated files such as `src/syscall/dispatcher_generated.c` are ignored by git
but required by the build. Production-grade workflows need clean checkout builds
and CI checks that catch stale generation logic.

## Scope

- Review all generated files and their generators.
- Decide which generated files are checked in and which are ignored.
- Add a freshness check target.
- Make clean generation work from a fresh tree.

## Suggested Steps

1. Inventory generated files.
2. Ensure generators are deterministic.
3. Add `make gen` or similarly named aggregate target.
4. Add `make check-generated` that fails when generated output differs.
5. Document the policy in the Makefile or a short doc.

## Done Criteria

- A clean tree can build generated files automatically.
- CI can verify generated output freshness.
- `.gitignore` matches the chosen policy.

