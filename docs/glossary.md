# Core CPU / Architecture

| Acronym | Full Name                            | Meaning                                       |
| ------- | ------------------------------------ | --------------------------------------------- |
| ISA     | Instruction Set Architecture         | CPU instruction set and execution model       |
| ABI     | Application Binary Interface         | Binary-level calling/runtime contract         |
| API     | Application Programming Interface    | Source-level interface                        |
| CPU     | Central Processing Unit              | Main processor                                |
| MMU     | Memory Management Unit               | Virtual memory translation hardware           |
| TLB     | Translation Lookaside Buffer         | CPU cache for virtual→physical mappings       |
| SMP     | Symmetric Multiprocessing            | Multi-core shared-memory execution            |
| NUMA    | Non-Uniform Memory Access            | Multi-node memory architecture                |
| SIMD    | Single Instruction Multiple Data     | Vectorized instructions                       |
| FPU     | Floating Point Unit                  | Floating-point execution hardware             |
| SSE     | Streaming SIMD Extensions            | x86 vector instruction family                 |
| AVX     | Advanced Vector Extensions           | Modern x86 vector instructions                |
| CET     | Control-flow Enforcement Technology  | Intel shadow-stack/CF protection              |
| SMEP    | Supervisor Mode Execution Prevention | Kernel protection against user code execution |
| SMAP    | Supervisor Mode Access Prevention    | Kernel protection against user memory access  |
| NX      | No Execute                           | Non-executable memory protection              |

---

# x86 Segmentation / Legacy

| Acronym | Full Name                     | Meaning                              |
| ------- | ----------------------------- | ------------------------------------ |
| GDT     | Global Descriptor Table       | Global x86 segment descriptors       |
| LDT     | Local Descriptor Table        | Per-process segment descriptors      |
| TSS     | Task State Segment            | CPU task/stack metadata structure    |
| TEB     | Thread Environment Block      | Windows per-thread structure         |
| PEB     | Process Environment Block     | Windows process metadata             |
| TCB     | Thread Control Block          | Linux/glibc thread structure         |
| TLS     | Thread Local Storage          | Per-thread global variables          |
| SEH     | Structured Exception Handling | Windows exception system             |
| VEH     | Vectored Exception Handling   | Windows vectored exception callbacks |
| IDT     | Interrupt Descriptor Table    | CPU interrupt handler table          |
| IRQL    | Interrupt Request Level       | Windows interrupt priority system    |

---

# Windows / NT Internals

| Acronym           | Full Name                       | Meaning                              |
| ----------------- | ------------------------------- | ------------------------------------ |
| NT                | New Technology                  | Windows NT architecture family       |
| NTDLL             | NT Layer DLL                    | Lowest userland Windows runtime      |
| Win32             | Windows 32-bit API              | Main Windows userspace API           |
| WOW64             | Windows-on-Windows 64           | 32-bit-on-64-bit compatibility layer |
| IAT               | Import Address Table            | PE imported function table           |
| EAT               | Export Address Table            | PE exported symbol table             |
| RVA               | Relative Virtual Address        | PE-relative memory address           |
| VA                | Virtual Address                 | Runtime virtual memory address       |
| DLL               | Dynamic Link Library            | Windows shared library               |
| EXE               | Executable                      | Windows executable file              |
| COM               | Component Object Model          | Windows binary object model          |
| CLSID             | Class Identifier                | COM object identifier                |
| GUID              | Globally Unique Identifier      | Windows-style UUID                   |
| SID               | Security Identifier             | Windows user/security identity       |
| APC               | Asynchronous Procedure Call     | Deferred thread callback             |
| LPC               | Local Procedure Call            | Windows IPC mechanism                |
| ALPC              | Advanced Local Procedure Call   | Modern LPC                           |
| HANDLE            | Kernel Object Handle            | Windows kernel object reference      |
| KUSER_SHARED_DATA | Kernel User Shared Data         | Shared kernel/user memory page       |
| SSDT              | System Service Descriptor Table | Windows syscall table                |
| DPC               | Deferred Procedure Call         | Deferred kernel execution            |
| IRP               | I/O Request Packet              | Windows driver I/O structure         |

---

# ELF / Linux / Runtime

