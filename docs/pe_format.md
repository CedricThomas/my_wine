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

Each section is mapped by calling `mmap` with a virtual address
derived from its RVA and a protection mode derived from its
Characteristics:

```
  mmap(image_base + VirtualAddress, VirtualSize, PROT from Characteristics)
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

See [architecture.md](architecture.md) §1.1 for the mapping process.
```

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

### IAT Patching

Before the loader runs, the IAT is a copy of the ILT — each entry
points to the same lookup data. During loading:

1. The loader reads each `IMAGE_IMPORT_DESCRIPTOR`, resolves the DLL
   name, and loads the DLL.
2. For each ILT entry, the loader looks up the function (by name
   or ordinal) in the DLL's export table.
3. The corresponding IAT entry is **patched** with the actual
   function address.

Guest code calls through the IAT:

```
  call [IAT_entry]    ; jumps to the resolved function address
```

The IAT lives in a writable section (`.idata` or `.data`) so the
loader can patch it. The ILT remains read-only.

### Import Chain Flow

```
DataDirectory[IMPORT] ──► IMAGE_IMPORT_DESCRIPTOR ──► DLL name ("ntdll.dll")
                              │
                              ├── OriginalFirstThunk ──► ILT (lookup thunks) ──► function names/ordinals
                              │
                              └── FirstThunk ──────────► IAT (writable) ──► [patched by loader]
```

See [architecture.md §1.2](architecture.md) for the import resolution process.
