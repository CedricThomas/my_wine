/*
 * my_wine.c - Main PE loader (orchestrator)
 *
 * Opens a PE32+ binary, maps sections with correct protections,
 * resolves imports, sets up TEB/PEB, allocates a guest stack,
 * and jumps to the entry point.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <asm/prctl.h>
#include <asm/unistd_64.h>
#include <fcntl.h>
#include <signal.h>
#include <ucontext.h>
#include <sys/syscall.h>
#include <sys/user.h>  // for REG_RIP, REG_RSP, etc.
#include <stdbool.h>

#include "include/pe.h"
#include "include/ntdll.h"
#include "include/kernel32.h"
#include "include/msvcrt.h"
#include "include/syscall/thunk_gen.h"
#include "include/syscall/signal_handler.h"
#include "include/syscall/dispatcher.h"

extern char **environ;  // from libc, for guest envp

/* SEH frame: { next=NULL, handler } — NULL-terminated chain */
static uint64_t g_seh_frame[2] __attribute__((aligned(8))) = { 0, 0 };

static void *g_stack_base = NULL;

/* ── External data accessor from msvcrt.c ─────────────────────── */
extern void *__wine_iob_data(void);

typedef int (*main_fn)(int, char **, char **);

/* ── External function declarations (from pe_parser.c) ──────── */

int parse_dos_header(const void *base, size_t file_size, IMAGE_DOS_HEADER *out_header);
int parse_nt_headers(const void *base, size_t file_size,
                     const IMAGE_DOS_HEADER *dos_header,
                     IMAGE_NT_HEADERS64 *out_nt_headers);
int parse_sections(const void *base, size_t file_size,
                   const IMAGE_NT_HEADERS64 *nt_headers,
                   IMAGE_SECTION_HEADER **out_sections);
int parse_imports(const void *base, size_t file_size,
                  const IMAGE_NT_HEADERS64 *nt_headers,
                  IMAGE_IMPORT_DESCRIPTOR **out_first_descriptor);
void dump_headers(const IMAGE_DOS_HEADER *dos, const IMAGE_NT_HEADERS64 *nt,
                  const IMAGE_SECTION_HEADER *sections);

/* ── Forward declarations for our own functions ──────────────── */

static int resolve_imports(void *base, IMAGE_NT_HEADERS64 *nt);
static void *setup_teb_peb(void);
static void *setup_stack(IMAGE_OPTIONAL_HEADER64 *opt);
__attribute__((ms_abi))
static int seh_crash_handler(void *exception_record, void *establisher_frame,
                              void *context_record, void *dispatcher_context);
void run_guest(void (*entry)(void), void *stack_top, void *peb,
                       char **guest_argv, char **guest_envp);
static void crash_handler(int sig, siginfo_t *info, void *ucontext);

/* ── Dynamic thunk locator ──────────────────────────────────── */

/* Find the .text jmp-thunk address whose IAT entry resolves to target_addr.
 * Scans all "ff 25 disp32" (jmp *disp(%rip)) instructions in .text and
 * checks if the dereferenced IAT pointer equals target_addr.
 * Returns the absolute address of the thunk instruction, or NULL. */
static void *find_text_thunk(void *image_base, IMAGE_NT_HEADERS64 *nt,
                              IMAGE_SECTION_HEADER *sections,
                              void *target_addr)
{
    /* Locate .text section */
    uint64_t text_start = 0, text_end = 0;
    for (uint16_t i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (memcmp(sections[i].Name, ".text", 5) == 0) {
            text_start = sections[i].VirtualAddress;
            text_end   = text_start + sections[i].Misc.VirtualSize;
            if (text_end < text_start || sections[i].SizeOfRawData > sections[i].Misc.VirtualSize)
                text_end = text_start + sections[i].SizeOfRawData;
            break;
        }
    }
    if (text_start == 0) return NULL;

    uint8_t *text_base = (uint8_t *)image_base + text_start;
    uint64_t target_val = (uint64_t)(uintptr_t)target_addr;

    for (uint64_t off = 0; off < (text_end - text_start) - 5; off++) {
        if (text_base[off] == 0xff && text_base[off + 1] == 0x25) {
            int32_t disp = *(int32_t *)(text_base + off + 2);
            uint64_t instr_addr = text_start + off;
            uint64_t target_rva = instr_addr + 6 + disp;
            uint64_t *target_ptr = (uint64_t *)((char *)image_base + target_rva);
            if (*target_ptr == target_val) {
                return (void *)((char *)image_base + instr_addr);
            }
        }
    }
    return NULL;
}

/* ── Jump to entry point ────────────────────────────────────── */

