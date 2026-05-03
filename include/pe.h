/*
 * pe.h — PE32+ (x86_64 Windows PE) structures
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

#define IMAGE_NT_OPTIONAL_HDR64_MAGIC 0x20B

#define IMAGE_DIRECTORY_ENTRY_IMPORT     1
#define IMAGE_DIRECTORY_ENTRY_BASERELOC  3

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

/* ── NT Headers ────────────────────────────────────────────────── */

typedef struct {
    uint32_t          Signature;
    IMAGE_FILE_HEADER FileHeader;
    IMAGE_OPTIONAL_HEADER64 OptionalHeader;
} IMAGE_NT_HEADERS64;

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

/* ── Import Descriptor ─────────────────────────────────────────── */

typedef struct {
    union {
        uint32_t Characteristics;
        uint32_t OriginalFirstThunk; /* RVA to IAT */
    } u1;
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;            /* RVA to DLL name string */
    uint32_t FirstThunk;      /* RVA to IAT */
} IMAGE_IMPORT_DESCRIPTOR;

/* ── Import By Name ────────────────────────────────────────────── */

typedef struct {
    uint16_t Hint;
    char     Name[1]; /* null-terminated */
} IMAGE_IMPORT_BY_NAME;

/* ── Thunk Data (64-bit) ───────────────────────────────────────── */

typedef union {
    uint64_t ForwarderString;
    uint64_t Function;
    uint64_t Ordinal;        /* high bit set when this is an ordinal */
    uint64_t AddressOfData;
} IMAGE_THUNK_DATA64;

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
