# Loader Architecture — Deep Dive

The loader subsystem (`src/loader/`) is the core of my_wine. It maps PE images,
resolves imports, sets up the Windows TEB/PEB structures, and orchestrates the
handoff from the Linux host to the guest Windows binary.

This document covers the five stages of the loading pipeline in detail:

1. **Image Mapping** — mmap at preferred base, copy section data, per-section mprotect
2. **Import Resolution** — two-pass IAT patching + RIP-relative thunk scanning
3. **TEB/PEB Setup** — GS→TEB, PEB + LDR data, module list
4. **Entry Point** — stack switch, guest_setup(), crash handlers
5. **PE32 vs PE32+** — process model, ABI, FS vs GS, syscall mechanics

---

## 1. Image Mapping

**Files:** `src/loader/image_mapper.c` · `src/loader/relocations.c`

The `map_image()` function (`image_mapper.c`) opens a PE file, maps it at the
preferred base address, copies section data from the file into the mapped region,
and applies per-section memory protections.

### 1.1 Flow

```
  PE file on disk
       │
       ▼
  ┌──────────────────────┐
  │ open() + fstat()     │  get file descriptor and size
  └──────────┬───────────┘
             ▼
  ┌──────────────────────┐
  │ mmap(file, PROT_READ)│  read-only map of the raw file
  └──────────┬───────────┘
             ▼
  ┌──────────────────────┐
  │ parse DOS header     │  check MZ signature, read e_lfanew
  │ parse NT headers     │  check PE signature, read Magic (0x10B/0x20B)
  │ parse section table  │  extract IMAGE_SECTION_HEADER array
  └──────────┬───────────┘
             ▼
  ┌──────────────────────┐
  │ mmap(image_base,     │  anonymous map at preferred ImageBase
  │   SizeOfImage)       │  PROT_READ|PROT_WRITE|PROT_EXEC
  └──────────┬───────────┘
             ▼
  ┌──────────────────────┐
  │ copy section data    │  for each section:
  │ + copy headers       │    memcpy(base+VirtualAddress, file+PointerToRawData)
  └──────────┬───────────┘
             ▼
  ┌──────────────────────┐
  │ apply relocations    │  patch addresses for non-preferred base
  └──────────┬───────────┘
             ▼
  ┌──────────────────────┐
  │ zero-fill .bss       │  memset sections with PointerToRawData == 0
  └──────────┬───────────┘
             ▼
  ┌──────────────────────┐
  │ mprotect per section │  set PROT_READ/PROT_WRITE/PROT_EXEC
  └──────────┬───────────┘
             ▼
  ┌──────────────────────┐
  │ munmap(file), close  │  release the file-backed mapping
  └──────────────────────┘
```

### 1.2 Base Address Selection

| Image Type | Preferred Base | Fallback |
|------------|---------------|----------|
| PE32       | PE's `ImageBase` | `0x00400000` (`PE32_DEFAULT_IMAGE_BASE`) |
| PE32+      | PE's `ImageBase` | Any (relocatable) |

For PE32 images, the loader enforces that the final base fits within 32-bit address
space (`ADDR32_LIMIT`). If the PE's preferred base exceeds `0x80000000`, the loader
falls back to `0x00400000`. This is set by the `g_loader.is_32bit` flag derived from
`pe_is_pe32(&nt)` and checked in `image_mapper.c`.

The mapping uses `MAP_FIXED_NOREPLACE` (newer kernels) or `MAP_FIXED` (older kernels).
If the preferred base is unavailable, the loader falls back to a relocatable mapping
at an arbitrary address.

### 1.3 Section Copy

Each `IMAGE_SECTION_HEADER` specifies:

- `VirtualAddress` — offset from image base where the section loads
- `PointerToRawData` — file offset of the section data
- `SizeOfRawData` — size of data in the file
- `Misc.VirtualSize` — size in memory (can be larger for .bss)
- `Characteristics` — flags: `IMAGE_SCN_MEM_READ`, `IMAGE_SCN_MEM_WRITE`, `IMAGE_SCN_MEM_EXECUTE`

For sections with `PointerToRawData == 0` or `SizeOfRawData == 0`, the section is
.bss-like and receives a `memset(dest, 0, size)`. Some PE32 images (including DOOM95)
encode .bss sections with a non-zero `SizeOfRawData` but `PointerToRawData == 0` —
these are also zeroed.

