# PE Format Primer

Understanding the Portable Executable format — what the loader reads and why it matters.

---

## 1. DOS Header

Every PE file begins with a minimal DOS (MS-DOS) header. Its sole
purpose in modern times is to identify the file as a PE and point
to the real headers.

### MZ Magic

The first two bytes are always `0x4D 0x5A` — the ASCII characters
`M` and `Z` (after Tom Peterlin, a Microsoft engineer). This is
the **only** way a loader identifies a file as a PE candidate.

```
  Offset    Value    Meaning
  ──────    ─────    ───────
  0x0000    0x5A4D   e_magic  ("MZ" in little-endian)
```

Note the endianness: on disk the bytes are `4D 5A` (`M` `Z`), but
as a `uint16_t` in little-endian x86, that reads as `0x5A4D`.

### e_lfanew — Offset to PE Signature

The most important field in the DOS header is `e_lfanew` at
offset `0x3C`. It is a **file offset** (not an RVA) to the PE
signature:

```
  e_lfanew (offset 0x3C in the file) ──► "PE\0\0"
```

**Key point:** `e_lfanew` is a raw byte offset from the beginning
of the file. It is **not** relative to any base address. You add
it directly to the file pointer to reach the PE signature.

### Struct Sketch

```c
typedef struct {
    uint16_t e_magic;     // 0x5A4D ("MZ")
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;    // file offset to PE signature
} IMAGE_DOS_HEADER;
```

Only `e_magic` and `e_lfanew` matter for the loader. The rest is
legacy DOS stub baggage.

### Full Layout Chain

From DOS header to section table — the complete header chain:

```
File Offset 0:
  ┌─────────────────────┐
  │  DOS Header (MZ)    │  0x3C → e_lfanew
  │  ...                │
  └──────────┬──────────┘
             │ e_lfanew
             ▼
File Offset e_lfanew:
  ┌──────────────────────┐
  │  "PE\0\0"            │ ← PE Signature (4 bytes)
  ├──────────────────────┤
  │  IMAGE_FILE_HEADER   │
  ├──────────────────────┤
  │  IMAGE_OPTIONAL      │
  │  HEADER64            │
  ├──────────────────────┤
  │  Section Table [...] │ ← one IMAGE_SECTION_HEADER per section
  └──────────────────────┘
```

---

## 2. NT Headers

Starting at the file offset given by `e_lfanew`, the NT headers
describe the actual executable:

```
  File layout at offset e_lfanew:

  ┌──────────────────────┐
  │  "PE\0\0"            │  ← PE Signature (4 bytes)
  ├──────────────────────┤
  │  IMAGE_FILE_HEADER   │  ← Machine, sections count, etc.
  ├──────────────────────┤
  │  IMAGE_OPTIONAL      │  ← Magic, entry point, sizes, ...
  │  HEADER64            │
  ├──────────────────────┤
  │  Section Table [...] │  ← one IMAGE_SECTION_HEADER per section
  └──────────────────────┘
```

### PE Signature

Four bytes: `0x50 0x45 0x00 0x00` — ASCII `PE` followed by two
null bytes. The loader validates this immediately after jumping to
`e_lfanew`.

```c
#define PE_SIGNATURE  0x00004550  // "PE\0\0" in little-endian
```

### IMAGE_FILE_HEADER

The file header describes the binary's architecture and layout:

```c
typedef struct {
    uint16_t Machine;              // 0x8664 = x86_64, 0x14c = x86_32
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} IMAGE_FILE_HEADER;
```

| Field | Common Values | Meaning |
|---|---|---|
| `Machine` | `0x8664` | x86_64 (64-bit) |
| `Machine` | `0x14c` | x86 (32-bit) |
| `NumberOfSections` | varies | count of `IMAGE_SECTION_HEADER` entries |
| `SizeOfOptionalHeader` | `0xF0` (PE32+), `0xE0` (PE32) | size of the optional header that follows |

### IMAGE_OPTIONAL_HEADER64

The optional header is not optional — it contains the fields the
loader actually uses to map the image into memory:

```c
typedef struct {
    uint16_t Magic;                // 0x20B = PE32+, 0x10B = PE32
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;  // RVA of the entry point
    uint32_t BaseOfCode;
    uint64_t ImageBase;            // preferred load address
    uint32_t SectionAlignment;     // alignment in memory (usually 0x1000)
    uint32_t FileAlignment;        // alignment in file (usually 0x200)
    // ... OS/version fields ...
    uint32_t SizeOfImage;          // total mapped size in bytes
    uint32_t SizeOfHeaders;        // DOS + NT + section table size
    uint32_t CheckSum;
    uint16_t Subsystem;            // 3 = WINDOWS_CUI, 2 = WINDOWS_GUI
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    // IMAGE_DATA_DIRECTORY entries follow ...
} IMAGE_OPTIONAL_HEADER64;
```