static int jump_to_entry(uint64_t entry_abs, void *stack_top, void *stack_base, void *teb,
                          char **guest_argv, char **guest_envp)
{
    (void)stack_base;  /* suppress unused warning */
    void *peb = *(void **)((char *)teb + 0x60);
    (void)peb;  /* not passed to run_guest anymore */
    (void)guest_argv;  /* used in child below */
    (void)guest_envp;  /* used in child below */
    pid_t pid = fork();

    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = crash_handler;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGILL, &sa, NULL);
        sigaction(SIGABRT, &sa, NULL);
        sigaction(SIGFPE, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);
        sigaction(SIGTRAP, &sa, NULL);
        { const char t[] = "CHILD: all handlers set\n";
          syscall(SYS_write, 2, t, sizeof(t)-1); }

        /* Set up signal stack for reliable signal handling */
        {
            void *sigstack_mem = mmap(NULL, 65536, PROT_READ|PROT_WRITE,
                                      MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
            if (sigstack_mem != MAP_FAILED) {
                stack_t ss;
                ss.ss_sp = sigstack_mem;
                ss.ss_size = 65536;
                ss.ss_flags = 0;
                sigaltstack(&ss, NULL);
            }
        }

        generate_all_thunks();
        setup_sigsys_handler(handle_syscall);
        /* setup_seccomp(); */

        fprintf(stderr, "my_wine: jumping to entry 0x%lx via inline asm\n",
                (unsigned long)entry_abs);
        fflush(stderr);

        /* Re-set GS base in child (inherited from parent but let's be sure) */
        if (syscall(__NR_arch_prctl, ARCH_SET_GS, (unsigned long)teb) != 0) {
            perror("ARCH_SET_GS");
            _exit(1);
        }
        /*
         * Patch __acrt_iob_func to return __wine_iob_data directly.
         *
         * WHY: The PE's __acrt_iob_func (msvcrt import wrapper) does:
         *   1. mov %ecx,%ebx     ; save index
         *   2. call __iob_func   ; our stub (ms_abi) may clobber rcx upper bits
         *   3. mov %ebx,%ecx     ; restore lower 32 bits (upper 32 remain garbage)
         *   4. lea (%rcx,%rcx,2),%rdx  ; rdx = rcx*3 (GARBAGE from upper bits)
         *   5. shl $4,%rdx       ; rdx *= 16 = rcx*48 (still GARBAGE)
         *   6. add %rdx,%rax     ; rax = base + garbage → wrong FILE*
         *
         * We cannot fix this from __iob_func alone because the garbage is in
         * rcx which our function doesn't control. Patching the wrapper to return
         * the base directly (bypassing the broken index math) is the cleanest fix.
         *
         * Patch: movabs $<addr>,%rax; ret; 4x NOP (15 bytes total)
         * This replaces the jmp thunk or wrapper with a direct return.
         */
        {
            uint64_t image_base = entry_abs & ~0xFFFFFUL;
            void *base = (void *)(uintptr_t)image_base;

            /* Reconstruct NT headers and section table from the live image */
            const IMAGE_DOS_HEADER *img_dos = (const IMAGE_DOS_HEADER *)base;
            uint32_t pe_off = img_dos->e_lfanew;
            IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)((char *)base + pe_off);
            uint32_t sec_off = pe_off + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                               nt->FileHeader.SizeOfOptionalHeader;
            IMAGE_SECTION_HEADER *sections =
                (IMAGE_SECTION_HEADER *)((char *)base + sec_off);

            /* Compute .text section end for bounds checking */
            uint64_t text_end = 0;
            for (uint16_t i = 0; i < nt->FileHeader.NumberOfSections; i++) {
                if (memcmp(sections[i].Name, ".text", 5) == 0) {
                    text_end = sections[i].VirtualAddress + sections[i].Misc.VirtualSize;
                    if (text_end < sections[i].VirtualAddress ||
                        sections[i].SizeOfRawData > sections[i].Misc.VirtualSize)
                        text_end = sections[i].VirtualAddress + sections[i].SizeOfRawData;
                    break;
                }
            }

            void *thunk = find_text_thunk(base, nt, sections,
                                          (void *)__iob_func);
            if (thunk == NULL) {
                fprintf(stderr, "WARNING: __acrt_iob_func thunk not found, skipping patch\n");
            } else {
                uint8_t *code = (uint8_t *)thunk;
                uintptr_t thunk_abs = (uintptr_t)thunk;
                uintptr_t page_addr = thunk_abs & ~(uintptr_t)4095;
                void *page = (void *)page_addr;

                /* Validate: first two bytes should be ff 25 (jmp *disp32(%rip)) */
                bool is_jmp_thunk = (code[0] == 0xff && code[1] == 0x25);
                if (!is_jmp_thunk) {
                    fprintf(stderr, "WARNING: __acrt_iob_func at 0x%lx has unexpected opcode 0x%02x 0x%02x, skipping patch\n",
                            thunk_abs, code[0], code[1]);
                } else {
                    /* Bounds check: ensure 15-byte patch won't exceed .text section */
                    uint64_t thunk_off = (uint64_t)thunk - (uint64_t)base;
                    if (text_end == 0 || thunk_off + 15 > text_end) {
                        fprintf(stderr, "WARNING: __acrt_iob_func thunk at 0x%lx is too close to .text end (need 15 bytes, have %ld), skipping patch\n",
                                (unsigned long)thunk_off, (long)(text_end > thunk_off ? text_end - thunk_off : 0));
                    } else {
                        if (mprotect(page, 4096, PROT_READ|PROT_WRITE|PROT_EXEC) == 0) {
                            /* movabs $imm64, %rax */
                            code[0] = 0x48;                /* REX.W */
                            code[1] = 0xb8;                /* movabs rax, imm64 */
                            *(uint64_t *)(code + 2) = (uint64_t)(uintptr_t)__wine_iob_data();
                            /* ret */
                            code[10] = 0xc3;
                            /* NOP padding to fill 15 bytes */
                            for (int k = 11; k < 15; k++) code[k] = 0x90;

                            if (mprotect(page, 4096, PROT_READ|PROT_EXEC) != 0) {
                                perror("mprotect restore __acrt_iob_func");
                            }
                            fprintf(stderr, "patched __acrt_iob_func at 0x%lx -> returns __wine_iob_data\n",
                                    thunk_abs);
                        } else {
                            perror("mprotect __acrt_iob_func");
                        }
                    }
                }
            }
        }

        /* Jump to the PE's AddressOfEntryPoint (mainCRTStartup) */
        {
            void (*entry)(void) = (void (*)(void))(void *)(uintptr_t)entry_abs;
            run_guest(entry, stack_top, NULL, guest_argv, guest_envp);
        }

        fprintf(stderr, "my_wine: inline jump returned\n");
        fflush(stderr);
        _exit(1);
    }

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        fprintf(stderr, "my_wine: child exited with code %d\n", code);
        return code;
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        fprintf(stderr, "my_wine: child killed by signal %d\n", sig);
        return 128 + sig;
    }

    return 0;
}
static void *g_image_base = NULL;

