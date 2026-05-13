/*
 * pe.h — PE32 (x86) and PE32+ (x86_64) Windows PE structures
 *
 * Structures based on the Microsoft PE/COFF specification.
 * See: https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
 */

#ifndef MY_WINE_PE_H
#define MY_WINE_PE_H

#include <stdint.h>

#pragma pack(push, 1)

/* ── Signatures & Constants ────────────────────────────────────── */

#define IMAGE_DOS_SIGNATURE         0x5A4D   /* "MZ" */
#define IMAGE_NT_SIGNATURE          0x4550   /* "PE\0\0" */

#define IMAGE_FILE_MACHINE_AMD64    0x8664
#define IMAGE_FILE_MACHINE_I386     0x14c

#define IMAGE_NT_OPTIONAL_HDR32_MAGIC 0x10B
#define IMAGE_NT_OPTIONAL_HDR64_MAGIC 0x20B

/* ── PE Type ───────────────────────────────────────────────────── */

typedef enum {
    PE_TYPE_32,
    PE_TYPE_64
} pe_type_t;

#define IMAGE_DIRECTORY_ENTRY_EXPORT     0
#define IMAGE_DIRECTORY_ENTRY_IMPORT     1
#define IMAGE_DIRECTORY_ENTRY_BASERELOC  3

#ifndef IMAGE_REL_BASED_DIR64
#define IMAGE_REL_BASED_DIR64       0x000A
#define IMAGE_REL_BASED_ABSOLUTE    0x0000
#define IMAGE_REL_BASED_HIGH        0x0001
#define IMAGE_REL_BASED_LOW         0x0002
#define IMAGE_REL_BASED_HIGHLOW     0x0003
#define IMAGE_REL_BASED_DIR32       0x0004
#endif
#ifndef IMAGE_FILE_RELOCS_STRIPPED
#define IMAGE_FILE_RELOCS_STRIPPED  0x0001
#endif

#define IMAGE_SCN_MEM_READ      0x40000000  /* bit 30 */
#define IMAGE_SCN_MEM_WRITE     0x80000000  /* bit 31 */
#define IMAGE_SCN_MEM_EXECUTE   0x20000000  /* bit 29 */

/* ── DOS Header ────────────────────────────────────────────────── */

typedef struct {
    uint16_t e_magic;       /* MZ signature */
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
    uint32_t e_lfanew;      /* Offset to PE signature */
} IMAGE_DOS_HEADER;

/* ── File Header ───────────────────────────────────────────────── */

typedef struct {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} IMAGE_FILE_HEADER;

/* ── Data Directory ────────────────────────────────────────────── */

typedef struct {
    uint32_t VirtualAddress;
    uint32_t Size;
} IMAGE_DATA_DIRECTORY;

/* ── Optional Header (PE32) ────────────────────────────────────── */

typedef struct {
    uint16_t  Magic;
    uint8_t   MajorLinkerVersion;
    uint8_t   MinorLinkerVersion;
    uint32_t  SizeOfCode;
    uint32_t  SizeOfInitializedData;
    uint32_t  SizeOfUninitializedData;
    uint32_t  AddressOfEntryPoint;
    uint32_t  BaseOfCode;
    uint32_t  BaseOfData;          /* PE32-only */
    uint32_t  ImageBase;
    uint32_t  SectionAlignment;
    uint32_t  FileAlignment;
    uint16_t  MajorOperatingSystemVersion;
    uint16_t  MinorOperatingSystemVersion;
    uint16_t  MajorImageVersion;
    uint16_t  MinorImageVersion;
    uint16_t  MajorSubsystemVersion;
    uint16_t  MinorSubsystemVersion;
    uint32_t  Win32VersionValue;
    uint32_t  SizeOfImage;
    uint32_t  SizeOfHeaders;
    uint32_t  CheckSum;
    uint16_t  Subsystem;
    uint16_t  DllCharacteristics;
    uint32_t  SizeOfStackReserve;
    uint32_t  SizeOfStackCommit;
    uint32_t  SizeOfHeapReserve;
    uint32_t  SizeOfHeapCommit;
    uint32_t  LoaderFlags;
    uint32_t  NumberOfRvaAndSizes;
    IMAGE_DATA_DIRECTORY DataDirectory[16];
} IMAGE_OPTIONAL_HEADER32;

/* ── Optional Header (PE32+) ───────────────────────────────────── */

typedef struct {
    uint16_t  Magic;
    uint8_t   MajorLinkerVersion;
    uint8_t   MinorLinkerVersion;
    uint32_t  SizeOfCode;
    uint32_t  SizeOfInitializedData;
    uint32_t  SizeOfUninitializedData;
    uint32_t  AddressOfEntryPoint;
    uint32_t  BaseOfCode;
    uint64_t  ImageBase;
    uint32_t  SectionAlignment;
    uint32_t  FileAlignment;
    uint16_t  MajorOperatingSystemVersion;
    uint16_t  MinorOperatingSystemVersion;
    uint16_t  MajorImageVersion;
    uint16_t  MinorImageVersion;
    uint16_t  MajorSubsystemVersion;
    uint16_t  MinorSubsystemVersion;
    uint32_t  Win32VersionValue;
    uint32_t  SizeOfImage;
    uint32_t  SizeOfHeaders;
    uint32_t  CheckSum;
    uint16_t  Subsystem;
    uint16_t  DllCharacteristics;
    uint64_t  SizeOfStackReserve;
    uint64_t  SizeOfStackCommit;
    uint64_t  SizeOfHeapReserve;
    uint64_t  SizeOfHeapCommit;
    uint32_t  LoaderFlags;
    uint32_t  NumberOfRvaAndSizes;
    IMAGE_DATA_DIRECTORY DataDirectory[16];
} IMAGE_OPTIONAL_HEADER64;