### 1.4 Per-Section Protections

After copying, each section is locked down with `mprotect()`:

```c
int prot = 0;
if (section.Characteristics & IMAGE_SCN_MEM_READ)     prot |= PROT_READ;
if (section.Characteristics & IMAGE_SCN_MEM_WRITE)    prot |= PROT_WRITE;
if (section.Characteristics & IMAGE_SCN_MEM_EXECUTE)  prot |= PROT_EXEC;
```

The size is rounded up to the page boundary: `((size + PAGE_MASK) & ~PAGE_MASK)`.

### 1.5 Relocations

**File:** `src/loader/relocations.c`

When the image cannot be mapped at the preferred `ImageBase`, the relocation table
(`DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC]`) is applied to patch all embedded
addresses by `delta = actual_base - preferred_base`.

**PE32 relocation types:**
- `IMAGE_REL_BASED_ABSOLUTE` (0) — padding, no-op
- `IMAGE_REL_BASED_HIGH` (1) — add high 16 bits of delta
- `IMAGE_REL_BASED_LOW` (2) — add low 16 bits of delta
- `IMAGE_REL_BASED_HIGHLOW` (3) — add full 32-bit delta
- `IMAGE_REL_BASED_DIR32` (4) — add full 32-bit delta (same as HIGHLOW)
- `IMAGE_REL_BASED_HIGHADJ` (5) — 16-bit adjustment (treated as HIGH + warning)

**PE32+ relocation types:**
- `IMAGE_REL_BASED_DIR64` (0xA) — add full 64-bit delta to uint64_t target

If the `IMAGE_FILE_RELOCS_STRIPPED` flag is set and the image loads at a non-preferred
base, `apply_relocations()` returns an error.

---

## 2. Import Resolution

**Files:** `src/loader/import_resolve.c` · `src/loader/import_init.c` · `src/loader/import_flat.c` ·
`src/loader/import_lookup.c` · `src/loader/import_table.c` · `src/loader/ordinal_table.c` ·
`src/pe_rip_scan.c` · `src/loader/dll_loader.c` · `src/loader/dll_path.c` · `src/loader/export_table.c`

Import resolution is the most complex phase of the loader. It uses a **two-pass approach**
to patch all references to external DLL functions in the PE image.

### 2.1 Pass 1: IAT Patching

**File:** `src/loader/import_resolve.c`

Pass 1 walks the `IMAGE_IMPORT_DESCRIPTOR` chain from the import directory and patches
each entry in the IAT (`FirstThunk`). For each descriptor:

1. **Read DLL name** from the descriptor's `Name` RVA
2. **Walk the ILT** (`OriginalFirstThunk` or `FirstThunk` if ILT is absent)
3. **For each thunk entry:**
   - If the high bit is set → **ordinal import**: look up function name from
     `ordinal_table.c`, then resolve via `resolve_loader_import()`
   - If the high bit is clear → **name import**: read the `IMAGE_IMPORT_BY_NAME` struct,
     extract the function name, then resolve
4. **Write the resolved address** into the corresponding IAT entry

The IAT is a writable section; writing here satisfies direct function calls like
`call dword [rip+offset]` that dereference the IAT.

### 2.2 Three-Tier Symbol Resolution

**File:** `src/loader/import_lookup.c`

`resolve_loader_import(dll_name, func_name)` searches for a symbol across three tiers:

| Tier | Source | Lookup Method |
|------|--------|---------------|
| 1 | `import_table.c` | Binary search (PE32+) or linear scan (PE32) |
| 2 | Loaded module exports | `find_module_by_name()` → `lookup_export()` |
| 3 | Not found | Returns NULL (guest will fault on call) |

**Tier 1** (`import_table.c`) is the static table of ~300+ stub function addresses,
organized by DLL name and function name. It covers:
- **ntdll.dll** — syscall handlers (`NtWriteFile`, `NtClose`, `NtTerminateProcess`, etc.)
- **kernel32.dll** — file I/O, memory, sync, string, process API
- **msvcrt.dll** — CRT functions (`__getmainargs`, `malloc`, `fprintf`, `_iob`, etc.)
- **user32.dll** — window, message, dialog, input API
- **gdi32.dll** — device context, bitmap, font, palette
- **ddraw.dll** / **dsound.dll** / **winmm.dll** — multimedia API