/* ── Import resolution ─────────────────────────────────────── */

/* Maps function names to our implementations */

typedef struct {
    const char *name;
    void *address;
} import_entry_t;

/* Name→address table for NT and kernel32 functions */
static import_entry_t import_table[] = {
    /* ntdll functions (via syscall thunks) */
    { "NtWriteFile", (void*)handler_NtWriteFile },
    { "NtReadFile", (void*)handler_NtReadFile },
    { "NtClose", (void*)handler_NtClose },
    { "NtTerminateProcess", (void*)handler_NtTerminateProcess },
    { "NtCallbackReturn", (void*)handler_NtCallbackReturn },
    { "NtQueryInformationProcess", (void*)handler_NtQueryInformationProcess },
    { "NtAllocateVirtualMemory", (void*)handler_NtAllocateVirtualMemory },
    { "NtFreeVirtualMemory", (void*)handler_NtFreeVirtualMemory },
    { "NtCreateSection", (void*)handler_NtCreateSection },
    { "NtMapViewOfSection", (void*)handler_NtMapViewOfSection },
    { "NtUnmapViewOfSection", (void*)handler_NtUnmapViewOfSection },
    { "NtCreateEvent", (void*)handler_NtCreateEvent },
    { "NtCreateThreadEx", (void*)handler_NtCreateThreadEx },
    { "NtOpenFile", (void*)handler_NtOpenFile },
    { "NtGetContextThread", (void*)handler_NtGetContextThread },
    { "NtSetContextThread", (void*)handler_NtSetContextThread },
    /* kernel32 functions */
    { "GetStdHandle", (void*)GetStdHandle },
    { "WriteFile", (void*)WriteFile },
    { "ReadFile", (void*)ReadFile },
    { "ExitProcess", (void*)ExitProcess },
    { "GetProcAddress", (void*)GetProcAddress },
    { "LoadLibraryA", (void*)LoadLibraryA },
    { "GetModuleHandleA", (void*)GetModuleHandleA },
    { "lstrlenA", (void*)lstrlenA },
    { "DeleteCriticalSection", (void*)DeleteCriticalSection },
    { "EnterCriticalSection", (void*)EnterCriticalSection },
    { "GetLastError", (void*)GetLastError },
    { "GetStartupInfoA", (void*)GetStartupInfoA },
    { "InitializeCriticalSection", (void*)InitializeCriticalSection },
    { "LeaveCriticalSection", (void*)LeaveCriticalSection },
    { "SetUnhandledExceptionFilter", (void*)SetUnhandledExceptionFilter },
    { "Sleep", (void*)Sleep },
    { "TlsGetValue", (void*)TlsGetValue },
    { "VirtualProtect", (void*)VirtualProtect },
    { "VirtualQuery", (void*)VirtualQuery },
    { "__C_specific_handler", (void*)__C_specific_handler },
    /* msvcrt functions (non-const entries, initialized at runtime) */
    { "__getmainargs", (void*)__getmainargs },
    { "__initenv", (void*)__initenv },
    { "__iob_func", (void*)__iob_func },
    { "__lconv_init", (void*)__lconv_init },
    { "__set_app_type", (void*)__set_app_type },
    { "__setusermatherr", (void*)__setusermatherr },
    { "_acmdln", (void*)&_acmdln },
    { "_amsg_exit", (void*)_amsg_exit },
    { "_cexit", (void*)_cexit },
    { "_commode", (void*)&_commode },
    { "_fmode", (void*)&_fmode },
    { "_initterm", (void*)_initterm },
    { "_onexit", (void*)_onexit },
    /* Dynamic entries - filled by init_msvcrt_imports() */
    { "abort", NULL },
    { "calloc", NULL },
    { "exit", NULL },
    { "fprintf", NULL },
    { "free", NULL },
    { "fwrite", NULL },
    { "malloc", NULL },
    { "memcpy", NULL },
    { "signal", NULL },
    { "strlen", NULL },
    { "strncmp", NULL },
    { "vfprintf", NULL },
    { NULL, NULL }
};

static void init_msvcrt_imports(void)
{
    import_table[49].address = __msvcrt_abort;    /* abort */
    import_table[50].address = __msvcrt_calloc;   /* calloc */
    import_table[51].address = __msvcrt_exit;     /* exit */
    import_table[52].address = __msvcrt_fprintf;  /* fprintf */
    import_table[53].address = __msvcrt_free;     /* free */
    import_table[54].address = __msvcrt_fwrite;   /* fwrite */
    import_table[55].address = __msvcrt_malloc;   /* malloc */
    import_table[56].address = __msvcrt_memcpy;   /* memcpy */
    import_table[57].address = __msvcrt_signal;   /* signal */
    import_table[58].address = __msvcrt_strlen;   /* strlen */
    import_table[59].address = __msvcrt_strncmp;  /* strncmp */
    import_table[60].address = __msvcrt_vfprintf; /* vfprintf */
}

