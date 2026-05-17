# Task Plans

This folder tracks short implementation plans for branch-quality fixes that
should land before or alongside the next Doom95 phases.

Current plans:

- `01-fix-flip-chain-semantics.md`
- `02-fix-beginpaint-endpaint-dc-lifecycle.md`
- `03-fix-cursor-id-mapping.md`
- `04-fix-setfocus-semantics.md`
- `05-harden-user32-class-registry.md`

Execution guidance:

1. Fix flip-chain semantics first because DirectDraw depends on it.
2. Land DC lifecycle and cursor fixes next; both are localized and low risk.
3. Correct `SetFocus()` semantics after updating tests.
4. Harden the class registry before more user32/dialog work expands the surface area.
