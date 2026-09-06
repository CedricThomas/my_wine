# Glossary

Terminology reference for the PE format, Windows runtime environment, API libraries, and project-specific terms used in **my_wine**.

---

## PE Format

| Term | Description |
|---|---|
| PE | Portable Executable — Windows binary format for executables (`.exe`) and DLLs (`.dll`). Consists of a DOS header, NT headers, sections, and optional data directories. |
| DOS Header | 64-byte legacy header at the start of every PE file. Contains the `"MZ"` signature and the offset (`e_lfanew`) to the NT headers. |
| NT Headers | Follow the DOS header. Contain the PE signature (`\x50\x45\x00\x00`), a `FILE_HEADER` (machine type, section count, etc.), and an `OPTIONAL_HEADER`. |
| IMAGE_NT_HEADERS32 | 32-bit variant of NT headers. Contains `IMAGE_FILE_HEADER` + `IMAGE_OPTIONAL_HEADER32` (32-bit fields: 4-byte pointers, `ImageBase` as `ULONG`). |
| IMAGE_NT_HEADERS64 | 64-bit variant of NT headers. Contains `IMAGE_FILE_HEADER` + `IMAGE_OPTIONAL_HEADER64` (64-bit fields: 8-byte pointers, `ImageBase` as `ULONGLONG`). |
| Section Table | Array of `IMAGE_SECTION_HEADER` entries immediately after the optional header. Each entry describes a section's name, virtual address, virtual size, file offset, raw size, and characteristics (read/write/execute flags). |
| Section | A named segment of the PE image (e.g., `.text`, `.data`, `.rdata`, `.rsrc`). Each section is mapped into memory at its virtual address with the specified protections. |
| Import Descriptor | `IMAGE_IMPORT_DESCRIPTOR` structure in the import data directory. Chains to the next descriptor via `OriginalFirstThunk`/`Name` fields (last entry has zero `OriginalFirstThunk` and zero `Name`). References the DLL name (e.g., `"kernel32.dll"`) and two arrays of thunks. |
| Thunk | Small bridge code or data entry that redirects a call to its target. In PE imports, the **Original Thunk Table (INT)** holds function names/ordinals; the **Import Address Table (IAT)** holds resolved function pointers. Our syscall thunks are 23-byte (x86_64) or 15-byte (x86) machine-code stubs generated at runtime. |
| IAT | Import Address Table — array of function pointers filled by the loader. Each entry points to the actual implementation of an imported function. We resolve IAT entries to our own stub implementations. |
| EAT | Export Address Table — array of function pointers written by a DLL. Contains exported function names, ordinals, and addresses. Used for dynamic resolution via `GetProcAddress`. |
| Export Table | `IMAGE_EXPORT_DIRECTORY` in the export data directory. Holds an array of exported function names, an array of ordinals, and an array of function addresses (the EAT). |
| Base Relocations | `IMAGE_BASE_RELOCATION` entries that allow the loader to adjust addresses when the PE is loaded at a different address than its preferred `ImageBase`. Each block covers a page and lists `RELOC_32` or `RELOC_32_64` fixups with offsets and types. |
| OEP | Original Entry Point — the RVA (relative virtual address) specified in the `AddressOfEntryPoint` field of the optional header. The first instruction the PE expects the loader to execute. |
| RVA | Relative Virtual Address — an address relative to the PE image's `ImageBase`. The loader resolves RVAs to absolute addresses by adding `ImageBase`. |
| COFF Symbols | Legacy debug/symbol data. `IMAGE_SYMBOL` entries in the symbol table provide function names, line numbers, and type information. Optional — not present in retail builds. |
| DLL | Dynamic Link Library — a PE image loaded at runtime (or mapped alongside the main executable). Our loader handles both the main `.exe` and dynamically loaded `.dll` files. |

---

## Windows Environment

