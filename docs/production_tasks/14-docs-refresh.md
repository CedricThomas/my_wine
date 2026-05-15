# Docs Refresh

## Goal

Make documentation accurate after cleanup is complete.

## Why

Docs are currently stale. Rewriting them too early would duplicate churn, so
this should happen after source boundaries, names, and build workflows are
stable.

## Scope

- README.
- Architecture docs.
- Onboarding docs.
- PE32 docs.
- Debug docs.
- Doom95 implementation path and old subplans.
- `improvement_for_later.md`.

## Audit Inputs

Use the audit's stale-doc candidates, current source inventory, and
`audit/architecture-boundaries.md` as the starting point:

- Treat `samples/doom95/docs` as planning/reference material unless rewritten
  against current PE32 support.
- Review `samples/doom95/docs/reference/risks.md` and
  `samples/doom95/docs/reference/loader_notes.md` for stale "no PE32 support"
  claims.
- Review `docs/debug.md` against the current three-binary direct-dispatch
  runtime.
- Keep `README.md`, `docs/onboarding.md`, `docs/architecture.md`,
  `docs/rationale.md`, and `docs/PE32.md` aligned with the final source
  boundaries after cleanup.
- Keep user-facing architecture docs concise, and link to
  `audit/architecture-boundaries.md` for detailed ownership/dependency rules
  unless the audit file is replaced by an equivalent maintained doc.

## Suggested Steps

1. Mark stale docs as stale or move them to an archive while cleanup is ongoing.
2. Rewrite docs from source behavior, not previous docs.
3. Keep current architecture docs short and operational.
4. Keep deep dives only where they are actively maintained.
5. Delete obsolete implementation plans when they no longer represent work.
6. Update `audit/source-inventory.md` if docs refresh changes stale-doc status.
7. Update `audit/architecture-boundaries.md` if docs refresh identifies a
   boundary description that no longer matches source behavior.

## Done Criteria

- A new contributor can build, test, and understand the active architecture.
- Stale Doom95 or historical implementation paths are removed or clearly
  archived.
- `improvement_for_later.md` contains only current, actionable items.