| Field | Values | Meaning |
|---|---|---|
| `Magic` | `0x20B` | PE32+ (64-bit) |
| `Magic` | `0x10B` | PE32 (32-bit) |
| `AddressOfEntryPoint` | RVA | relative virtual address of `mainCRTStartup` or equivalent |
| `ImageBase` | virtual address | preferred load address (e.g., `0x140000000`) |
| `SectionAlignment` | typically `0x1000` | sections aligned to page boundaries in memory |
| `FileAlignment` | typically `0x200` | sections aligned to 512-byte boundaries in file |
| `SizeOfImage` | varies | `mmap` this many bytes for the image |
| `SizeOfStackReserve` | varies | stack reservation (usually 8 MB) |

### Full NT Headers Layout

```
  Offset e_lfanew
  │
  ├── 0x00  "PE\0\0"              ← IMAGE_NT_HEADERS::Signature
  │
  ├── 0x04  IMAGE_FILE_HEADER      ← Machine, NumberOfSections, ...
  │
  ├── 0x18  IMAGE_OPTIONAL_HEADER64 ← Magic, ImageBase, EntryPoint, ...
  │
  └── 0x108 (or 0xF8 for PE32)
       Section Table [...]          ← IMAGE_SECTION_HEADER × NumberOfSections
```

**Offset arithmetic:** the section table starts at `e_lfanew + 4 + 20 + SizeOfOptionalHeader`.
- PE32+ (`Magic` `0x20B`): `SizeOfOptionalHeader` = `0xF0` → `0x18 + 0xF0 = 0x108`
- PE32 (`Magic` `0x10B`): `SizeOfOptionalHeader` = `0xE0` → `0x18 + 0xE0 = 0xF8`

Each `IMAGE_SECTION_HEADER` is 40 bytes (`sizeof(IMAGE_SECTION_HEADER)`).
The section table begins immediately after the optional header at:

```
  section_table_offset = e_lfanew
                       + sizeof(uint32_t)       // PE signature
                       + sizeof(IMAGE_FILE_HEADER)
                       + SizeOfOptionalHeader
```

---

## 3. Sections

The section table describes each named region of the image. Each entry
is an `IMAGE_SECTION_HEADER` (40 bytes) that tells the loader how to
map a region from the file into memory.

### Common Sections

| Section | Content | Characteristics |
|---|---|---|
| `.text` | Executable code | `IMAGE_SCN_MEM_READ \| IMAGE_SCN_MEM_EXECUTE` |
| `.data` | Initialized globals, statics | `IMAGE_SCN_MEM_READ \| IMAGE_SCN_MEM_WRITE` |
| `.bss` | Uninitialized globals (zero-filled) | `IMAGE_SCN_MEM_READ \| IMAGE_SCN_MEM_WRITE` |
| `.refptr` | Global Pointer to Data (GCC/MinGW) | `IMAGE_SCN_MEM_READ \| IMAGE_SCN_MEM_WRITE` |

### Struct Sketch

```c
typedef struct {
    char Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;     // RVA
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;   // file offset
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;     // IMAGE_SCN_MEM_READ|WRITE|EXECUTE
} IMAGE_SECTION_HEADER;
```

| Field | Meaning |
|---|---|
| `Name` | Section name, padded to 8 bytes (e.g., `.text\0\0\0`)|
| `VirtualSize` | Size of the section in memory (after alignment) |
| `VirtualAddress` | RVA where the section is placed in the mapped image |
| `SizeOfRawData` | Size of the section's raw data in the file |
| `PointerToRawData` | File offset of the section's raw data |
| `Characteristics` | Bit flags controlling memory protection and content |

### Mapping into Memory

The loader maps the PE image into memory via `map_image()` in
`image_mapper.c`. The process:

1. **Open the PE file** on disk and `mmap` it read-only (`MAP_PRIVATE`).
2. **Parse headers** (DOS header, NT headers, section table) from
   the file mapping using `parse_dos_header()`, `parse_nt_headers()`,
   and `parse_sections()` from `pe_headers.c`.
