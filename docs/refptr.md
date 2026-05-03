# CRT refptr Patching

Deep-dive into the `.refptr` mechanism and why my_wine must patch it.

---

## 1. What Is `.refptr`?

`.refptr` is a GCC/MinGW relocation mechanism for global variables.
It is a linker-generated section that contains **pointers to global
variables**, not the variables themselves.

### How It Works

In a normal ELF binary, a global variable like `_fmode` is defined at
a fixed address. A reference to `_fmode` is simply a pointer to that
address. The linker resolves it directly.

In MinGW-w64 PE binaries, GCC generates an extra level of indirection
for certain globals. Instead of embedding the address directly, the
linker creates a `.refptr` section containing an array of 64-bit
pointers, one per global that needs indirection:

```
.refptr section layout (example):
Offset  Symbol                    Points to
──────  ──────────────────────    ─────────
0x000   __CTOR_LIST__             → constructor list
0x010   __DTOR_LIST__             → destructor list
0x020   __xi_a                    → init flag array start
0x030   __dyn_tls_init_callback   → TLS init callback (NULL)
0x040   __image_base__            → image base address
0x050   __imp___initenv           → pointer to __initenv
0x090   __mingw_oldexcpt_handler  → exception handler
0x0A0   __native_startup_lock     → startup lock variable
0x0B0   __native_startup_state    → startup state variable
0x0C0   __xc_a                    → init flag array start
0x0D0   __xc_z                    → init flag array end
...
```

Each entry is a `uint64_t` (8 bytes). The PE code dereferences these
entries to get the actual values or addresses.

### Why Indirection?

The indirection allows the loader to relocate these pointers at load
time. When the image is loaded at a different base than its preferred
base, the pointers in `.refptr` are adjusted by the linker/loader so
they still point to the correct addresses. This is essential for
ASLR (Address Space Layout Randomization) support.

---

## 2. Why It Needs Patching

When my_wine loads a PE at its preferred base, the `.refptr` section
contains pointers that were valid **for the PE's own globals in a
Windows environment**. In our Linux environment:

1. The PE's `.data` and `.bss` sections exist at their expected
   addresses (we mapped at the preferred base).