In the 64-bit build, `import_table` is sorted by function name at startup (`qsort`)
for `bsearch()`-compatible lookup. In the 32-bit build (no glibc after FS→TEB switch),
an insertion sort with hand-rolled `strcmp` is used, and the scan is linear.

`import_init.c` patches additional msvcrt symbols dynamically (e.g., `abort`, `calloc`,
`signal`, `localeconv`) with CRT stub addresses from `src/msvcrt/crt_*.c`.

### 2.3 Pass 2: RIP-Relative Thunk Scanning

**Files:** `src/loader/import_resolve.c` · `src/pe_rip_scan.c` · `src/loader/import_flat.c`

Pass 1 patches the *direct* IAT entries, but PE32+ code often has **RIP-relative
indirect jumps** (`ff 25 disp32`) in `.text` that point to IAT slots. These thunks
may reference a *different* IAT entry than the one patched by Pass 1 (because the
compiler generates separate jump-thunks per call site).

Pass 2 works by:

1. **Scanning `.text`** for `ff 25 disp32` instructions via `scan_rip_relative_jumps()`
   (`pe_rip_scan.c`)
2. **Deduplicating** unique IAT target addresses
3. **Sorting** targets by address
4. **Building a flat import array** from all descriptors (`build_flat_import_array()`
   in `import_flat.c`) — each entry records `(ILT value, resolved address, IAT address,
   DLL name, function name)`
5. **Matching** each thunk target against the flat array using three strategies:

| Strategy | Logic | File |
|----------|-------|------|
| 1: resolved overlap | Target IAT address matches a flat entry's `iat_addr` and the current value equals `resolved_addr` | `import_flat.c` |
| 2: ILT value match | Current value at target equals a flat entry's `ilt_value` → write `resolved_addr` | `import_flat.c` |
| 3: ILT offset match | Target IAT address matches flat entry's `iat_addr` → write `resolved_addr` | `import_flat.c` |

Pass 2 is **PE32+ only**. PE32 uses absolute addressing (`ff 15 disp32` or `ff 25 disp32`
with 32-bit absolute addresses), so the relocation pass already handles thunk targets.

### 2.4 Ordinal Imports

**File:** `src/loader/ordinal_table.c`

When a thunk entry has the high bit set, the lower 16 bits are an ordinal number.
`ordinal_lookup(dll_name, ordinal)` searches a static table mapping `(dll_name, ordinal) → func_name`.

The table covers:
- ~80 ntdll.dll ordinals (Windows 10/11 x64)
- ~50 kernel32.dll ordinals
- ~18 msvcrt.dll ordinals
- dplay.dll ordinals (DOOM95 multiplayer)
- comctl32.dll ordinals

If the ordinal is not in the table, `resolve_loader_import()` still gets the DLL name,
and the tier-2 lookup searches loaded module exports by ordinal.

### 2.5 DLL Loading

**Files:** `src/loader/dll_loader.c` · `src/loader/dll_path.c`

When a module depends on a DLL not in the stub table, `resolve_module_imports()`
in `import_resolve.c` detects the missing DLL and calls `find_dll_path()` to locate it.