3. **Map the image region** with `mmap` at the preferred `ImageBase`:
   ```c
   mmap((void *)image_base, image_size,
        PROT_READ|PROT_WRITE|PROT_EXEC,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
   ```
   If `MAP_FIXED` fails (e.g., the address range is already in use),
   fall back to `MAP_STACK`:
   ```c
   mmap(NULL, image_size,
        PROT_READ|PROT_WRITE|PROT_EXEC,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
   ```
4. **Copy raw section data** from the file mapping into the image:
   for each section with `SizeOfRawData > 0`, `memcpy` from
   `file_base + PointerToRawData` to `image_base + VirtualAddress`.
   Sections with `SizeOfRawData == 0` (like `.bss`) are already
   zero-filled by the anonymous `mmap`.
5. **Copy PE headers** (`SizeOfHeaders` bytes) from file_base into
   the image base, so the PE headers live at the same virtual addresses
   within the mapped image.
6. **Set per-section protections** with `mprotect`: decode the
   `Characteristics` bits (`IMAGE_SCN_MEM_READ`, `IMAGE_SCN_MEM_WRITE`,
   `IMAGE_SCN_MEM_EXECUTE`) and translate to `PROT_READ | PROT_WRITE | PROT_EXEC`.
7. **Unmap** the original file mapping (no longer needed).

```
  mmap(image_base, SizeOfImage, RWX, MAP_FIXED)

  For each section with SizeOfRawData > 0:
    memcpy(image_base + VirtualAddress,
           file_base + PointerToRawData,
           SizeOfRawData)

  memcpy(image_base, file_base, SizeOfHeaders)

  For each section:
    mprotect(image_base + VirtualAddress, page-aligned size,
             PROT from Characteristics)
```

| Section | PROT flags |
|---|---|
| `.text` | `PROT_READ \| PROT_EXEC` |
| `.data` | `PROT_READ \| PROT_WRITE` |
| `.bss` | `PROT_READ \| PROT_WRITE` (zero-filled beyond raw data) |
| `.refptr` | `PROT_READ \| PROT_WRITE` |

**Important:** `.bss` sections may have `SizeOfRawData=0` but
`VirtualSize>0`. There is no data in the file — the section exists
only in memory and must be zero-filled up to `VirtualSize`.

### File Layout vs. Memory Layout

Section alignment in memory (typically 4K / `0x1000`) differs from
file alignment (typically 512B / `0x200`). Raw data is packed more
tightly on disk.

```
File Layout (disk)                  Memory Layout (RVA-aligned)
┌─────────────┐                    ┌─────────────┐
│ .text       │  PointerToRawData  │ .text       │  VirtualAddress (RVA)
│ 0x1000-2FFF │ ─────────────────► │ 0x1000-2FFF │  (4K aligned)
├─────────────┤                    ├─────────────┤
│ .data       │  PointerToRawData  │ .data       │  VirtualAddress (RVA)
│ 0x3000-3FFF │ ─────────────────► │ 0x3000-3FFF │
├─────────────┤                    ├─────────────┤
│ .bss (0 B)  │  no raw data       │ .bss        │  zero-filled
│             │                    │ 0x4000-4FFF │  (VirtualSize > 0)
└─────────────┘                    └─────────────┘
```

**Key differences:**
- **On disk**: sections are `FileAlignment`-aligned (`0x200`), raw
  data is contiguous and may be smaller than `VirtualSize`
- **In memory**: sections are `SectionAlignment`-aligned (`0x1000`),
  each starts at `image_base + VirtualAddress`, padded to page size
- **.bss**: `PointerToRawData` and `SizeOfRawData` may both be zero;
  the loader allocates `VirtualSize` bytes and fills them with zeros

---

## 4. Imports

Windows executables rely on imported functions from DLLs. The import
chain tells the loader which DLLs to load and which symbols to
resolve.

### The Import Chain

The entry point is `OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]`,
which holds the RVA of an array of `IMAGE_IMPORT_DESCRIPTOR` entries.
The array is NULL-terminated (all fields zero).

```c
typedef struct _IMAGE_IMPORT_DESCRIPTOR {
    union {
        uint32_t Characteristics;
        uint32_t OriginalFirstThunk;  // RVA of ILT (import lookup table)
    } DUMMYUNIONNAME;
    uint32_t TimeDateStamp;           // 0 before loading, DLL timestamp after
    uint32_t ForwarderChain;
    uint32_t Name;                    // RVA to DLL name (e.g., "ntdll.dll")
    uint32_t FirstThunk;              // RVA of IAT (import address table)
} IMAGE_IMPORT_DESCRIPTOR;
```

