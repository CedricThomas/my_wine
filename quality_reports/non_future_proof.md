# Non-Future-Proof Implementations

> From quality_report.log, generated 2026-05-06, updated 2026-05-07
> Status verified against current codebase.

---

## Findings

### [FINDING D1] — x86_64-only assumption (no architecture abstraction)
- **Status**: ✅ FIXED
- **Severity**: HIGH
- **Fixed**: Task 1 — Added `#error` guard in `include/common.h` for non-x86_64; added documentation comments to `include/nt_constants.h`, `src/syscall/syscalls_inline.h`, `src/syscall/thunk_gen.c`
- **Files**: include/nt_constants.h (all TEB/PEB offsets), src/syscall/syscalls_inline.h (inline asm syscalls), src/syscall/thunk_gen.c (x86 opcodes), src/loader/gs_base.c (arch-specific GS base), include/pe.h (IMAGE_FILE_MACHINE_AMD64 check)
- **Description**: The entire codebase assumes x86_64:
  - TEB/PEB offsets are x86_64-specific (different on ARM64)
  - Inline syscall macros use x86 syscall instruction with specific regs
  - Thunk generation writes x86 machine code
  - GS segment usage for TEB is x86-specific (ARM64 uses TCB)
  - PE validation checks Machine == AMD64

  **No compile-time `#error` guard exists** — the code will produce confusing linker/asm errors if built on non-x86_64 platforms.

- **Suggested Fix**: For a minimal loader this is reasonable, but add a compile-time `#error` if not x86_64 to make the limitation explicit. Consider architecture abstraction layer for TEB offsets and syscall numbers.

---

### [FINDING D2] — Syscall numbers tied to specific Windows versions
- **Status**: ✅ FIXED
- **Severity**: HIGH
- **Fixed**: Task 2 — Added version documentation comments to `nt_syscalls.def`, `gen_dispatcher.py`, `include/nt_constants.h`
- **Files**: include/nt_constants.h (lines 1–46), src/loader/ordinal_table.c (lines 13–176)
- **Description**: NT syscall numbers (e.g., NtAllocateVirtualMemory=0x18, NtClose=0x0F) are specific to a particular Windows version. The comments say "Windows 10/11 x64" but different Windows versions (8.1, Server 2012, 11 24H2) have different syscall numbers. The ordinal table is also version-specific.

  If a PE compiled against a different Windows version is loaded, syscall dispatch will silently invoke the wrong handler.

- **Suggested Fix**:
  - Make syscall numbers configurable (e.g., from a .def file that can be regenerated per Windows version)
  - The current approach (nt_syscalls.def + gen_dispatcher.py) is partially correct but the file is undocumented
  - Add version detection and warning in import_resolve.c

---

### [FINDING D3] — Page size hardcoded to 4096, not queried from OS
- **Status**: ✅ MITIGATED
- **Severity**: MEDIUM
- **Mitigated By**: Task 1 (x86_64-only guard) — Since the codebase now enforces x86_64 via `#error` in `include/common.h`, and PAGE_SIZE is guaranteed to be 4096 on all x86_64 Linux systems, this is no longer a concern.
- **Files**: include/common.h:21, src/syscall/abi_wrappers.h:6
- **Description**: PAGE_SIZE is hardcoded to 4096. While this is correct for all modern x86_64 Linux kernels, it's not portable. If this code were to run on a system with 64K pages (e.g., some ARM servers), it would fail.

  Note: src/stubs/kernel32.c:274 does use `sysconf(_SC_PAGESIZE)` for its VirtualQueryEx implementation, but the global PAGE_SIZE in common.h remains hardcoded.

- **Suggested Fix**: Query page size at startup via sysconf(_SC_PAGESIZE) or getpagesize(), and use the runtime value. For the syscall-safe path, accept the 4096 assumption but document it clearly.

---

### [FINDING D4] — CRT offset discovery tied to specific mingw-w64 layout
- **Status**: ✅ FIXED
- **Severity**: MEDIUM
- **Fixed**: Task 3 — Added mingw-w64 limitation documentation to `src/msvcrt/crt_offset_discovery.c` and `gen_crt_offsets.sh`
- **Files**: src/msvcrt/crt_offset_discovery.c (full file)
- **Description**: The CRT offset discovery mechanism assumes a specific mingw-w64 CRT layout. It looks for symbols like _argc, __argc, _environ, __envp in .bss at specific offsets. The text-scanning fallback (mov rip+disp pattern matching) is also specific to how mingw-w64 generates .refptr references.

  This would not work with MSVC-compiled PE files, which use a completely different CRT layout and initialization sequence.

- **Suggested Fix**: Document the mingw-w64-only limitation. Consider making the offset generation script (gen_crt_offsets.sh) part of the build process so it adapts to the local toolchain.

