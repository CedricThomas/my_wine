# Non-Future-Proof Implementations

> From quality_report.log, generated 2026-05-06

---

## Findings

[FINDING D1] — x86_64-only assumption (no architecture abstraction)
- **Severity**: HIGH
- **Files**: include/nt_constants.h (all TEB/PEB offsets), src/syscall/syscalls_inline.h (inline asm syscalls), src/syscall/thunk_gen.c (x86 opcodes), src/loader/gs_base.c (arch-specific GS base), include/pe.h (IMAGE_FILE_MACHINE_AMD64 check)
- **Description**: The entire codebase assumes x86_64:
  - TEB/PEB offsets are x86_64-specific (different on ARM64)
  - Inline syscall macros use x86 syscall instruction with specific regs
  - Thunk generation writes x86 machine code
  - GS segment usage for TEB is x86-specific (ARM64 uses TCB)
  - PE validation checks Machine == AMD64

  This means the code will never run on ARM64 Windows PE or IA-64.

- **Suggested Fix**: For a minimal loader this is reasonable, but add a compile-time #error if not x86_64 to make the limitation explicit. Consider architecture abstraction layer for TEB offsets and syscall numbers.

---

[FINDING D2] — Syscall numbers tied to specific Windows versions
- **Severity**: HIGH
- **Files**: include/nt_constants.h (lines 1–46), src/loader/ordinal_table.c (lines 13–176)
- **Description**: NT syscall numbers (e.g., NtAllocateVirtualMemory=0x18, NtClose=0x0F) are specific to a particular Windows version. The comments say "Windows 10/11 x64" but different Windows versions (8.1, Server 2012, 11 24H2) have different syscall numbers. The ordinal table is also version-specific.

  If a PE compiled against a different Windows version is loaded, syscall dispatch will silently invoke the wrong handler.

- **Suggested Fix**:
  - Make syscall numbers configurable (e.g., from a .def file that can be regenerated per Windows version)
  - The current approach (nt_syscalls.def + gen_dispatcher.py) is partially correct but the file is undocumented
  - Add version detection and warning in import_resolve.c

---

[FINDING D3] — Page size hardcoded to 4096, not queried from OS
- **Severity**: MEDIUM
- **Files**: include/common.h:9, src/syscall/abi_wrappers.h:6
- **Description**: PAGE_SIZE is hardcoded to 4096. While this is correct for all modern x86_64 Linux kernels, it's not portable. If this code were to run on a system with 64K pages (e.g., some ARM servers), it would fail.

- **Suggested Fix**: Query page size at startup via sysconf(_SC_PAGESIZE) or getpagesize(), and use the runtime value. For the syscall-safe path, accept the 4096 assumption but document it clearly.

---

[FINDING D4] — CRT offset discovery tied to specific mingw-w64 layout
- **Severity**: MEDIUM
- **Files**: src/msvcrt/crt_offset_discovery.c (full file)
- **Description**: The CRT offset discovery mechanism assumes a specific mingw-w64 CRT layout. It looks for symbols like _argc, __argc, _environ, __envp in .bss at specific offsets. The text-scanning fallback (mov rip+disp pattern matching) is also specific to how mingw-w64 generates .refptr references.

  This would not work with MSVC-compiled PE files, which use a completely different CRT layout and initialization sequence.

- **Suggested Fix**: Document the mingw-w64-only limitation. Consider making the offset generation script (gen_crt_offsets.sh) part of the build process so it adapts to the local toolchain.

---

[FINDING D5] — No PE32 (32-bit) support
- **Severity**: MEDIUM
- **Files**: src/pe_headers.c:62-65
- **Description**: parse_nt_headers() only accepts IMAGE_NT_OPTIONAL_HDR64_MAGIC (0x20B). PE32 binaries (0x10B magic) are rejected. A 32-bit Windows executable will fail to load with "Invalid NT headers".

  This is a reasonable limitation for a minimal loader, but it should be documented. Consider emitting a clearer error message.

- **Suggested Fix**: Add a check in parse_nt_headers that returns a distinct error code for PE32 vs invalid headers, so the error message can say "PE32 not supported (only PE32+)" instead of "Invalid NT headers".

---

## Implementation Plan

Tasks ordered by priority (impact vs effort). Each task is independently implementable.