| Field | Meaning |
|---|---|
| `OriginalFirstThunk` | RVA to the **ILT** (import lookup table) — the read-only lookup thunks |
| `FirstThunk` | RVA to the **IAT** (import address table) — the writable call targets |
| `Name` | RVA to a null-terminated DLL name string |

The ILT is used by the loader to find what to import; the IAT is what
guest code actually calls through.

### Thunk Entries

Both the ILT and IAT are arrays of `IMAGE_THUNK_DATA64` entries.
Each entry is either an ordinal import or a name import, distinguished
by the highest bit (bit 63):

```c
typedef struct _IMAGE_THUNK_DATA64 {
    union {
        uint64_t ForwarderString;    // RVA to forwarder string
        uint64_t Function;           // direct address (after loader patches IAT)
        uint64_t Ordinal;            // if bit63 set: ordinal import
        uint64_t AddressOfData;      // if bit63 clear: RVA to IMAGE_IMPORT_BY_NAME
    } u1;
} IMAGE_THUNK_DATA64;
```

- **Bit 63 set** (ordinal import): the lower 16 bits contain the
  ordinal number. The DLL is called by ordinal, not by name.
- **Bit 63 clear** (name import): the value is an RVA pointing to an
  `IMAGE_IMPORT_BY_NAME` structure in the data directory.

```c
typedef struct _IMAGE_IMPORT_BY_NAME {
    uint16_t Hint;       // optional hint for faster lookup
    char Name[1];        // null-terminated function name
} IMAGE_IMPORT_BY_NAME;
```

### IAT Patching in my_wine

Before the loader runs, the IAT is a copy of the ILT — each entry
points to the same lookup data. During loading, my_wine patches every
IAT entry so guest code jumps to the right target. The resolution
happens in two passes (`import_resolve.c`):

**Pass 1** — Walk each `IMAGE_IMPORT_DESCRIPTOR`, read each ILT entry,
look up the function name in my_wine's static `import_table`
(`import_table.c`), and write the resolved address into the IAT
(`FirstThunk`). The `import_table` maps DLL+function names to
concrete addresses:

- **kernel32.dll** / **msvcrt.dll** imports → **stub functions** written
  in C (e.g., `GetStdHandle`, `WriteFile`, `__iob_func`, `malloc`)
- **ntdll.dll** imports → **dynamically generated 23-byte thunks**
  (`thunk_gen.c`) that call `__wine_dispatcher` to execute the
  corresponding NT syscall

Each thunk is 23 bytes of machine code living in a single `mmap`d
executable blob:

```
  Offset 0-1:   push rdi          (save original RDI)
  Offset 2-8:   mov rdi, imm32    (set NT syscall number)
  Offset 9-18:  mov rax, imm64    (load dispatcher address, absolute)
  Offset 19-20: call rax          (enter dispatcher via indirect call)
  Offset 21:    pop rdi           (restore original RDI)
  Offset 22:    ret               (return to guest caller)
```

The absolute indirect call (`mov rax; call rax`) is used instead of a
relative call (`E8 displacement`) to handle ASLR — the thunk blob and
dispatcher can be more than 2 GB apart when independently randomized.

**Pass 2** — Scan `.text` for `ff 25 disp32` (RIP-relative jump thunks)
that may reference IAT addresses not covered by Pass 1 (non-standard
import layouts). Uses four matching strategies to patch any remaining
mismatches.

```
DataDirectory[IMPORT] ──► IMAGE_IMPORT_DESCRIPTOR ──► DLL name ("ntdll.dll")
                              │
                              ├── OriginalFirstThunk ──► ILT (lookup thunks) ──► function names/ordinals
                              │
                              └── FirstThunk ──────────► IAT (writable) ──► patched to stub or thunk
```

Guest code calls through the IAT:

```asm
  call [IAT_entry]    ; jumps to our stub function or 23-byte thunk
```

The IAT lives in a writable section (`.idata` or `.data`) so the
loader can patch it. The ILT remains read-only.

---

## 5. RVA vs File Offsets

The PE format uses two different address spaces — **RVA** for the
mapped image in memory and **file offsets** for the data on disk.
Confusing them is the most common source of bugs in PE parsers.

### What is an RVA?

**RVA** (Relative Virtual Address) is an offset from `ImageBase`.
It is **not** an absolute virtual address. To get the actual address
where the loader maps data:

