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

## Audit Inputs

Start from the audit's header inventory and
`audit/architecture-boundaries.md` dependency rules:

- Architecture-critical headers: `include/wine_abi.h`, `include/pe.h`,
  `include/nt_constants.h`, `include/nt_syscalls.def`.
- Guest ABI headers: `include/kernel32.h`, `include/msvcrt.h`,
  `include/ntdll.h`, `include/syscall/*.h`.
- Removed stale candidates: `include/loader/pe32_trampoline.h`,
  `include/syscall/signal_handler.h`, `include/render_backend.h`.

Do not move declarations across glibc-safe and no-glibc boundaries without
making that boundary explicit in the header name or location. Use the boundary
document to decide whether a header is a public contract, layer-private API, or
guest-safe/syscall-only API.

## Suggested Steps

1. Inventory which `.c` files include each header.
2. Classify headers as public, shared-internal, module-private, or test helper.
3. Move private headers to module directories.
4. Reduce transitive includes.
5. Run the full build after each move.
6. Reconcile the resulting header classification with the audit.
7. Update `audit/architecture-boundaries.md` if header moves change allowed
   dependencies between layers.

## Done Criteria

- Public headers expose only intentional project-wide contracts.
- Private headers are named and located as private implementation details.
- No source file includes broad headers just to get one unrelated declaration.