| Term | Description |
|---|---|
| PE32 | 32-bit PE format (machine type `IMAGE_FILE_MACHINE_I386`). Uses 4-byte pointers, `FS` segment to access the TEB, and 32-bit calling conventions. Our `my_wine32` backend handles this format. |
| PE32+ | 64-bit PE format (machine type `IMAGE_FILE_MACHINE_AMD64`). Uses 8-byte pointers, `GS` segment to access the TEB, and 64-bit calling conventions (Microsoft x64 ABI). Our `my_wine64` backend handles this format. |
| TEB | Thread Environment Block — per-thread data structure containing thread-local storage, exception chain, FS/GS segment base, SEH handler chain, and various Windows runtime state. On x86_64, accessed via `GS:0`; on x86, via `FS:0`. |
| PEB | Process Environment Block — per-process data structure containing the loaded module list (LDR), process parameters, default heap, and various process-level settings. Pointer stored in `TEB.ProcessEnvironmentBlock`. |
| LDR | Loader Data — `PEB_LDR_DATA` structure inside the PEB. Tracks all loaded modules (DLLs) in three sorted lists (by load order, memory order, init order). Updated when `LoadLibraryA`/`FreeLibraryA` is called. |
| TID | Thread ID — Windows thread identifier, often stored in TEB fields (`ClientId.UniqueThread`). |
| SEH | Structured Exception Handling — Windows mechanism for exception propagation. TEB stores a pointer to the current exception handler chain (`ExceptionList`). Our crash handlers bridge POSIX signals to SEH semantics. |
| TLS | Thread Local Storage — per-thread variable storage provided by Windows. TEB and PEB maintain TLS slots and callback arrays. Currently incomplete in our implementation. |

---

## Windows API

| Term | Description |
|---|---|
| ntdll | `ntdll.dll` — the NT subsystem library. The lowest-level Windows API layer; directly invokes kernel syscalls via `syscall` instructions. All other Windows libraries (kernel32, user32, etc.) call through ntdll. We implement ~25 NT syscall handlers. |
| kernel32 | `kernel32.dll` — core Windows API for process management, file I/O, memory management, console, modules, threads, and synchronization. We implement a subset (e.g., `CreateProcess`, `CreateFileA`, `VirtualAlloc`). |
| user32 | `user32.dll` — Windows GUI subsystem API (windows, messages, dialogs, input). Partially implemented for graphical samples that use message loops and window creation. |
| msvcrt | `msvcrt.dll` — Microsoft C runtime library. Provides standard C functions (`printf`, `malloc`, `exit`, etc.), CRT initialization (`__getmainargs`, `_initterm`), and global state (`__acrt_iob_func`). |
| ddraw | `ddraw.dll` — DirectDraw, the legacy 2D graphics API. Referenced by older games (e.g., DOOM95). Minimal stub support. |
| dsound | `dsound.dll` — DirectSound, the legacy audio API. Minimal stub support. |
| Win32 API | The user-mode Windows application programming interface, collectively covering kernel32, user32, gdi32, advapi32, and other subsystem DLLs. |
| Win64 API | The 64-bit variant of the Win32 API — same logical APIs but compiled for x86_64 with 64-bit pointers and calling convention. |
| MSVCRT ABI | The calling convention used by `msvcrt.dll` functions. In 32-bit mode, cdecl (caller cleans stack); in 64-bit mode, Microsoft x64 ABI (first 4 integer args in RCX/RDX/R8/R9). |
| Syscall ABI | The mechanism by which user-mode transitions to kernel-mode on Windows. On x86_64, `syscall` instruction with `R10` pointing to TEB, `R10+0x0` containing TEB pointer, and `RAX` containing the syscall number. Linux uses `R10` as the 3rd argument — this creates a conflict we work around. |
| NT Syscall Numbers | Integer identifiers for each kernel function (e.g., `NtAllocateVirtualMemory`, `NtTerminateProcess`). On Windows 10 x86_64 these range from 0x03 to 0x5E (see `include/nt_syscalls.def`). Our dispatcher uses a switch-based lookup table over these known numbers — there is no numeric boundary separating "native" from "emulated" syscalls. |

---

## Project Terms