static void *resolve_import(const char *dll_name, const char *func_name)
{
    (void)dll_name; // We don't distinguish between DLLs for now
    for (int i = 0; import_table[i].name != NULL; i++) {
        if (strcmp(import_table[i].name, func_name) == 0) {
            return import_table[i].address;
        }
    }
    fprintf(stderr, "  ERROR: unresolved import: %s!%s\n", dll_name, func_name);
    return NULL;
}

static int resolve_imports(void *base, IMAGE_NT_HEADERS64 *nt)
{

    IMAGE_OPTIONAL_HEADER64 *opt = &nt->OptionalHeader;

    /* Get import directory */

    if (opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size == 0) {
        printf("No imports to resolve\n");
        return 0;
    }

    uint64_t import_rva = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;

    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)((char *)base + import_rva);


    printf("Resolving imports:\n");
    fflush(stdout);

    /* ── Pass 1: resolve and write to descriptor's FirstThunk (IAT) ──── */

    IMAGE_IMPORT_DESCRIPTOR *desc_start = desc;
    while (desc->Name != 0) {
        const char *dll_name = (const char *)((char *)base + desc->Name);

        printf("  DLL: %s\n", dll_name);

        /* Get the original thunk table (with function names/ordinals) */
        IMAGE_THUNK_DATA64 *orig_thunks = (IMAGE_THUNK_DATA64 *)((char *)base + desc->u1.OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *iath = (IMAGE_THUNK_DATA64 *)((char *)base + desc->FirstThunk);


        for (int i = 0; orig_thunks[i].AddressOfData != 0; i++) {
            void *addr = NULL;

            if (orig_thunks[i].AddressOfData & 0x8000000000000000ULL) {
                /* Ordinal import (high bit set) */
                uint64_t ordinal = orig_thunks[i].AddressOfData & 0xFFFF;
                fprintf(stderr, "  WARNING: ordinal import %lu not supported\n", ordinal);
                continue;
            } else {
                /* Name import */
                IMAGE_IMPORT_BY_NAME *imp_name = (IMAGE_IMPORT_BY_NAME *)((char *)base + orig_thunks[i].AddressOfData);
                addr = resolve_import(dll_name, (const char *)imp_name->Name);
            }

            if (addr != NULL) {
                printf("    Resolved %s -> %p\n",
                       orig_thunks[i].AddressOfData & 0x8000000000000000ULL ?
                       "<ordinal>" :
                       ((IMAGE_IMPORT_BY_NAME *)((char *)base + orig_thunks[i].AddressOfData))->Name,
                       addr);
                iath[i].AddressOfData = (uint64_t)(uintptr_t)addr;
            } else {
                fprintf(stderr, "    FAILED to resolve import at index %d\n", i);
            }
        }

        desc++;
    }

    /* ── Pass 2: patch thunk IAT targets found by scanning .text ──── */
    /* The PE has non-standard import layout: the jmp thunks in .text read from
     * addresses (e.g., 0x878c-0x88cc) that differ from the descriptor's FirstThunk
     * (e.g., 0x80bc-0x8204). These thunk targets are in a zero-padded region of
     * .idata that was never populated from the file. We need to populate them.
     *
     * Strategy: scan .text for all "jmp *disp32(%rip)" instructions (ff 25),
     * collect unique targets, sort them. These correspond to the ILT entries in
     * DLL order. Match by position: the nth sorted target gets the nth resolved
     * address from the concatenated IAT entries. */

    /* Find .text section — compute section headers from the actual image, not
     * the local nt copy (which doesn't have sections past the optional header). */
    const IMAGE_DOS_HEADER *img_dos = (const IMAGE_DOS_HEADER *)base;
    uint32_t pe_off = img_dos->e_lfanew;
    uint32_t sec_off = pe_off + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                       nt->FileHeader.SizeOfOptionalHeader;
    IMAGE_SECTION_HEADER *sections = (IMAGE_SECTION_HEADER *)((char *)base + sec_off);
    uint16_t num_sections = nt->FileHeader.NumberOfSections;

    uint64_t text_start = 0, text_end = 0;
    for (uint16_t i = 0; i < num_sections; i++) {
        if (memcmp(sections[i].Name, ".text", 5) == 0) {
            text_start = sections[i].VirtualAddress;
            text_end   = text_start + sections[i].Misc.VirtualSize;
            if (text_end < text_start || sections[i].SizeOfRawData > sections[i].Misc.VirtualSize)
                text_end = text_start + sections[i].SizeOfRawData;
            break;
        }
    }

    if (text_start == 0) {
        fprintf(stderr, "WARNING: .text section not found, skipping thunk scan\n");
    } else {
        uint8_t *text_base = (uint8_t *)base + text_start;

        /* Step 1: scan .text for all ff 25 xx xx xx xx (jmp *disp32(%rip)),
         * collect unique target addresses */
        uint64_t thunk_targets[256];
        int num_targets = 0;

        for (uint64_t off = 0; off < (text_end - text_start) - 5; off++) {
            if (text_base[off] == 0xff && text_base[off + 1] == 0x25) {
                int32_t disp = *(int32_t *)(text_base + off + 2);
                uint64_t instr_addr = text_start + off;
                uint64_t target = instr_addr + 6 + disp;

                /* Deduplicate */
                int dup = 0;
                for (int t = 0; t < num_targets; t++) {
                    if (thunk_targets[t] == target) { dup = 1; break; }
                }
                if (!dup && num_targets < 256) {
                    thunk_targets[num_targets++] = target;
                }
            }
        }

        /* Step 2: sort targets by address */
        for (int i = 0; i < num_targets - 1; i++) {
            for (int j = i + 1; j < num_targets; j++) {
                if (thunk_targets[j] < thunk_targets[i]) {
                    uint64_t tmp = thunk_targets[i];
                    thunk_targets[i] = thunk_targets[j];
                    thunk_targets[j] = tmp;
                }
            }
        }

        printf("  Found %d thunk targets in .text (range 0x%lx-0x%lx)\n",
               num_targets,
               num_targets > 0 ? (unsigned long)thunk_targets[0] : 0,
               num_targets > 0 ? (unsigned long)thunk_targets[num_targets - 1] + 7 : 0);

        /* Step 3: build flat array of (ILT_value, resolved_addr, func_name) from
         * all import descriptors in DLL order */
        struct import_flat {
            uint64_t ilt_value;       /* OriginalFirstThunk[i].AddressOfData */
            uint64_t resolved_addr;   /* FirstThunk[i].AddressOfData (from pass 1) */
            const char *dll_name;
            const char *func_name;
        };
        struct import_flat flat[256];
        int num_flat = 0;

        desc = desc_start;
        while (desc->Name != 0 && num_flat < 256) {
            const char *dll_name = (const char *)((char *)base + desc->Name);
            IMAGE_THUNK_DATA64 *orig_thunks = (IMAGE_THUNK_DATA64 *)((char *)base + desc->u1.OriginalFirstThunk);
            IMAGE_THUNK_DATA64 *iath = (IMAGE_THUNK_DATA64 *)((char *)base + desc->FirstThunk);

            for (int i = 0; orig_thunks[i].AddressOfData != 0 && num_flat < 256; i++) {
                flat[num_flat].ilt_value = orig_thunks[i].AddressOfData;
                flat[num_flat].resolved_addr = iath[i].AddressOfData;
                flat[num_flat].dll_name = dll_name;
                if (orig_thunks[i].AddressOfData & 0x8000000000000000ULL) {
                    flat[num_flat].func_name = "<ordinal>";
                } else {
                    IMAGE_IMPORT_BY_NAME *imp_name = (IMAGE_IMPORT_BY_NAME *)((char *)base + orig_thunks[i].AddressOfData);
                    flat[num_flat].func_name = (const char *)imp_name->Name;
                }
                num_flat++;
            }
            desc++;
        }

        printf("  Flat import array: %d entries from %d descriptors\n",
               num_flat, (int)(desc - desc_start));

        /* Step 4: match thunk targets to flat entries
         * The thunk IAT may contain:
         * (a) ILT RVAs (if from file data that was copied as OriginalFirstThunk)
         * (b) Already-resolved addresses (if the target overlaps with FirstThunk
         *     that pass 1 populated)
         * (c) Garbage/random file data
         *
         * Strategy: try matching by resolved_addr first (handles overlap case),
         * then by ILT value, then by position. */
        int matched = 0;
        for (int t = 0; t < num_targets; t++) {
            uint64_t target = thunk_targets[t];
            uint64_t *target_ptr = (uint64_t *)((char *)base + target);
            uint64_t current_val = *target_ptr;

            int did_match = 0;

            /* Try matching by resolved address (target overlaps with IAT from pass 1) */
            for (int f = 0; f < num_flat; f++) {
                if (flat[f].resolved_addr == current_val) {
                    /* Already has the correct resolved address - no write needed */
                    matched++;
                    did_match = 1;
                    break;
                }
            }

            if (!did_match && current_val != 0) {
                /* Try matching by ILT RVA */
                for (int f = 0; f < num_flat; f++) {
                    if (flat[f].ilt_value == current_val && flat[f].resolved_addr != 0) {
                        *target_ptr = flat[f].resolved_addr;
                        printf("    Thunk patch (ilt match): %s!%s at 0x%lx <- 0x%lx\n",
                               flat[f].dll_name, flat[f].func_name,
                               (unsigned long)target, (unsigned long)flat[f].resolved_addr);
                        matched++;
                        did_match = 1;
                        break;
                    }
                }
            }

            if (!did_match) {
                /* Fallback: match by position (nth target <- nth flat entry) */
                if (t < num_flat && flat[t].resolved_addr != 0) {
                    *target_ptr = flat[t].resolved_addr;
                    printf("    Thunk patch (pos match): %s!%s at 0x%lx <- 0x%lx\n",
                           flat[t].dll_name, flat[t].func_name,
                           (unsigned long)target, (unsigned long)flat[t].resolved_addr);
                    matched++;
                }
            }
        }

        printf("  Thunk IAT patched: %d/%d targets resolved\n", matched, num_targets);
    }

    return 0;
}

