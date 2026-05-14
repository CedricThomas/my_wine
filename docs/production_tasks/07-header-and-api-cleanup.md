# Header And API Cleanup

## Goal

Separate public contracts from private implementation headers.

## Why

Clear headers make module boundaries enforceable. They also make it easier to
see what APIs are stable, internal, architecture-specific, or test-only.

## Scope

- Review headers in `include/` and source subdirectories.
- Move private declarations closer to implementation where appropriate.
- Split large headers by responsibility.
- Remove unused declarations.

## Suggested Steps

1. Inventory which `.c` files include each header.
2. Classify headers as public, shared-internal, module-private, or test helper.
3. Move private headers to module directories.
4. Reduce transitive includes.
5. Run the full build after each move.

## Done Criteria

- Public headers expose only intentional project-wide contracts.
- Private headers are named and located as private implementation details.
- No source file includes broad headers just to get one unrelated declaration.