| Acronym | Full Name                          | Meaning                         |
| ------- | ---------------------------------- | ------------------------------- |
| ELF     | Executable and Linkable Format     | Linux binary format             |
| PIC     | Position Independent Code          | Relocatable executable code     |
| PIE     | Position Independent Executable    | ASLR-compatible executable      |
| GOT     | Global Offset Table                | Runtime-resolved address table  |
| PLT     | Procedure Linkage Table            | Lazy-binding trampolines        |
| ASLR    | Address Space Layout Randomization | Memory randomization protection |
| VDSO    | Virtual Dynamic Shared Object      | Kernel-mapped syscall helper    |
| AUXV    | Auxiliary Vector                   | Kernel startup metadata         |
| CRT     | C Runtime                          | Program startup/runtime layer   |
| CRT0    | C Runtime Zero                     | Startup entry glue              |
| libc    | C Library                          | Standard runtime library        |
| ld.so   | Loader / Dynamic Linker            | ELF dynamic loader              |
| glibc   | GNU C Library                      | GNU/Linux libc implementation   |
| musl    | musl libc                          | Minimal libc implementation     |
| syscall | System Call                        | User→kernel transition          |
| brk     | Break                              | Old heap-growth syscall         |
| mmap    | Memory Map                         | Virtual memory mapping syscall  |
| VMA     | Virtual Memory Area                | Kernel memory region metadata   |
| OOM     | Out Of Memory                      | Memory exhaustion condition     |

---

# Linking / Binary Loading

| Acronym    | Full Name                                     | Meaning                          |
| ---------- | --------------------------------------------- | -------------------------------- |
| REL        | Relocation                                    | Relative relocation type         |
| RELA       | Relocation with Addend                        | ELF relocation format            |
| IFUNC      | Indirect Function                             | Runtime-selected implementation  |
| DT_NEEDED  | Dynamic Table Needed                          | ELF dependency entry             |
| SONAME     | Shared Object Name                            | Shared library identity          |
| RPATH      | Runtime Path                                  | Embedded library search path     |
| PLT/GOT    | Procedure Linkage Table / Global Offset Table | Dynamic linking mechanism        |
| thunk      | —                                             | ABI/redirect bridge code         |
| trampoline | —                                             | Execution redirection helper     |
| loader     | —                                             | Binary mapping/runtime bootstrap |
| relocation | —                                             | Runtime address patching         |

---

# Compilers / Toolchain

| Acronym  | Full Name                                | Meaning                           |
| -------- | ---------------------------------------- | --------------------------------- |
| GCC      | GNU Compiler Collection                  | GNU compiler suite                |
| LLVM     | Low Level Virtual Machine                | Compiler infrastructure           |
| Clang    | C Language Frontend                      | LLVM C/C++ compiler               |
| binutils | Binary Utilities                         | GNU assembler/linker tools        |
| ld       | Linker                                   | Produces executables/shared libs  |
| as       | Assembler                                | Converts assembly to machine code |
| objdump  | Object Dump                              | Binary inspection tool            |
| nm       | Name List                                | Symbol inspection tool            |
| DWARF    | Debugging With Attributed Record Formats | Debug metadata format             |
| PDB      | Program Database                         | Windows debug symbols             |
| LTO      | Link Time Optimization                   | Cross-module optimization         |
| JIT      | Just-In-Time                             | Runtime compilation               |
| AOT      | Ahead-Of-Time                            | Compile before execution          |

---

# Calling Conventions

| Acronym  | Full Name     | Meaning                           |
| -------- | ------------- | --------------------------------- |
| SysV     | System V ABI  | Linux x86-64 calling convention   |
| stdcall  | Standard Call | Old Windows x86 convention        |
| cdecl    | C Declaration | Classic C calling convention      |
| fastcall | Fast Call     | Register-based calling convention |
| thiscall | This Call     | C++ member-call convention        |

---

# Virtualization / Emulation

| Acronym | Full Name                     | Meaning                           |
| ------- | ----------------------------- | --------------------------------- |
| VM      | Virtual Machine               | Virtualized execution environment |
| VMM     | Virtual Machine Monitor       | Hypervisor                        |
| KVM     | Kernel-based Virtual Machine  | Linux virtualization subsystem    |
| QEMU    | Quick Emulator                | CPU/system emulator               |
| TCG     | Tiny Code Generator           | QEMU JIT backend                  |
| VT-x    | Virtualization Technology x86 | Intel virtualization extensions   |
| AMD-V   | AMD Virtualization            | AMD virtualization extensions     |
| VMX     | Virtual Machine Extensions    | Intel virtualization mode         |
| SVM     | Secure Virtual Machine        | AMD virtualization mode           |

---

# Graphics / Game Runtime

| Acronym | Full Name                       | Meaning                        |
| ------- | ------------------------------- | ------------------------------ |
| GPU     | Graphics Processing Unit        | Graphics/compute processor     |
| VRAM    | Video RAM                       | GPU memory                     |
| DMA     | Direct Memory Access            | Device memory transfer         |
| OpenGL  | Open Graphics Library           | Graphics API                   |
| Vulkan  | —                               | Modern graphics/compute API    |
| DXGI    | DirectX Graphics Infrastructure | Windows graphics interface     |
| D3D     | Direct3D                        | Microsoft graphics API         |
| WSI     | Window System Integration       | Vulkan window interface        |
| SDL     | Simple DirectMedia Layer        | Multimedia abstraction library |

---

# Debugging / Reverse Engineering