2. However, many `.refptr` entries point to CRT globals that don't
   exist in the PE — they exist in the **host** (my_wine's process).
3. Examples: `__CTOR_LIST__`, `__DTOR_LIST__`, `__native_startup_lock`,
   `_fmode`, `_commode`, etc. — these are variables that the mingw-w64
   CRT defines and the PE's code expects to find.

If we leave the `.refptr` entries untouched, they point to addresses
within the PE that contain garbage (or don't exist). When the CRT
startup code (`mainCRTStartup → __getmainargs → _initterm`) dereferences
them, the process crashes.

**The fix:** Patch each `.refptr` entry to point to our *own* stub
variables (defined in `crt_globals.c`) instead of the PE's original
targets.

---

## 3. How `patch_crt_refptrs` Works

`patch_crt_refptrs()` is defined in `src/stubs/crt_refptrs.c`. It
runs in the parent process before `fork()`.

### Step 1: Find the `.refptr` Section

```c
IMAGE_SECTION_HEADER *refptr_sec = find_section_by_name(nt, sections, ".refptr");
```

The section is located by name in the section table. Its
`VirtualAddress` and `SizeOfRawData`/`Misc.VirtualSize` give the
range.

### Step 2: Try COFF Symbol Table for Dynamic Discovery

If the PE has an unparsed COFF symbol table, the function attempts
symbol-based discovery:

```c
int sym_count = parse_symbol_table_from_image(image_base, nt, ...);
uint32_t sym_value = lookup_symbol_value(symbols, sym_count, string_table, name);
if (sym_value != 0) {
    target_rva = refptr_base + sym_value;  // symbol offset within .refptr
}
```

This is more robust than hardcoded offsets because it discovers the
actual location of each `.refptr` entry from the PE's own symbol
information.

### Step 3: Fall Back to Relative Offsets

If no symbol table is available (or the symbol isn't found), the
function falls back to hardcoded relative offsets within the `.refptr`
section. These offsets are derived from the mingw-w64 linker's layout
and are far more portable than absolute RVAs because they work
regardless of image base:

```c
{ "__CTOR_LIST__",              (void *)&ctor_list_stub,              0x000 },
{ "__DTOR_LIST__",              (void *)&dtor_list_stub,              0x010 },
{ "__image_base__",             (void *)&g_crt_ctx.image_base,        0x040 },
...
```

### Step 4: Apply Each Patch

For each mapping:

```c
apply_refptr_patch(image_base, target_rva, target, name, image_size);
```

Which does:
1. `mprotect(page, 4096, PROT_READ|PROT_WRITE)` — make page writable
2. `*refptr = target` — overwrite the pointer
3. `mprotect(page, 4096, PROT_READ)` — restore read-only

The `mprotect` is necessary because `.refptr` sections are typically
mapped as `PROT_READ` only (they're in a section with
`IMAGE_SCN_MEM_READ` but no `IMAGE_SCN_MEM_WRITE`).

### Step 5: Special Handling for `__imp___initenv`

The `__imp___initenv` stub is special — it needs to point to the PE's
own `envp` pointer in `.bss` (at offset `0x018` from `.bss` base),
not to our host variable. This is set up dynamically:

```c
__imp___initenv_stub = (void **)((char *)image_base + g_crt_ctx.bss_vaddr + 0x018);
```

---

## 4. The `__acrt_iob_func` Patch

A separate but related patching problem is `__acrt_iob_func`.

### The Problem

The PE's `__acrt_iob_func` is an import wrapper (a small function in
`.text`) that the mingw-w64 CRT uses to get the `FILE*` array. It
looks like this:

```asm
__acrt_iob_func:
    mov  ecx, <index>     ; save index in ecx (32-bit)
    call __iob_func       ; our stub, returns base of FILE array in rax
    mov  ecx, ebx         ; restore lower 32 bits of index
    lea  rdx, [rcx + rcx*2]  ; rdx = rcx * 3  ← BUG: upper 32 bits of rcx are garbage
    shl  rdx, 4           ; rdx *= 16 → rdx = rcx * 48
    add  rax, rdx         ; rax = base + offset → wrong FILE*
    ret
```

The bug: our `__iob_func` stub (compiled with `ms_abi`) may clobber
the upper 32 bits of `rcx`. When the wrapper tries to use `rcx` again
for the index calculation, the upper 32 bits are garbage, leading to
a wildly incorrect offset.

**Root cause:** The mingw-w64 CRT's `__acrt_iob_func` wrapper assumes
that `call __iob_func` preserves `rcx` (because on Windows, `rcx` is
a caller-saved register and the wrapper is responsible for saving it).
But our stub may not honor this convention in all cases.

### The Fix

The fix is applied in the child process (after `fork()`) in
`entry.c:patch_acrt_iob()`. It patches the `__acrt_iob_func` wrapper
in the PE's `.text` section:

```c
// Find the jmp-thunk in .text that resolves to __iob_func
void *thunk = find_text_thunk(base, nt, sections, __iob_func);

// Overwrite with: movabs rax, <addr>; ret; NOP NOP NOP NOP NOP
code[0]  = 0x48;  // REX.W
code[1]  = 0xb8;  // movabs rax, imm64
code[2..9] = __wine_iob_data();  // 8-byte immediate
code[10] = 0xc3;  // ret
code[11..14] = 0x90;  // NOP padding (15 bytes total)
```

The patched function simply returns `__wine_iob_data()` directly,
bypassing the broken index math entirely. Callers that need `stdout`,
`stderr`, or `stdin` get the correct `FILE*` pointer without any
index calculation.

This is a **15-byte overwrite** of the original `jmp *disp(%rip)`
thunk instruction. It requires `mprotect` to make `.text` writable
temporarily.

### Why in the Child?

The `__acrt_iob_func` patch is done in the child process (not the
parent) because:

1. The `__wine_iob_data()` function returns a pointer that is only
   meaningful in the child's address space (the host's `__wine_iob`
   is copied by `fork()`).
2. The `.text` section is `PROT_READ|PROT_EXEC` in both parent and
   child (inherited from the image mapping), so `mprotect` works in
   either. But doing it in the child avoids unnecessary patching in
   the parent.