**`find_dll_path()`** searches in order:
1. Explicit path (if the DLL name contains `/` or `\`)
2. Current directory (`.`)
3. Application directory (derived from the main PE path)
4. `WINE_DLL_PATH` environment variable (semicolon-separated)

Each location is tried with the original name, the uppercase name (Windows filenames
are case-insensitive), and with `.dll` appended if the name lacks an extension.

**`load_dll()`** in `dll_loader.c`:
1. Atomically reserves a base address below 4GB using CAS on `g_loader.dll_base_next`
2. Calls `map_image_at(path, ..., alloc_base)` to map at the reserved base
3. Allocates NT headers on the heap
4. Registers the module in `module_list.c` and links into PEB LDR
5. Recursively resolves the DLL's imports (`resolve_module_imports()` with depth guard)
6. Parses the export table (`parse_export_table()`) so other modules can find its functions
7. Calls `DllMain(DLL_PROCESS_ATTACH)` for PE32 images

The atomic base allocator avoids overlapping mappings and keeps all DLL addresses in
32-bit address space (critical for the `ms_abi` calling convention in PE32+).

### 2.6 Export Tables

**File:** `src/loader/export_table.c`

`parse_export_table(mod)` reads the `IMAGE_EXPORT_DIRECTORY` from the module and
caches the name table, ordinal table, and function table into the embedded
`EXPORT_CACHE` struct. `lookup_export(mod, func_name)` does a hand-rolled binary
search on the name table, then resolves through the ordinal table to the function
table. Forwarder strings (RVA points inside the export directory) are detected
but not resolved (deferred).

---

## 3. TEB/PEB Setup

**Files:** `src/loader/teb_peb.c` · `src/loader/gs_base.c` · `src/loader/peb_ldr.c` ·
`src/loader/module_list.c` · `src/loader/pe32_entry.c` · `src/loader/pe32_guest_launch.c`

### 3.1 TEB Allocation

**File:** `src/loader/teb_peb.c`

The Thread Environment Block is a single page allocated with `mmap(MAP_ANONYMOUS)`.

| Field | Offset (PE32+) | Offset (PE32) | Value |
|-------|---------------|---------------|-------|
| SEH chain | `TEB_SEH_CHAIN` (0x00) | `TEB32_SEH_CHAIN` (0x00) | Pointer to static SEH frame |
| TEB self-ref | `TEB64_TEB_SELF_REF` (0x08) | `TEB32_TEB_SELF_REF` (0x04) | TEB address itself |
| Thread pointer | `TEB64_THREAD_PTR` (0x48) | `TEB32_THREAD_PTR` (0x24) | TEB address itself |
| Fiber data | — | `TEB32_FIBER_DATA` (0x10) | TEB address (fixes gs:[0x48] null deref) |
| Last status | — | `TEB32_LAST_STATUS` (0x34) | `STATUS_SUCCESS` (0) |
| PEB pointer | `TEB64_PEB_PTR` (0x60) | `TEB32_PEB_PTR` (0x30) | PEB address |
| GDI TEB offset | — | `TEB32_GDI_TEB_OFFSET` (0x18) | TEB + GDI_PROCESS_LOCAL (for CRT bootstrap) |

For PE32 running on a 64-bit kernel, the TEB is mapped at fixed address
`TEB32_FIXED_ADDR` (`0x7FFDE000`) to ensure it fits in 32-bit address space.

### 3.2 PEB Allocation

The Process Environment Block is a separate page:

| Field | Offset (PE32+) | Offset (PE32) | Value |
|-------|---------------|---------------|-------|
| Being debugged | `PEB64_BEING_DEBUGGED` (0x02) | `PEB32_BEING_DEBUGGED` (0x02) | 0 |
| Image base | `PEB64_IMAGE_BASE` (0x08) | `PEB32_IMAGE_BASE` (0x08) | `g_loader.image_base` |
| Process heap | `PEB64_PROCESS_HEAP` (0x030) | `PEB32_PROCESS_HEAP` (0x03C) | from `init_process_heap()` |
| LDR pointer | `PEB64_LDR` (0x18) | `PEB32_LDR` (0x0C) | from `init_peb_ldr()` |

For PE32, the PEB is at fixed address `PEB32_FIXED_ADDR` (`0x7FFDF000`).

### 3.3 PE32 Process Parameters

**File:** `src/loader/pe32_process.c`

`wire_peb32_params()` allocates a page for RTL_USER_PROCESS_PARAMETERS containing:
- `CurrentDirectory`: `C:\` (as UTF-16)
- `DllPath`: `C:\` (as UTF-16)
- `CommandLine`: the PE path (as UTF-16)

### 3.4 GS Base Selection

**File:** `src/loader/gs_base.c`

`set_gs_base(addr)` uses a two-step strategy:

1. `arch_prctl(ARCH_SET_GS, addr)` — the standard Linux API
2. Verify with `arch_prctl(ARCH_GET_GS)` — some environments silently fail

If either step fails, the fallback uses the `wrgsbase` instruction directly
(FSGSBASE CPU feature), then verifies with `rdgsbase`.

For PE32, GS is not used — instead, **FS is set via `set_thread_area`** (syscall 243):

```c
struct modify_ldt_ldt_s ldt = {
    .entry_number = -1,        /* allocate new LDT entry */
    .base_addr = teb_address,  /* TEB base */
    .limit = 0xFFFFF,
    .seg_32bit = 1,
    .usable = 1,
};
long ldt_rc = INLINE_SYSCALL_SET_THREAD_AREA(&ldt);
uint16_t fs_sel = ((uint16_t)ldt.entry_number << 3) | 3;
__asm__ volatile("mov %0, %%fs" : : "r"(fs_sel));
```

This is the same mechanism Wine uses for 32-bit processes on 64-bit kernels.
`arch_prctl(ARCH_SET_FS)` returns `EINVAL` in 32-bit mode, so the LDT approach
is the only option.

### 3.5 PEB LDR

**File:** `src/loader/peb_ldr.c`

`init_peb_ldr()` allocates a `PEB_LDR_DATA` structure with three circular
doubly-linked lists:

- `InLoadOrderModuleList` — order in which modules were loaded
- `InMemoryOrderModuleList` — order by memory address
- `InInitializationOrderModuleList` — order of `DllMain` initialization

`ldr_add_module(mod)` inserts the module's `LDR_DATA_TABLE_ENTRY` (embedded
in `loaded_module_t`) into all three lists. The entry is populated by
`module_init_ldr_entry()` in `module_list.c` with:
- `DllBase`, `EntryPoint`, `SizeOfImage`, `TimeDateStamp`
- `FullDllName` and `BaseDllName` (as `LDR_UNICODE_STRING`)

`ldr_find_by_addr(addr)` walks the memory-order list to find which module
contains a given address.

### 3.6 Module List

**File:** `src/loader/module_list.c`

The module registry (`g_loader.modules[MAX_MODULES]`) tracks all loaded PE images.
Each `loaded_module_t` contains:
- `base`, `name[]`, `nt` (NT headers pointer)
- `load_count`, `ldr_linked`, `dllmain_called`
- Embedded `LDR_DATA_TABLE_ENTRY` (for PEB LDR linkage)
- Embedded `EXPORT_CACHE` (for export table lookup)

`add_module()` finds a free slot, initializes the LDR entry, and increments
`g_loader.module_count`. `find_module_by_name()` and `find_module_by_addr()`
search the array. A `*_safe` variant avoids glibc/PLT calls for post-GS context.

---

## 4. Entry Point

**Files:** `src/run_guest.S` · `src/loader/pe32_run_guest.S` · `src/loader/guest_setup.c` ·
`src/loader/entry.c` · `src/loader/crash_handlers.c`

### 4.1 Guest Setup Orchestrator

**File:** `src/loader/guest_setup.c`

`setup_guest_and_run()` is the final orchestrator before jumping to guest code:

```
setup_guest_and_run(entry_abs, image_base, stack_top, teb, argv, envp)
│
├── install_crash_signal_handlers()
│   ├── mmap signal stack
│   ├── sigaltstack
│   └── sigaction(SIGSEGV, SIGILL, SIGABRT, SIGFPE, SIGBUS, SIGTRAP)
│
├── setup_seh_and_thunks()
│   ├── static SEH frame [0, seh_crash_handler]
│   ├── generate_all_thunks()
│   └── setup_unix_stack()
│
├── parse_pe_headers()     /* re-parse from image_base for patches */
│
├── apply_final_patches()
│   ├── patch_acrt_iob()  /* fix __acrt_iob_func thunk */
│   └── mprotect .bss sections writable
│
├── finalize_guest_state()
│   ├── save host GS base
│   ├── set_gs_base(teb)  /* or set_fs for PE32 */
│   └── TEB[SEH_CHAIN] = seh_frame
│
└── jump_to_guest()
    └── run_guest(entry, stack_top, ..., exit_process_fn)  /* noreturn */
```

### 4.2 `__acrt_iob_func` Patch

**File:** `src/loader/guest_setup.c`

The msvcrt `__acrt_iob_func` wrapper has a bug: after calling `__iob_func` (our
stub), it restores `%rcx` from `%ebx`, but the upper 32 bits of `%rcx` remain
garbage. The subsequent `lea (%rcx, %rcx, 2)` and `shl $4` produce an incorrect
offset. The fix patches the thunk with:

```
movabs $<__wine_iob_data>, %rax
ret
NOP...                          (15 bytes total)
```

The thunk is found by scanning `.text` for `ff 25` instructions whose IAT target
resolves to `__iob_func`.

### 4.3 Crash Handlers

**File:** `src/loader/crash_handlers.c`

Two crash handler paths:

**SEH handler** (`seh_crash_handler`):
- Called via the Windows SEH chain when an exception occurs in guest code
- Reads the exception record, logs "SEV: SEH handler invoked"
- Calls `INLINE_SYSCALL_EXIT(exit_code)` directly (no glibc)

**POSIX signal handler** (`crash_handler`):
- Handles `SIGSEGV`, `SIGILL`, `SIGABRT`, `SIGFPE`, `SIGBUS`, `SIGTRAP`
- Runs on the alternate signal stack (not the guest stack)
- Validates `ucontext` pointer before reading registers
- Dumps `RIP/RSP/EIP/ESP` + `si_addr` to stderr via raw syscall
- On PE32, `SIGTRAP` (0xCC breakpoint) is resumable — if the trap byte is
  at `EIP-1` inside the image, the handler returns instead of exiting
- Uses `INLINE_SYSCALL_EXIT_GROUP` (not `_exit()`) because glibc TLS may be
  corrupted after GS→TEB switch

The alternate signal stack is allocated with `mmap` (fixed address `0x00800000`
for PE32). If allocation fails, handlers still work but run on the guest stack
with a warning.

### 4.4 PE32+ Stack Switch

**File:** `src/run_guest.S`

The PE32+ entry trampoline switches to the guest stack and calls the entry point:

```asm
run_guest:
    mov %rsi, %rsp           # switch to guest stack (stack_top)
    mov %r9, %r15            # save exit_process_fn in callee-saved reg
    mov %rcx, %r9            # save guest_argv (rcx reused)
    mov $1, %rcx             # argc = 1 (Microsoft x64 ABI: arg1 = rcx)
    mov %r9, %rdx            # guest_argv -> rdx (arg2)
    # guest_envp already in r8 (arg5 in sysv → arg3 in ms_abi)
    call *%rdi               # call entry()
    mov %rax, %rcx           # uExitCode = return value
    call *%r15               # ExitProcess(uExitCode)
    hlt                      # should not reach here
```

Arguments are translated from the Linux sysv ABI (C calling convention) to the
Microsoft x64 ABI: `rcx=argc, rdx=argv, r8=envp`.

### 4.5 PE32 Stack Switch

**File:** `src/loader/pe32_run_guest.S`

The PE32 entry trampoline uses cdecl conventions:

```asm
pe32_run_guest:
    movl 4(%esp), %esi      # entry_abs -> ESI
    movl 8(%esp), %eax      # stack_top
    movl %eax, %esp
    andl $-16, %esp         # 16-byte align
    subl $4, %esp           # space for fake return address
    movl %esp, %ebp         # EBP = ESP (valid frame pointer)
    jmp *%esi               # jump to PE entry

pe32_guest_return_exit:
    movl %eax, %ebx
    movl $252, %eax         # __NR_exit_group (i386)
    int $0x80
```

The `pe32_entry.c` file prepares the stack before the jump:

```c
/* pe32_setup_fs_and_jump in pe32_guest_launch.c */
*(uint32_t *)(sp + 0) = (uint32_t)pe32_guest_return_exit;
if (entry_type == PE32_ENTRY_TYPE_MAIN) {
    *(uint32_t *)(sp + 4)  = 1;               // argc
    *(uint32_t *)(sp + 8)  = pe32_argv_ptr();  // argv
    *(uint32_t *)(sp + 12) = pe32_envp_ptr();  // envp
} else {
    *(uint32_t *)(sp + 4)  = (uint32_t)image_base;  // hInstance
    *(uint32_t *)(sp + 8)  = 0;                  // lpPrevInstance
    *(uint32_t *)(sp + 12) = (uint32_t)cmd_line;  // lpCmdLine
    *(uint32_t *)(sp + 16) = 5;                  // nCmdShow
}
```

### 4.6 Entry Resolution

**File:** `src/loader/pe32_entry_resolve.c`

For PE32 images, the loader resolves the actual entry point:

1. **Check if DOOM95** — if path contains `DOOM95.EXE`, use the PE entry directly
2. **Parse symbol table** — look for CRT entry symbols: `main`, `_main`, `WinMain@16`,
   `WinMain`, `wWinMain@16`, `wWinMain`, `Main@16`, `wMain@16`
3. **Watcom entry extraction** — if no symbol match, scan the entry stub for the
   pattern `c7 05 ... e9/e8 ...` (mov absolute; jmp/call relative) to extract
   the Watcom bootstrap target
4. **Default** — fall back to the PE's `AddressOfEntryPoint`

The entry type determines the argument layout on the guest stack (`main` gets
`argc/argv/envp`; `WinMain` gets `hInstance/prevInstance/cmdLine/cmdShow`).

---

## 5. PE32 vs PE32+

### 5.1 Process Model

| Aspect | PE32+ (`my_wine64`) | PE32 (`my_wine32`) |
|--------|---------------------|---------------------|
| Entry point | `src/main.c` | `src/loader/pe32_entry.c` |
| Compilation | 64-bit native | 32-bit ELF (`-m32`) |
| Linking | Standard | `-no-pie` (PIC not needed for 32-bit) |
| CRT | Host glibc not used for guest | glibc CRT is used for the 32-bit process |
| Process count | 1 | 1 (launched by `my_wine` wrapper) |
| Stack model | Host stack → guest stack switch | Host stack → guest stack switch |

### 5.2 ABI & Segment Register

| Aspect | PE32+ | PE32 |
|--------|-------|------|
| Guest TEB selector | **GS** (via `arch_prctl` or `wrgsbase`) | **FS** (via `set_thread_area` LDT) |
| Guest calling convention | Microsoft x64 (`rcx, rdx, r8, r9`) | Cdecl (stack-based) |
| Syscall instruction | `syscall` | `int $0x80` |
| Stack alignment | `rsp % 16 == 8` after call | `esp % 16 == 0` before call |
| Thunk size | 8 bytes (64-bit addresses) | 4 bytes (32-bit addresses) |
| Address space | Full 48-bit x86_64 | 32-bit (`0x00000000`–`0x7FFFFFFF`) |

### 5.3 Memory Layout

**PE32+ (`my_wine64`):**
```
High memory ──→ [ ... host stuff ... ]
                  [ guest TEB (GS) ]
                  [ guest PEB ]
                  [ guest stack (top) ]  ← RSP
                  [ ... gap ... ]
                  [ PE image at preferred base ]
                  [ thunk pages ]
                  [ signal stack ]
Low memory  ──→ [ my_wine64 binary ]
```

**PE32 (`my_wine32`):**
```
0x7FFFFFFF ──→ [ glibc TLS / stack ]
0x7FFDF000     [ PEB (fixed) ]
0x7FFDE000     [ TEB (fixed) ]
0x00800000     [ signal stack ]
0x00600000     [ UNIX stack ]
0x00500000     [ guest stack ]
0x00400000     [ PE image ]  (default PE32 base)
0x00000000     [ my_wine32 binary ]
```

The PE32 layout is tightly constrained: the guest stack at `0x00500000` sits
between the PE image and the UNIX stack. This is enforced by fixed `MAP_FIXED`
mappings to avoid overlaps.

### 5.4 PE32 Bootstrap Path

**Files:** `src/loader/pe32_bootstrap.c` · `src/loader/pe32_process.c` ·
`src/loader/pe32_guest_launch.c` · `src/loader/pe32_entry_resolve.c` ·
`src/loader/pe32_doom95_compat.c` · `src/loader/pe32_doom95_command.c`

The PE32 bootstrap is modularized into stages:

| Stage | Function | File |
|-------|----------|------|
| Path selection | `pe32_resolve_path_or_null()` | `pe32_bootstrap.c` |
| Runtime config | `pe32_init_runtime_debug_level()` | `pe32_bootstrap.c` |
| Command line | `pe32_seed_command_line()` | `pe32_bootstrap.c` |
| Image map | `pe32_map_image_or_exit()` | `pe32_bootstrap.c` |
| CRT detection | `pe32_activate_crt()` | `pe32_bootstrap.c` |
| Import resolution | `pe32_resolve_imports_or_exit()` | `pe32_bootstrap.c` |
| Entry resolution | `pe32_resolve_entry_symbol()` | `pe32_entry_resolve.c` |
| CRT _initialized | `pe32_patch_crt_initialized()` | `pe32_entry_resolve.c` |
| PEB init | `pe32_init_peb_or_exit()` | `pe32_guest_launch.c` |
| TEB init | `pe32_init_teb_or_exit()` | `pe32_guest_launch.c` |
| PEB fields | `wire_peb32_fields()` | `pe32_process.c` |
| Stack setup | `pe32_setup_guest_stack_or_exit()` | `pe32_guest_launch.c` |
| Dispatcher | `pe32_prepare_dispatch_or_exit()` | `pe32_guest_launch.c` |
| argv setup | `ensure_argv_setup()` | `pe32_process.c` |
| CRT .bss seeding | `pe32_seed_crt_bss()` | `pe32_bootstrap.c` |
| Doom95 compat | `pe32_apply_doom95_runtime_compat()` | `pe32_doom95_compat.c` |
| FS switch + jump | `pe32_setup_fs_and_jump()` | `pe32_guest_launch.c` |

The Doom95 compatibility layer (`pe32_doom95_compat.c`):
- Detects DOOM95.EXE by filename
- Seeds the guest's std handle table (stdin/stdout/stderr)
- Patches the trap flag at `0x077d84`
- Seeds the command array with `-basewad DOOM1.WAD` (from `pe32_doom95_command.c`)

This path is unique to the DOOM95 sample and is intentionally isolated from the
generic PE32 launch logic.

### 5.5 Threading Safety

Both PE32 and PE32+ use spinlocks (`wine_spinlock_t`) for shared state (handle
manager, heap). Pthreads and glibc mutexes are excluded from the 32-bit build
because glibc uses GS-relative TLS access, which would break after the segment
switch to the TEB. The 64-bit build gates pthread usage behind
`#ifndef MY_WINE32` in `wine_heap.c`.

### 5.6 Guest Cleanup

**File:** `src/loader/guest_setup.c`

After the guest exits (via `ExitProcess` → `NtTerminateProcess`), `cleanup_guest()`:
- Unmaps the PEB page
- Unmaps the TEB page
- Unmaps the guest stack
- Calls `cleanup_thunk_pages()` to release syscall thunk pages

All cleanup uses `INLINE_SYSCALL_MUNMAP` (direct syscall, no glibc) because
GS/FS point to the TEB at this stage.

---

## Cross-Reference Summary

| Concept | Primary File(s) |
|---------|----------------|
| Image mapping | `src/loader/image_mapper.c` |
| Relocations | `src/loader/relocations.c` |
| Pass 1 IAT patching | `src/loader/import_resolve.c` |
| Pass 2 thunk scanning | `src/loader/import_resolve.c` · `src/pe_rip_scan.c` |
| Thunk patch strategies | `src/loader/import_flat.c` |
| Symbol lookup | `src/loader/import_lookup.c` |
| Static import table | `src/loader/import_table.c` |
| Dynamic msvcrt init | `src/loader/import_init.c` |
| Ordinal lookup | `src/loader/ordinal_table.c` |
| DLL loading | `src/loader/dll_loader.c` |
| DLL path search | `src/loader/dll_path.c` |
| Export table parsing | `src/loader/export_table.c` |
| TEB/PEB allocation | `src/loader/teb_peb.c` |
| GS base setting | `src/loader/gs_base.c` |
| PEB LDR management | `src/loader/peb_ldr.c` |
| Module registry | `src/loader/module_list.c` |
| Guest setup orchestrator | `src/loader/guest_setup.c` |
| PE32+ stack switch | `src/run_guest.S` |
| PE32 stack switch | `src/loader/pe32_run_guest.S` |
| Entry point (PE32+) | `src/loader/entry.c` · `src/main.c` |
| Crash handlers | `src/loader/crash_handlers.c` |
| PE32 entry point | `src/loader/pe32_entry.c` |
| PE32 bootstrap | `src/loader/pe32_bootstrap.c` |
| PE32 process params | `src/loader/pe32_process.c` |
| PE32 guest launch | `src/loader/pe32_guest_launch.c` |
| PE32 entry resolution | `src/loader/pe32_entry_resolve.c` |
| PE32 Doom95 compat | `src/loader/pe32_doom95_compat.c` |
| PE32 Doom95 command | `src/loader/pe32_doom95_command.c` |