static void *setup_teb_peb(void)
{
    /* Allocate TEB (Thread Environment Block) - at least 4KB */
    size_t teb_size = 4096;
    void *teb = mmap(NULL, teb_size, PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
    if (teb == MAP_FAILED) {
        perror("mmap TEB");
        return NULL;
    }

    /* Zero the TEB */
    memset(teb, 0, teb_size);

    /* Set up SEH chain: gs:[0x00] points to EXCEPTION_REGISTRATION_RECORD
     * which is { next=NULL, handler=seh_crash_handler } */
    g_seh_frame[0] = 0;  /* next = NULL (end of chain) */
    g_seh_frame[1] = (uint64_t)(uintptr_t)&seh_crash_handler;
    *(void **)teb = (void *)g_seh_frame;  /* gs:[0x00] = SEH chain head */

    /* Fix gs:[0x30] null deref crash at 0x1400011d4:
     *   mov rax, gs:[0x30]  →  rax must be TEB
     *   mov rsi, [rax+8]    →  teb[0x08] must be TEB (self-ref)
     * so the loop that checks rsi==rax can exit. */
    *(void **)((uint8_t *)teb + 0x08) = teb;  // TEB self-referential
    *(void **)((uint8_t *)teb + 0x30) = teb;  // fake thread pointer (self-ref)

    /* Allocate PEB (Process Environment Block) */
    size_t peb_size = 4096;
    void *peb = mmap(NULL, peb_size, PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
    if (peb == MAP_FAILED) {
        perror("mmap PEB");
        munmap(teb, teb_size);
        return NULL;
    }

    memset(peb, 0, peb_size);

    /* Set PEB pointer in TEB at offset 0x60 */
    *(void **)((char *)teb + 0x60) = peb;

    /* Set image base pointer in PEB at offset 0x008 (ImageBaseAddress) */
    *(void **)((char *)peb + 0x008) = g_image_base;

    /* Set BeingDebugged = 0 in PEB at offset 0x002 */
    *(uint8_t *)((char *)peb + 0x002) = 0;

    /* Set GS segment to point to TEB */
    if (syscall(__NR_arch_prctl, ARCH_SET_GS, (unsigned long)teb) != 0) {
        perror("arch_prctl ARCH_SET_GS");
        munmap(peb, peb_size);
        munmap(teb, teb_size);
        return NULL;
    }

    printf("TEB at %p, PEB at %p\n", teb, peb);
    printf("arch_prctl(ARCH_GET_GS) = %p\n",
           (void *)syscall(__NR_arch_prctl, ARCH_GET_GS, 0));

    return teb;
}

static void *setup_stack(IMAGE_OPTIONAL_HEADER64 *opt)
{
    uint64_t reserve = opt->SizeOfStackReserve;
    uint64_t commit  = opt->SizeOfStackCommit;

    /* Ensure minimum sizes */
    if (reserve == 0) reserve = 1024 * 1024; /* 1MB default */
    if (commit  == 0) commit   = 4096;        /* 1 page minimum */

    /* CRITICAL: ensure at least 512KB of stack for CRT startup (mainCRTStartup
     * needs significant stack for nested calls to __getmainargs, _initterm, etc.)
     * The PE header often specifies only 4KB commit, which is insufficient. */
    if (commit < 512 * 1024) commit = 512 * 1024;

    /* Align to page boundary */
    reserve = (reserve + 4095) & ~(uint64_t)4095;
    commit  = (commit  + 4095) & ~(uint64_t)4095;

    /* Allocate stack (grows downward on x86_64) */
    void *stack_base = mmap(NULL, commit, PROT_READ|PROT_WRITE,
                             MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
    if (stack_base == MAP_FAILED) {
        perror("mmap stack");
        return NULL;
    }

    /* Top of stack (aligned to 16 bytes for x86_64 ABI requirement) */
    uintptr_t stack_top = (uintptr_t)stack_base + commit;
    stack_top = (stack_top & ~(uintptr_t)15) + 8;  /* ABI requires rsp%16==8 */


    /* Print stack info */
    printf("Stack: base=%p, top=%p, reserve=0x%lx, commit=0x%lx\n",
           stack_base, (void *)stack_top,
           (unsigned long)reserve, (unsigned long)commit);

    /* Store stack_base at a known location for later use */
    *(void **)((uintptr_t)stack_top - 8) = stack_base;
    g_stack_base = stack_base;
    return (void *)stack_top;
}

/* ── Guest entry trampoline (implemented in run_guest.S) ──── */
extern void run_guest(void (*entry)(void), void *stack_top, void *peb,
                       char **guest_argv, char **guest_envp) __attribute__((noreturn));

/* ── Crash handler ──────────────────────────────────────────── */

/* SEH handler — called when an exception occurs in guest code.
 * On x86_64, SEH handlers receive (ExceptionRecord, EstablisherFrame, ContextRecord, DispatcherContext)
 * in RCX, RDX, R8, R9 per Microsoft x64 ABI. */
__attribute__((ms_abi, used))
static int seh_crash_handler(void *exception_record, void *establisher_frame,
                              void *context_record, void *dispatcher_context)
{
    (void)exception_record;
    (void)establisher_frame;
    (void)context_record;
    (void)dispatcher_context;

    /* Dump info via syscall (stderr) */
    { const char t[] = "SEV: SEH handler invoked (exception in guest code)\n";
      syscall(SYS_write, 2, t, sizeof(t)-1); }

    /* Extract exit code from exception record if possible, else use 0xC0000005 (ACCESS_VIOLATION) */
    uint64_t exit_code = 0xC0000005;

    /* Call NtTerminateProcess to exit cleanly */
    syscall(__NR_exit, (int)(exit_code & 0xFF));
    return 1; /* ExceptionContinueExecution (never reached) */
}

static void crash_handler(int sig, siginfo_t *info, void *ucontext)
{
    (void)info;
    const char *sig_name = "UNKNOWN";
    if (sig == SIGSEGV) sig_name = "SIGSEGV";
    else if (sig == SIGILL) sig_name = "SIGILL";
    else if (sig == SIGABRT) sig_name = "SIGABRT";
    else if (sig == SIGFPE) sig_name = "SIGFPE";
    else if (sig == SIGBUS) sig_name = "SIGBUS";
    else if (sig == SIGTRAP) sig_name = "SIGTRAP";
    
    ucontext_t *uc = (ucontext_t *)ucontext;
    if (uc) {
        greg_t *regs = uc->uc_mcontext.gregs;
        char hex_buf[256];
        int hlen;
        
        const char *hdr = "CRASH: ";
        syscall(SYS_write, 2, hdr, 7);
        syscall(SYS_write, 2, sig_name, strlen(sig_name));
        
        hlen = snprintf(hex_buf, sizeof(hex_buf),
            " RIP=0x%llx RSP=0x%llx EFL=0x%llx\n"
            " RAX=0x%llx RBX=0x%llx RCX=0x%llx RDX=0x%llx\n"
            " RSI=0x%llx RDI=0x%llx RBP=0x%llx R12=0x%llx\n"
            " R13=0x%llx R14=0x%llx R15=0x%llx\n",
            (unsigned long long)regs[REG_RIP], (unsigned long long)regs[REG_RSP], (unsigned long long)regs[REG_EFL],
            (unsigned long long)regs[REG_RAX], (unsigned long long)regs[REG_RBX], (unsigned long long)regs[REG_RCX], (unsigned long long)regs[REG_RDX],
            (unsigned long long)regs[REG_RSI], (unsigned long long)regs[REG_RDI], (unsigned long long)regs[REG_RBP], (unsigned long long)regs[REG_R12],
            (unsigned long long)regs[REG_R13], (unsigned long long)regs[REG_R14], (unsigned long long)regs[REG_R15]);
        syscall(SYS_write, 2, hex_buf, hlen);
    }
    
    _exit(139);
}

/* ── main ────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <pe_binary>\n", argv[0]);
        return 1;
    }

    /* 1. Open the PE file */
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) { perror("open"); return 1; }

    struct stat st;
    if (fstat(fd, &st) < 0) { perror("fstat"); close(fd); return 1; }
    size_t file_size = (size_t)st.st_size;

    /* 2. Map file read-only */
    void *file_base = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file_base == MAP_FAILED) { perror("mmap file"); close(fd); return 1; }

    /* 3. Parse headers */
    IMAGE_DOS_HEADER dos;
    if (parse_dos_header(file_base, file_size, &dos) != 0) {
        fprintf(stderr, "Invalid DOS header\n");
        munmap(file_base, file_size);
        close(fd);
        return 1;
    }

    IMAGE_NT_HEADERS64 nt;
    if (parse_nt_headers(file_base, file_size, &dos, &nt) != 0) {
        fprintf(stderr, "Invalid NT headers\n");
        munmap(file_base, file_size);
        close(fd);
        return 1;
    }

    IMAGE_SECTION_HEADER *sections = NULL;
    int num_sections = parse_sections(file_base, file_size, &nt, &sections);
    if (num_sections < 0) {
        fprintf(stderr, "Failed to parse sections\n");
        munmap(file_base, file_size);
        close(fd);
        return 1;
    }

    dump_headers(&dos, &nt, sections);

    /* Force flush before debug output */
    fflush(stdout);


    /* 4. Map image */
    uint64_t image_base = nt.OptionalHeader.ImageBase;
    size_t image_size   = nt.OptionalHeader.SizeOfImage;



    void *base = mmap((void *)(uintptr_t)image_base, image_size,
                       PROT_READ|PROT_WRITE|PROT_EXEC,
                       MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
    if (base == MAP_FAILED) {

        base = mmap(NULL, image_size,
                     PROT_READ|PROT_WRITE|PROT_EXEC,
                     MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
        if (base == MAP_FAILED) {
            perror("mmap image");
            munmap(file_base, file_size);
            close(fd);
            return 1;
        }
    }


    /* 5. Copy section data from file to image */

    for (int i = 0; i < num_sections; i++) {
        if (sections[i].SizeOfRawData == 0)
            continue; /* .bss etc. - zero-filled, already anonymous */
        void *dest = (char *)base + sections[i].VirtualAddress;
        void *src  = (char *)file_base + sections[i].PointerToRawData;
        memcpy(dest, src, sections[i].SizeOfRawData);
    }

    /* Copy PE file headers (DOS + NT + section table) into the image */
    {
        uint32_t headers_size = nt.OptionalHeader.SizeOfHeaders;
        if (headers_size > 0) {
            memcpy(base, file_base, headers_size);
            /* Re-point sections into the image */
            const IMAGE_DOS_HEADER *img_dos = (const IMAGE_DOS_HEADER *)base;
            uint32_t pe_off = img_dos->e_lfanew;
            size_t sec_off  = pe_off + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                              nt.FileHeader.SizeOfOptionalHeader;
            sections = (IMAGE_SECTION_HEADER *)((char *)base + sec_off);
        }
    }

    /* 6. Set per-section protections */
    for (int i = 0; i < num_sections; i++) {
        if (sections[i].Misc.VirtualSize == 0 && sections[i].SizeOfRawData == 0)
            continue;

        int prot = 0;
        if (sections[i].Characteristics & IMAGE_SCN_MEM_READ)
            prot |= PROT_READ;
        if (sections[i].Characteristics & IMAGE_SCN_MEM_WRITE)
            prot |= PROT_WRITE;
        if (sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)
            prot |= PROT_EXEC;

        size_t size = sections[i].Misc.VirtualSize;
        if (size == 0)
            size = sections[i].SizeOfRawData;
        size = (size + 4095) & ~(size_t)4095;

        if (mprotect((char *)base + sections[i].VirtualAddress, size, prot) != 0) {
            perror("mprotect");
            munmap(file_base, file_size);
            close(fd);
            return 1;
        }
    }

    /* Unmap the original file mapping (no longer needed) */

    munmap(file_base, file_size);
    close(fd);

    /* Save the image base for later use (import resolution, TEB/PEB, etc.) */
    g_image_base = base;

    /* Initialize dynamic msvcrt import entries */
    init_msvcrt_imports();


    /* Patch CRT refptrs so the PE can find our global variables */

    patch_crt_refptrs(base, &nt, sections);


    /* 7. Resolve imports */

    resolve_imports(base, &nt);


    /* 8. Set up TEB/PEB */

    void *teb = setup_teb_peb();

    if (!teb) return 1;

    /* 9. Set up stack */

    void *stack_top = setup_stack(&nt.OptionalHeader);

    if (!stack_top) return 1;

    /* 10. Zero .data section and initialize global variables
     *
     * The memset below zeros the entire .data section, so the zero-init
     * values (has_cctor=0, managedapp=0, startinfo=NULL, mainret=0, envp=NULL,
     * argv=NULL) are redundant — they are already zero after memset.
     *
     * The offsets below (0x004, 0x008, 0x010, 0x018, 0x020, 0x028) are
     * relative to the .data section base. They are CRT-specific variable
     * offsets that ideally would come from the PE's symbol table, but are
     * linker-defined for mingw-w64 CRT startup layout.
     */
    {
        int data_section_idx = -1;
        for (int i = 0; i < num_sections; i++) {
            if (memcmp(sections[i].Name, ".data", 5) == 0) {
                data_section_idx = i;
                break;
            }
        }

        if (data_section_idx < 0) {
            fprintf(stderr, "WARNING: .data section not found\n");
        } else {
            IMAGE_SECTION_HEADER *data_sec = &sections[data_section_idx];
            uint64_t data_vaddr = data_sec->VirtualAddress;
            size_t data_size = data_sec->Misc.VirtualSize;
            if (data_size == 0) {
                data_size = data_sec->SizeOfRawData;
            }
            /* Zero the entire .data section */
            memset((uint8_t *)base + data_vaddr, 0, data_size);

            /* Compute base within .data section dynamically */
            uint8_t *data_base = (uint8_t *)base + data_vaddr;

            /* argc = 1, relative to .data section base
             * (was: data_base + 0x7028; offset = 0x7028 - 0x7000 = 0x028) */
            *(uint32_t *)(data_base + 0x028) = 1;

            printf(".data section: vaddr=0x%lx, size=0x%lx, initialized argc\n",
                   (unsigned long)data_vaddr, (unsigned long)data_size);
        }
    }

    /* 11. Build guest argv/envp from actual host arguments */
    char *guest_argv[2];
    guest_argv[0] = argv[1];  /* the PE path */
    guest_argv[1] = NULL;
    char **guest_envp = environ;  /* real host environment */

    /* Set msvcrt globals so __getmainargs can return the real values */
    g_guest_argv = guest_argv;
    g_guest_envp = guest_envp;

    /* Fill _cmdline_storage so _acmdln points to the actual PE path */
    strncpy(_cmdline_storage, argv[1], sizeof(_cmdline_storage) - 1);
    _cmdline_storage[sizeof(_cmdline_storage) - 1] = '\0';

    /* Pre-seed argv/envp pointers in the PE's .bss so the CRT doesn't
     * crash when reading them before calling __getmainargs.
     *
     * Use .bss section VA dynamically (set by patch_crt_refptrs in g_bss_vaddr).
     * The offsets (0x018, 0x020) are relative to .bss base and are CRT-specific;
     * they correspond to the mingw-w64 CRT's envp/argv locations. */
    {
        if (g_bss_vaddr != 0) {
            uint8_t *bss_base = (uint8_t *)base + g_bss_vaddr;
            *(uint64_t *)(bss_base + 0x020) = (uint64_t)(uintptr_t)guest_argv;  // argv
            *(uint64_t *)(bss_base + 0x018) = (uint64_t)(uintptr_t)guest_envp;  // envp
        } else {
            fprintf(stderr, "WARNING: g_bss_vaddr not set, skipping .bss pre-seed\n");
        }
    }

    /* 12. Jump to entry point (pass absolute address, not RVA) */
    uint64_t entry_abs = (uint64_t)(uintptr_t)base + nt.OptionalHeader.AddressOfEntryPoint;
    return jump_to_entry(entry_abs, stack_top, g_stack_base, teb, guest_argv, guest_envp);
}