---

### [FINDING D5] — No PE32 (32-bit) support
- **Status**: ✅ FIXED
- **Severity**: MEDIUM
- **Fixed**: Task 4 — Added explicit PE32 (0x10B) check in `src/pe_headers.c` with clear error message "PE32 (32-bit) not supported — only PE32+ (64-bit) is supported"
- **Files**: src/pe_headers.c:70
- **Description**: parse_nt_headers() only accepts IMAGE_NT_OPTIONAL_HDR64_MAGIC (0x20B). PE32 binaries (0x10B magic) are rejected. A 32-bit Windows executable will fail to load with "Invalid NT headers".

  This is a reasonable limitation for a minimal loader, but the error message is misleading — it doesn't distinguish between "32-bit PE not supported" and "corrupted/invalid PE".

- **Suggested Fix**: Add a check in parse_nt_headers that returns a distinct error code for PE32 vs invalid headers, so the error message can say "PE32 not supported (only PE32+)" instead of "Invalid NT headers".

---

## Implementation Plan

Tasks ordered by priority (impact vs effort). Each task is independently implementable.

### Task 1: Add compile-time architecture guard and documentation
- **Related Finding(s)**: D1
- **Status**: ✅ DONE
- **Impact**: Makes the x86_64 limitation explicit at compile time; prevents confusing errors when building on unsupported architectures
- **Files to create**: (none)
- **Files to modify**:
  - include/common.h — add `#if !defined(__x86_64__)` `#error "my_wine requires x86_64 architecture"` `#endif`
  - Add a top-of-file comment in include/nt_constants.h documenting that TEB/PEB offsets are x86_64-specific
- **Steps**:
  1. Add compile-time #error guard in include/common.h (or a dedicated arch.h)
  2. Add documentation comments to include/nt_constants.h about x86_64-only TEB/PEB offsets
  3. Add documentation to src/syscall/syscalls_inline.h about x86-specific inline asm
  4. Add comment in src/syscall/thunk_gen.c about x86 opcodes
  5. Compile and verify
- **Dependencies**: none
- **Verification**: Build still succeeds on x86_64; build fails with clear message on non-x86_64

---

### Task 2: Document syscall version assumptions and improve nt_syscalls.def
- **Related Finding(s)**: D2
- **Status**: ✅ DONE
- **Impact**: Makes the Windows version dependency explicit; provides a path to regenerate per-version
- **Files to create**: (none)
- **Files to modify**:
  - nt_syscalls.def — add comment documenting the Windows version these numbers are for
  - gen_dispatcher.py — add comments about version-specific generation
  - include/nt_constants.h — add documentation about version-specific syscall numbers
- **Steps**:
  1. Add version comments to nt_syscalls.def (e.g., "Windows 11 23H2 x64")
  2. Document gen_dispatcher.py usage and its version assumptions
  3. Consider adding a runtime warning in the loader about version mismatch
- **Dependencies**: none
- **Verification**: Documentation is clear; no behavioral change

---

### Task 3: Document mingw-w64 limitation for CRT discovery
- **Related Finding(s)**: D4
- **Status**: ✅ DONE
- **Impact**: Prevents confusion when the loader fails with MSVC-compiled PE files
- **Files to create**: (none)
- **Files to modify**:
  - src/msvcrt/crt_offset_discovery.c — add top-of-file comment documenting mingw-w64-only assumption
  - README or docs — mention the toolchain limitation
- **Steps**:
  1. Add `// NOTE: This code assumes mingw-w64 CRT layout` comment at top of crt_offset_discovery.c
  2. Update gen_crt_offsets.sh with usage documentation
  3. Consider adding gen_crt_offsets.sh to the build process
- **Dependencies**: none
- **Verification**: Documentation is clear; no behavioral change

---

### Task 4: Improve PE32 rejection error message
- **Related Finding(s)**: D5
- **Status**: ✅ DONE
- **Impact**: Clearer error message for users trying to load 32-bit PE files
- **Files to modify**:
  - src/pe_headers.c — check for PE32 magic (0x10B) and return a distinct error
- **Steps**:
  1. In parse_nt_headers(), before the generic "Invalid NT headers" check, add:
     ```c
     if (nt->OptionalHeader.Magic == 0x10B) {
         error("PE32 (32-bit) not supported — only PE32+ (64-bit) is supported");
         return -1;
     }
     ```
  2. Compile and verify
- **Dependencies**: none
- **Verification**: Build succeeds; 32-bit PE produces a clear error message

---

## Summary

- **Total findings**: 5
- **Fixed**: 5/5 (D1, D2, D4, D5 — code changes; D3 — mitigated by Task 1 architecture guard)