| Term | Description |
|---|---|
| my_wine | The project name. Also the wrapper binary that detects PE type (PE32 vs PE32+) and dispatches to the correct backend via `execvp()`. |
| my_wine64 | The 64-bit PE backend. Handles PE32+ binaries: maps the image, resolves imports, sets up TEB/PEB with GS base, and jumps to the entry point. |
| my_wine32 | The 32-bit PE backend. Handles PE32 binaries: maps the image with 32-bit glibc CRT, resolves imports, sets up TEB/PEB with FS base, and jumps to the entry point. |
| Stub | A minimal implementation of a Windows API function. Stubs are registered in `import_table.c` and resolve IAT entries. They may perform the requested operation (via Linux syscalls or `mprotect`) or return a fixed value. |
| Thunk Generation | Dynamic creation of machine-code bridge stubs (via `thunk_gen.c`). Each thunk encodes the NT syscall number and the address of `__wine_dispatcher`. x86_64 thunks are 23 bytes; x86 thunks are 15 bytes. All thunks live in a single `mmap`'d executable page. |
| Syscall Dispatcher | `__wine_dispatcher` — the central dispatch function that routes NT syscall numbers to their handler implementations. Uses a switch-based lookup table (generated from `nt_syscalls.def`) keyed on specific syscall numbers. Only the ~25 known NT syscalls are handled; any other number hits the default case. |
| .refptr Patching | The process of fixing MinGW-w64's CRT `.refptr` entries. These are relocation entries in the PE image that point to CRT global variables (`__acrt_iob_func`, etc.). We patch them to point to our own global stubs so the guest code sees our implementations instead of unresolved symbols. |
| GS Base | The base address of the GS segment register, set via `arch_prctl(ARCH_SET_GS)` or `wrgsbase`. On x86_64 Linux, the GS segment base points to the TEB so that guest PE code can access `GS:[0]` for thread-local data. Implemented in `gs_base.c` with FSGSBASE fallback. |
| FS Base | The base address of the FS segment register, set via `arch_prctl(ARCH_SET_FS)` or `wrfsbase`. On 32-bit Linux, the FS segment base points to the TEB so that PE32 guest code can access `FS:[0]` for thread-local data. |
| Guest Stack | A separately `mmap`'d stack region for the PE guest code. The loader sets up this stack before jumping to the entry point, so the guest code has its own stack space independent of the host loader's stack. |
| Guest Code | The PE binary's own machine code, running inside our process after the loader completes initialization. Guest code executes in the same address space as the loader but with its own stack and segment registers. |
| Host Code | The loader's own C and assembly code (the code that maps the PE, sets up TEB/PEB, and dispatches syscalls). |
| `__wine_dispatcher` | The main dispatcher function symbol. Referenced by all dynamically generated thunks. Routes syscall numbers to handler functions. |
| Wine Syscall ABI | The calling convention for NT syscalls on x86_64: `RAX` (or `RDI` in our thunks) carries the syscall number, `R10` points to the TEB, and the `syscall` instruction transitions to kernel mode. Our thunks avoid the `syscall` instruction entirely — they call `__wine_dispatcher` in user space, which saves guest state, switches to a UNIX stack, and calls the C handler directly. |
| CRT Patch Policy | The rules for which CRT globals (`.refptr` symbols, `__acrt_iob_func`, etc.) need to be patched and which can remain untouched. Different CRT flavors (MinGW-w64, UCRT, MSVCRT, Watcom) have different symbol layouts. |
| Single-Process Model | The architecture where the loader and guest code run in the same process (vs. Wine's forked-wine-preloader model). The loader maps the PE, patches it, sets up the environment, then transfers control — all within one process. |
| Import Resolution Pass 1 | The first pass of import resolution: iterate the import descriptor chain, find DLL names and function names/ordinals, and fill IAT entries with our stub addresses. |
| Import Resolution Pass 2 | The second pass: scan the code section for `call rel32` instructions that reference thunk addresses in the INT, and patch them to point to our dynamically generated thunk code. |
| Image Mapper | `image_mapper.c` — maps the PE file, copies section data to the preferred image base, applies per-section protections via `mprotect`, then unmaps the original file mapping. |
| RIP-Relative Scan | The process of scanning the `.text` section for `call` instructions with RIP-relative displacements that reference import thunks, so they can be redirected to our generated thunks. Implemented in `pe_rip_scan.c`. |