/* ── NT Headers (PE32) ─────────────────────────────────────────── */

typedef struct {
    uint32_t              Signature;
    IMAGE_FILE_HEADER     FileHeader;
    IMAGE_OPTIONAL_HEADER32 OptionalHeader;
} IMAGE_NT_HEADERS32;

/* ── NT Headers (PE32+) ────────────────────────────────────────── */

typedef struct {
    uint32_t          Signature;
    IMAGE_FILE_HEADER FileHeader;
    IMAGE_OPTIONAL_HEADER64 OptionalHeader;
} IMAGE_NT_HEADERS64;

/* ── NT Headers (tagged union) ─────────────────────────────────── */

typedef struct {
    pe_type_t pe_type;
    union {
        IMAGE_NT_HEADERS32 nt32;
        IMAGE_NT_HEADERS64 nt64;
    } u;
} IMAGE_NT_HEADERS;

/* ── Section Header ────────────────────────────────────────────── */

typedef struct {
    uint8_t  Name[8];
    union {
        uint32_t PhysicalAddress;
        uint32_t VirtualSize;
    } Misc;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
} IMAGE_SECTION_HEADER;

/* ── Import Descriptor (20 bytes, same for PE32 and PE32+) ───────
 *
 * Per the Microsoft PE/COFF specification, all fields are 32-bit RVAs
 * regardless of PE type. OriginalFirstThunk and FirstThunk are RVAs
 * that point to thunk arrays: IMAGE_THUNK_DATA32 (4-byte) for PE32,
 * IMAGE_THUNK_DATA64 (8-byte) for PE32+.
 */

typedef struct {
    union {
        uint32_t Characteristics;
        uint32_t OriginalFirstThunk; /* RVA to ILT */
    } u1;
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;                   /* RVA to DLL name string */
    uint32_t FirstThunk;             /* RVA to IAT */
} IMAGE_IMPORT_DESCRIPTOR;

/* ── Import By Name ────────────────────────────────────────────── */

typedef struct {
    uint16_t Hint;
    char     Name[1]; /* null-terminated */
} IMAGE_IMPORT_BY_NAME;

/* ── Thunk Data (32-bit) ───────────────────────────────────────── */

typedef union {
    uint32_t ForwarderString;
    uint32_t Function;
    uint32_t Ordinal;        /* high bit set when this is an ordinal */
    uint32_t AddressOfData;
} IMAGE_THUNK_DATA32;

/* ── Thunk Data (64-bit) ───────────────────────────────────────── */

typedef union {
    uint64_t ForwarderString;
    uint64_t Function;
    uint64_t Ordinal;        /* high bit set when this is an ordinal */
    uint64_t AddressOfData;
} IMAGE_THUNK_DATA64;

/* ── Relocation ────────────────────────────────────────────────── */

/* Each relocation entry is a single 16-bit value: bits 0-11 = offset,
   bits 12-15 = type. See PE/COFF specification. */
typedef uint16_t IMAGE_RELOC_ENTRY;

typedef struct {
    uint32_t virtualAddress;
    uint32_t sizeOfBlock;
    IMAGE_RELOC_ENTRY entries[]; /* flexible array */
} IMAGE_BASE_RELOCATION;

#define IMAGE_REL_ENTRY_OFFSET(entry)  ((uint16_t)((entry) & 0x0FFF))
#define IMAGE_REL_ENTRY_TYPE(entry)    ((uint16_t)(((entry) >> 12) & 0xF))

/* ── Export Directory ──────────────────────────────────────────── */

typedef struct {
    uint32_t Characteristics;
    uint32_t TimeDateStamp;
    uint16_t MajorVersion;
    uint16_t MinorVersion;
    uint32_t Name;
    uint32_t Base;
    uint32_t NumberOfFunctions;
    uint32_t NumberOfNames;
    uint32_t AddressOfFunctions;
    uint32_t AddressOfNames;
    uint32_t AddressOfNameOrdinals;
} IMAGE_EXPORT_DIRECTORY;

/* ── TLS Directory (64-bit) ────────────────────────────────────── */

typedef struct {
    void *StartAddress;
    void *EndAddress;
    void *ZeroFill;
    void *Callback; // actually pointer to array of callbacks
    uint32_t SizeOfZeroFill;
    uint32_t Characteristics;
} IMAGE_TLS_DIRECTORY64;

/* ── COFF Symbol Table ─────────────────────────────────────────── */

#define IMAGE_SYM_UNDEFINED          0
#define IMAGE_SIZEOF_SYMBOL          18
#define IMAGE_SIZEOF_SHORT_NAME      8

typedef struct {
    union {
        uint8_t  ShortName[IMAGE_SIZEOF_SHORT_NAME];
        struct {
            uint32_t Short;
            uint32_t Long;  /* offset into string table */
        } Name;
    } N;
    uint32_t Value;
    int16_t  SectionNumber;
    uint16_t Type;
    uint8_t  StorageClass;
    uint8_t  NumberOfAuxSymbols;
} IMAGE_SYMBOL;

/* Common storage classes */
#define IMAGE_SYM_CLASS_EXTERNAL     2
#define IMAGE_SYM_CLASS_STATIC       3

/* Section types */
#define IMAGE_SYM_CLASS_END_OF_FUNCTION ((uint8_t)-1)

#pragma pack(pop)

#endif /* MY_WINE_PE_H */
