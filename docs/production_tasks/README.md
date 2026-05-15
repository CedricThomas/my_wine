# Production-Grade Task Plan

This folder breaks the production-readiness work into separate sessions.
Each task should be handled as an independent change set with behavior kept
stable unless the task explicitly says otherwise.

Recommended order:

1. [Audit And Inventory](01-audit-and-inventory.md)
2. [Build System Cleanup](02-build-system-cleanup.md)
3. [Generated Files Workflow](03-generated-files-workflow.md)
4. [Testing And CI](11-testing-and-ci.md)
5. [Runtime Logging And Diagnostics](04-runtime-logging-and-diagnostics.md)
6. [Architecture Boundaries](06-architecture-boundaries.md)
7. [PE Bounds And Loader Hardening](05-pe-bounds-and-loader-hardening.md)
8. [Header And API Cleanup](07-header-and-api-cleanup.md)
9. [Shared Utility Layer](08-shared-utility-layer.md)
10. [Duplicate And Dead Code Removal](09-duplicate-and-dead-code-removal.md)
11. [Large File Refactors](10-large-file-refactors.md)
12. [Naming Cleanup](12-naming-cleanup.md)
13. [Comment Cleanup](13-comment-cleanup.md)
14. [Docs Refresh](14-docs-refresh.md)

The audit outputs in `audit/source-inventory.md` and
`audit/architecture-boundaries.md` are the baseline for tasks 2-14. Before
starting any follow-up task, read the relevant audit sections and update the
task if the source layout, ownership layers, dependency rules, or glibc-safety
boundaries have changed since the audit.

Default done criteria for every task:

- `make run-tests` passes.
- Behavior changes are called out explicitly.
- Any stale or misleading note discovered during the task is either fixed or
  recorded for the docs refresh task.
- Any change to architecture classification, generated artifacts, glibc-safety
  constraints, or stale-doc candidates is reflected in `audit/source-inventory.md`.
- Any change to layer ownership, allowed dependencies, or libc/syscall-only
  zones is reflected in `audit/architecture-boundaries.md`.
- The change stays scoped to the task, unless a small prerequisite fix is needed.
