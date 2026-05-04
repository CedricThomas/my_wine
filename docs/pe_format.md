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
