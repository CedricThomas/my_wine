# PE Bounds And Loader Hardening

## Goal

Make PE parsing and loader memory access robust against malformed inputs.

## Why

The loader consumes PE-controlled offsets, sizes, RVAs, section metadata, import
tables, export tables, relocations, and string pointers. Production code should
validate these before dereferencing or copying.

## Scope

- Harden section copy boundaries.
- Harden RVA-to-pointer conversion.
- Harden import descriptor walking.
- Harden export table parsing.
- Harden relocation parsing.
- Review fixed-address mappings.

## Audit Inputs

Prioritize files marked risky or parser/memory-sensitive in
`audit/source-inventory.md`, and keep the layer/dependency rules from
`audit/architecture-boundaries.md` in view:

- `src/pe_headers.c`, `src/pe_imports.c`, `src/pe_rip_scan.c`,
  `src/pe_symbols.c`
- `src/loader/image_mapper.c`, `src/loader/relocations.c`,
  `src/loader/import_resolve.c`, `src/loader/import_table.c`,
  `src/loader/export_table.c`, `src/loader/dll_loader.c`
- `src/msvcrt/crt_offset_discovery.c`
- `src/loader/pe32_entry.c`

Keep PE32 and PE32+ pointer-size differences explicit while hardening. Checked
RVA and pointer helpers should not make PE parsing depend on loader state or
make syscall-only loader paths depend on libc-backed parser utilities.

## Suggested Steps

1. Introduce central helpers for checked RVA ranges.
2. Replace direct `(base + rva)` access in loader code with checked helpers.
3. Add malformed PE unit tests for each parser path.
4. Replace dangerous `MAP_FIXED` usage where possible with safer behavior.
5. Document when fixed mappings are required.
6. Cover PE32 and PE32+ cases from the audit before changing shared helpers.
7. Update `audit/architecture-boundaries.md` if hardening introduces a new
   shared helper layer or changes allowed dependencies.

## Done Criteria

- Malformed PE tests fail gracefully instead of crashing.
- Loader code has a consistent checked-RVA pattern.
- Fixed mapping decisions are explicit and justified.