```
  absolute_address = ImageBase + RVA
```

Almost every address stored inside the PE headers is an RVA:
`AddressOfEntryPoint`, `VirtualAddress` in section headers, and
every entry in the `DataDirectory`.

### What is a File Offset?

A **file offset** is the raw byte position in the file on disk,
starting from the very first byte (`0x0000`). It has nothing to do
with `ImageBase` or memory layout. `e_lfanew` and `PointerToRawData`
are file offsets.

### Converting RVA to File Offset

To read data from the file when you only have an RVA, you must:

1. **Find the containing section** — scan the section table for the
   first entry where the RVA falls within its virtual range.
2. **Compute the offset** — map from the section's virtual address
   to its file position.

```
  For each section:
    if (RVA >= section.VirtualAddress) &&
       (RVA < section.VirtualAddress + section.VirtualSize):

      file_offset = section.PointerToRawData + (RVA - section.VirtualAddress)
      break
```

If no section contains the RVA, the address is in the header region
(RVA < first section's VirtualAddress) — the file offset equals the
RVA directly (headers start at file offset 0).

### C Implementation

The `rva_to_offset()` helper in `pe_priv.h` implements this:

```c
static inline int rva_to_offset(const IMAGE_NT_HEADERS64 *nt,
                                const IMAGE_SECTION_HEADER *sections,
                                uint32_t rva, size_t file_size)
{
    for (each section) {
        if (rva >= sec->VirtualAddress && rva < sec->VirtualAddress + sec->VirtualSize)
            return sec->PointerToRawData + (rva - sec->VirtualAddress);
    }
    /* Header region: before first section */
    if (rva < nt->OptionalHeader.SizeOfHeaders)
        return (int)rva;
    return -1;  /* not mappable */
}
```

### Visual Flow

```
Given RVA = 0x1234

Step 1 — Find the section:

  ┌──────────────────────────────────────────────┐
  │  Section    VA Range         Raw File Offset  │
  │  .text     0x1000-0x1FFF    0x0400           │  ← 0x1234 is here
  │  .data     0x2000-0x2FFF    0x2000           │
  │  .bss      0x3000-0x3FFF    (none)           │
  └──────────────────────────────────────────────┘

Step 2 — Compute file offset:

  file_offset = PointerToRawData + (RVA - VirtualAddress)
              = 0x0400 + (0x1234 - 0x1000)
              = 0x0400 + 0x0234
              = 0x0634

  Read from file at offset 0x0634.
```

**Rules of thumb:**
- `AddressOfEntryPoint`, `VirtualAddress`, DataDirectory entries → **RVA**
- `e_lfanew`, `PointerToRawData`, `PointerToSymbolTable` → **file offset**
- When in doubt: check the PE spec name — "*Address" fields are RVA,
  "*ToRaw*" fields are file offsets

---

## 6. Entry Point

The entry point is where the loader hands control to the program.
In the PE headers it is stored as `AddressOfEntryPoint` in the
`IMAGE_OPTIONAL_HEADER`.

### It Is an RVA

`AddressOfEntryPoint` is an RVA, not an absolute address. The loader
converts it before jumping:

```
  entry_address = ImageBase + AddressOfEntryPoint
```

### What Does It Point To?

For **mingw-w64** compiled executables, `AddressOfEntryPoint`
typically points to `mainCRTStartup` (the C runtime startup
function). This is not `main()` itself — the CRT startup:

1. Initializes the C runtime (global constructors, heap, etc.)
2. Parses `argc`, `argv`, and `envp` from the process environment
3. Calls `main(argc, argv, envp)`

This is the same startup chain used on native Windows.

**Bypassing the CRT:** my_wine can optionally skip the CRT startup
entirely. In `main.c`, the loader parses the COFF symbol table
(`parse_symbol_table_from_file()`) and looks up `main` via
`lookup_symbol_rva()`. If found, it computes `ImageBase + main_RVA`
and jumps directly to `main()`, bypassing `mainCRTStartup`.
This avoids the complexity of stubbing CRT initialization functions
but requires the `.bss` variables (`_argc`, `_argv`, `_environ`) to
be pre-seeded manually (done in `main.c` via `seed_bss_vars()`).

### my_wine Reaches the Entry Point Via `run_guest.S`

my_wine uses a **single-process model** — there is no `fork()`, no
child process. The loader and guest run in the same process. After
all setup is complete, control transfers to the PE entry point
through `run_guest()`, a naked assembly trampoline (`run_guest.S`).

The trampoline is **naked** (no prologue, no epilogue) because any
stack adjustment or register saving would corrupt the guest's
expected state. The guest believes it was started by the Windows
loader and expects the stack and registers in a specific configuration.

`run_guest` remaps the System V calling convention (used by the C
loader) to the Microsoft x64 calling convention (expected by the
guest):

| SysV (C caller) | → | MS x64 (guest) |
|---|---|---|
| `rdi` = entry function | → | `call *rdi` |
| `rsi` = guest stack top | → | `rsp` (switch stack) |
| `rcx` = guest_argv | → | `rdx` (arg2) |
| `r8`  = guest_envp | → | `r8` (arg3, already in place) |
| `r9`  = ExitProcess fn | → | saved in `r15` (callee-saved) |

The trampoline sets `rcx = 1` (argc), passes `guest_argv` and
`guest_envp`, and calls the entry function. After entry returns,
it routes the exit code through `ExitProcess` (saved in `r15`):

```asm
run_guest:
    mov %rsi, %rsp           # switch to guest stack
    mov %r9, %r15            # save ExitProcess in callee-saved r15
    mov %rcx, %r9            # save guest_argv (rcx overwritten)
    mov $1, %rcx             # argc = 1
    mov %r9, %rdx            # guest_argv → rdx
    call *%rdi               # call entry()  (e.g., mainCRTStartup or main)
    mov %rax, %rcx           # exit code = main()'s return value
    call *%r15               # ExitProcess(exit_code)
    hlt                      # should not reach here
```

`ExitProcess` calls `NtTerminateProcess`, which dispatches through
the syscall dispatcher and exits the process. The trampoline never
returns.

The guest setup pipeline (in `guest_setup.c`) executes in this order
before calling `run_guest`:

1. Install signal handlers (`SIGSEGV`, `SIGILL`, etc.)
2. Set up alternate signal stack
3. Generate all syscall thunks (`generate_all_thunks()`)
4. Set up UNIX stack for syscall dispatch
5. Re-parse PE headers from the mapped image
6. Apply final patches (`__acrt_iob_func`)
7. Set GS base to TEB
8. Wire SEH chain into TEB
9. Jump to entry via `run_guest`

### DLLs Have No Entry Point

`AddressOfEntryPoint` can be **zero** for DLLs — they do not have
an entry point in the same way executables do. my_wine only handles
executables, so a zero entry point indicates a malformed or
unsupported binary.

---

## 7. Summary

A quick reference mapping header fields to their meaning and where
my_wine uses them:

| Header Field | Meaning | Where my_wine uses it |
|---|---|---|
| `e_lfanew` (DOS Header) | File offset to PE signature | `pe_headers.c` — find NT headers |
| `Machine` (File Header) | Target architecture (0x8664 = x64) | `pe_headers.c` — validate x86_64 |
| `NumberOfSections` (File Header) | Count of sections in the PE | `pe_headers.c` — parse section table |
| `PointerToSymbolTable` (File Header) | File offset to COFF symbol table | `pe_symbols.c, main.c` — look up `main`, refptrs |
| `SizeOfImage` (Optional Header) | Total mapped size in bytes | `image_mapper.c` — mmap size |
| `SectionAlignment` (Optional Header) | Memory alignment (typically 4K) | `image_mapper.c` — section alignment |
| `FileAlignment` (Optional Header) | File alignment (typically 512B) | `pe_headers.c` — section parsing |
| `AddressOfEntryPoint` (Optional Header) | RVA of code entry point | `main.c` — default jump target (or bypass via COFF) |
| `SizeOfHeaders` (Optional Header) | Size of PE headers to copy | `image_mapper.c` — copy headers into image |
| `DataDirectory[IMPORT]` (Optional Header) | RVA of import descriptor table | `pe_imports.c`, `import_resolve.c` |
| `ImageBase` (Optional Header) | Preferred load address | `image_mapper.c` — mmap target |
| `SizeOfStackReserve/Commit` (Optional Header) | Stack sizing | `teb_peb.c` — allocate guest stack |

---

## Related Documents

- [Onboarding](onboarding.md) — Reading order and project guide
- [Architecture](architecture.md) — How it works: data flow, syscall interception
- [Rationale](rationale.md) — Design decisions, requirements, limitations
- [CRT refptr Patching](refptr.md) — Deep-dive into .refptr section handling
- [README](../README.md) — Build, run, quick start
