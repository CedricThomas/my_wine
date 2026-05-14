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

## Suggested Steps

1. Mark stale docs as stale or move them to an archive while cleanup is ongoing.
2. Rewrite docs from source behavior, not previous docs.
3. Keep current architecture docs short and operational.
4. Keep deep dives only where they are actively maintained.
5. Delete obsolete implementation plans when they no longer represent work.

## Done Criteria

- A new contributor can build, test, and understand the active architecture.
- Stale Doom95 or historical implementation paths are removed or clearly
  archived.
- `improvement_for_later.md` contains only current, actionable items.