### Task 1: Add compile-time architecture guard and documentation
- **Related Finding(s)**: D1
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

### Task 2: Document and improve syscall number configurability
- **Related Finding(s)**: D2
- **Impact**: Makes the Windows version dependency explicit; documents the existing .def + gen_dispatcher.py approach; adds runtime warning if a PE's expected Windows version differs
- **Files to create**: (none)
- **Files to modify**:
  - include/nt_constants.h — add comments explaining which Windows version the syscall numbers target
  - src/loader/ordinal_table.c — add comments about version specificity
  - src/loader/import_resolve.c — add a version detection check with warning
- **Steps**:
  1. Add a comment block at the top of nt_constants.h: "Syscall numbers for Windows 10/11 x64 (build XXXX). Regenerate via gen_dispatcher.py if targeting a different version."
  2. Add similar documentation to ordinal_table.c
  3. In import_resolve.c, after parsing PE headers, check the MajorOperatingSystemVersion/MinorOperatingSystemVersion from the PE OptionalHeader and emit a warning if it doesn't match
  4. Document nt_syscalls.def and gen_dispatcher.py in README or a BUILDING.md
  5. Compile and verify
- **Dependencies**: none
- **Verification**: Build succeeds; warning is emitted for PEs with mismatched Windows version; documentation is present

---

### Task 3: Add runtime page size query with fallback
- **Related Finding(s)**: D3
- **Impact**: Makes page size portable; adds runtime detection with 4096 fallback for syscall-safe path; documents the limitation clearly
- **Files to create**: (none)
- **Files to modify**:
  - include/common.h — add runtime page size variable and query logic
  - src/syscall/abi_wrappers.h — add documentation about 4096 assumption
- **Steps**:
  1. In include/common.h, change PAGE_SIZE from a literal to a runtime-initialized const: `extern const unsigned long PAGE_SIZE;`
  2. In src/main.c (early init), call `PAGE_SIZE = sysconf(_SC_PAGESIZE)` (or getpagesize())
  3. For the syscall-safe path in abi_wrappers.h, keep 4096 but add comment: "Hardcoded for syscall-safety; verified against runtime PAGE_SIZE in init"
  4. Add assertion at startup: `assert(PAGE_SIZE == 4096)` for current platform
  5. Compile and verify
- **Dependencies**: none
- **Verification**: Build succeeds; PAGE_SIZE is correctly detected at runtime; behavior unchanged on x86_64 Linux

---

### Task 4: Document mingw-w64 CRT limitation and integrate gen_crt_offsets.sh
- **Related Finding(s)**: D4
- **Impact**: Makes the toolchain dependency explicit; ensures CRT offsets are regenerated when the toolchain changes
- **Files to create**: (none)
- **Files to modify**:
  - src/msvcrt/crt_offset_discovery.c — add top-of-file comment about mingw-w64-only assumption
  - Makefile — add gen_crt_offsets.sh to build steps (if not already)
- **Steps**:
  1. Add a prominent comment in crt_offset_discovery.c: "This file assumes mingw-w64 CRT layout. Will not work with MSVC-compiled PEs."
  2. Ensure gen_crt_offsets.sh is invoked during build (add to Makefile if not already present)
  3. Add a README note about requiring mingw-w64 toolchain
  4. Compile and verify
- **Dependencies**: none
- **Verification**: Build succeeds; documentation is present; gen_crt_offsets.sh is in build flow

---

### Task 5: Improve PE32 rejection error message
- **Related Finding(s)**: D5
- **Impact**: Provides a clear, actionable error message for 32-bit PE files instead of a generic "Invalid NT headers"
- **Files to create**: (none)
- **Files to modify**:
  - src/pe_headers.c — distinguish PE32 from invalid headers in parse_nt_headers()
- **Steps**:
  1. In parse_nt_headers(), check for IMAGE_NT_OPTIONAL_HDR32_MAGIC (0x10B) before the generic failure path
  2. Return a distinct error code (e.g., -2 for PE32, -1 for invalid)
  3. Update the caller (src/main.c) to emit "PE32 (32-bit) not supported — only PE32+ (64-bit) is supported" for the PE32 case
  4. Compile and verify
- **Dependencies**: none
- **Verification**: Build succeeds; loading a PE32 binary produces a clear error message