| Acronym | Full Name                   | Meaning                          |
| ------- | --------------------------- | -------------------------------- |
| RE      | Reverse Engineering         | Binary analysis                  |
| ROP     | Return-Oriented Programming | Gadget-based code reuse          |
| DEP     | Data Execution Prevention   | NX-based protection              |
| CFG     | Control Flow Guard          | Windows indirect-call protection |
| PAC     | Pointer Authentication Code | ARM pointer protection           |
| ptrace  | Process Trace               | Linux debugging syscall          |
| GDB     | GNU Debugger                | GNU debugger                     |
| LLDB    | LLVM Debugger               | LLVM debugger                    |
| IDA     | Interactive DisAssembler    | Reverse engineering suite        |
| POC     | Proof Of Concept            | Demonstration exploit/prototype  |

---

# Wine / Compatibility

| Acronym    | Full Name               | Meaning                          |
| ---------- | ----------------------- | -------------------------------- |
| Wine       | Wine Is Not an Emulator | Windows compatibility layer      |
| Proton     | —                       | Valve Wine gaming stack          |
| PE         | Portable Executable     | Windows binary format            |
| NE         | New Executable          | Old Windows executable format    |
| DOS        | Disk Operating System   | Legacy PC OS                     |
| Winelib    | Wine Library            | Wine-native build system         |
| wineserver | —                       | Wine process coordination daemon |

---

# Networking / Systems

| Acronym | Full Name                     | Meaning                          |
| ------- | ----------------------------- | -------------------------------- |
| IPC     | Inter-Process Communication   | Process messaging                |
| RPC     | Remote Procedure Call         | Remote invocation system         |
| WOL     | Wake-on-LAN                   | Remote power-on packet           |
| TCP     | Transmission Control Protocol | Reliable transport protocol      |
| UDP     | User Datagram Protocol        | Unreliable low-latency transport |
| NAT     | Network Address Translation   | IP translation                   |
| MTU     | Maximum Transmission Unit     | Network packet size              |
| DNS     | Domain Name System            | Internet naming system           |
| HTTP    | HyperText Transfer Protocol   | Web protocol                     |
| HTTPS   | HTTP Secure                   | TLS-secured HTTP                 |

---

# Other Useful Terms / Acronyms 

| Acronym       | Full Name                | Meaning                          |
| ------------- | ------------------------ | -------------------------------- |
| VVAR          | Virtual Variables Page   | Kernel timing helper page        |
| vdso32        | 32-bit VDSO              | Compat syscall helper            |
| vsyscall      | Virtual Syscall          | Old fast syscall mechanism       |
| ELF TLS       | ELF Thread Local Storage | ELF TLS runtime system           |
| rtld          | Runtime Linker           | Another name for ld.so           |
| rtld_fini     | Runtime Linker Finalizer | ld.so cleanup callback           |
| auxv          | Auxiliary Vector         | Startup kernel metadata          |
| canary        | Stack Canary             | Stack overflow protection        |
| shadow stack  | —                        | Protected return-address stack   |
| red zone      | —                        | SysV ABI stack optimization area |
| PLT stub      | —                        | Tiny PLT trampoline entry        |
| syscall thunk | —                        | ABI bridge around syscalls       |

| Acronym / Term | Full Name    | Size / Meaning      |
| -------------- | ------------ | ------------------- |
| bit            | Binary Digit | 1 bit               |
| nibble         | —            | 4 bits              |
| BYTE           | Byte         | 8 bits / 1 byte     |
| WORD           | Word         | 16 bits / 2 bytes   |
| DWORD          | Double Word  | 32 bits / 4 bytes   |
| QWORD          | Quad Word    | 64 bits / 8 bytes   |
| OWORD          | Octa Word    | 128 bits / 16 bytes |
| YWORD          | —            | 256 bits / 32 bytes |
| ZWORD          | —            | 512 bits / 64 bytes |

| Register | Size   |
| -------- | ------ |
| AL / AH  | 8-bit  |
| AX       | 16-bit |
| EAX      | 32-bit |
| RAX      | 64-bit |
| BL / BH  | 8-bit  |
| BX       | 16-bit |
| EBX      | 32-bit |
| RBX      | 64-bit |
| CL / CH  | 8-bit  |
| CX       | 16-bit |
| ECX      | 32-bit |
| RCX      | 64-bit |
| DL / DH  | 8-bit  |
| DX       | 16-bit |
| EDX      | 32-bit |
| RDX      | 64-bit |

| Calling Convention | arg1 | arg2 | arg3 | arg4 |
| ------------------ | ---- | ---- | ---- | ---- |
| SysV x86-64        | RDI  | RSI  | RDX  | RCX  |
| Windows x64        | RCX  | RDX  | R8   | R9   |

| Assembly Suffix | Meaning | Size   |
| --------------- | ------- | ------ |
| b               | byte    | 8-bit  |
| w               | word    | 16-bit |
| l               | long    | 32-bit |
| q               | quad    | 64-bit |
