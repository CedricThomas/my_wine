# CRT refptr Patching

Deep-dive into the `.refptr` mechanism and why my_wine must patch it.

---

## 1. What Is `.refptr`?

`.refptr` is a GCC/MinGW relocation mechanism for global variables.
It is a linker-generated section that contains **pointers to global
variables**, not the variables themselves.

### Two-Level Indirection

In a normal ELF binary, a global variable like `_fmode` is defined at
a fixed address. A reference to `_fmode` is simply a pointer to that
address. The linker resolves it directly.

In MinGW-w64 PE binaries, GCC generates an extra level of indirection
for certain globals. Instead of embedding the address directly, the
linker creates a `.refptr` section containing an array of 64-bit
pointers, one per global that needs indirection.

The PE code then performs **two-level indirection** to access these
globals:

```asm
; CRT startup code accessing a refptr global (e.g. __native_startup_lock)
mov  rax, [rip + offset_to_refptr_entry]  ; Level 1: load the pointer stored in .refptr
mov  rax, [rax]                            ; Level 2: dereference to get the actual value
```

**The problem:** if the value stored at the `.refptr` entry is `0`, the
second `mov` dereferences address `0` → `SIGSEGV`. The entry cannot be
left as zero. Instead, it must point to a real memory location that
contains `0` (or whatever initial value is needed).

### Example `.refptr` Section Layout

```
.refptr section layout (example):
Offset  Symbol                      Level 2 target
──────  ──────────────────────────  ──────────────────
0x000   __CTOR_LIST__               → constructor list (first element = 0 means none)
0x010   __DTOR_LIST__               → destructor list (first element = 0 means none)
0x020   __xi_a                      → init flag array start
0x030   __dyn_tls_init_callback     → TLS init callback (NULL/0)
0x040   __image_base__              → image base address
0x050   __imp___initenv             → pointer to __initenv
0x090   __mingw_oldexcpt_handler    → exception handler
0x0A0   __native_startup_lock       → startup lock variable (0)
0x0B0   __native_startup_state      → startup state variable (0)
0x0C0   __xc_a                      → init flag array start
0x0D0   __xc_z                      → init flag array end
...
```

Each entry is a `uint64_t` (8 bytes). The CRT code loads the entry,
dereferences it, and uses the resulting value.

---

## 2. Why It Needs Patching

my_wine loads the PE at its preferred base address (single-process
model — no fork, no child process). The PE's `.data` and `.bss`
sections are mapped at their expected addresses.

However, the `.refptr` entries originally point to CRT globals that
exist inside the PE itself (within its `.bss` or `.CRT` sections).
In a normal Windows execution, these addresses are valid. In my_wine,
many of these CRT globals don't have meaningful content — or worse,
dereferencing them would access garbage memory.

If left untouched, the CRT startup code (`mainCRTStartup →
__getmainargs → _initterm`) would dereference these `.refptr` entries
and either crash or get garbage values.

**The fix:** Patch each `.refptr` entry to point to my_wine's own stub
variables (defined in `src/msvcrt/crt_globals.c`) instead of the PE's
original targets. These stubs are real global variables that contain
safe initial values (typically `0`).

---

## 3. The Stub Globals

`src/msvcrt/crt_globals.c` defines the stub variables that `.refptr`
entries are patched to point to. These provide safe two-level
indirection: the CRT loads the stub's address from `.refptr`, then
dereferences it to get a safe value.

### Zero-valued stubs

```c
uint64_t dyn_tls_callback_stub     = 0;   // TLS init callback: NULL
uint64_t mingw_excpt_handler_stub  = 0;   // Old exception handler: NULL
uint64_t xc_a_stub                 = 0;   // XC init range start
uint64_t xc_z_stub                 = 0;   // XC init range end
uint64_t xi_a_stub                 = 0;   // XI init range start
uint64_t xi_z_stub                 = 0;   // XI init range end
uint64_t native_startup_lock       = 0;   // Startup lock: 0 (unlocked)
int      native_startup_state      = 0;   // Startup state: 0
```

When the CRT does `mov rax, [refptr_entry]` → `mov rax, [rax]`, it
loads the stub's address, dereferences it, gets `0`, and proceeds
safely.

### Stub arrays for constructor/destructor lists

```c
uint32_t ctor_list_stub[] = { 0 };   // First element = 0 → no constructors
uint32_t dtor_list_stub[] = { 0 };   // First element = 0 → no destructors
```

`__do_global_ctors` reads the first element; if `0`, it means "empty
list" and the CRT skips the constructor loop.

### Other CRT globals

```c
int      __msvcrt_app_type       = 0;  // Console vs GUI app type
int      _commode                = 0;  // Communication mode
int      _fmode                  = 0;  // File mode (text/binary)
int      dowildcard_val          = 0;  // Wildcard expansion flag
int      newmode_val             = 0;  // New mode flag
char     _cmdline_storage[4096];          // Command line buffer
char    *_acmdln                 = _cmdline_storage;
void    **__imp___initenv_stub   = 0;   // Set dynamically → PE's .bss envp
```

### CRT context

```c
// From include/msvcrt.h
typedef struct {
    uint64_t image_base;         // Base address of the loaded PE
    uint64_t bss_vaddr;          // VirtualAddress of the .bss section
    uint32_t argc_bss_offset;    // Offset within .bss for argc (default 0x028)
    uint32_t argv_bss_offset;    // Offset within .bss for argv (default 0x020)
    uint32_t envp_bss_offset;    // Offset within .bss for envp (default 0x018)
} crt_context_t;

crt_context_t g_crt_ctx = { 0 };
```

`g_crt_ctx.image_base` and `g_crt_ctx.bss_vaddr` are set during
`patch_crt_refptrs()` and used throughout CRT setup.

---

## 4. How `patch_crt_refptrs` Works

`patch_crt_refptrs()` is defined in `src/msvcrt/crt_refptrs.c`. It
runs during loader initialization (called from `main.c` after the PE
is mapped, before the guest entry point is jumped to).

### The `refptr_mappings[]` Table

The function iterates over a static table of 18 symbol-to-stub mappings:

```c
const refptr_mapping_t refptr_mappings[] = {
    { "__CTOR_LIST__",              (void *)&ctor_list_stub },
    { "__DTOR_LIST__",              (void *)&dtor_list_stub },
    { "__xi_a",                     (void *)&xi_a_stub },
    { "__dyn_tls_init_callback",    (void *)&dyn_tls_callback_stub },
    { "__image_base__",             (void *)&g_crt_ctx.image_base },
    { "__imp___initenv",            (void *)&__imp___initenv_stub },
    { "__mingw_oldexcpt_handler",   (void *)&mingw_excpt_handler_stub },
    { "__native_startup_lock",      (void *)&native_startup_lock },
    { "__native_startup_state",     (void *)&native_startup_state },
    { "__xc_a",                     (void *)&xc_a_stub },
    { "__xc_z",                     (void *)&xc_z_stub },
    { "__xi_a (dup)",               (void *)&xi_a_stub },   // duplicate entry for some builds
    { "__xi_z",                     (void *)&xi_z_stub },
    { "_commode",                   (void *)&_commode },
    { "__imp__acmdln",              (void *)&_acmdln },
    { "_dowildcard",                (void *)&dowildcard_val },
    { "_fmode",                     (void *)&_fmode },
    { "_newmode",                   (void *)&newmode_val },
    { "mingw_app_type",             (void *)&__msvcrt_app_type },
    { NULL, NULL }                  // sentinel
};
```

### Three-Layer Discovery Strategy

The function uses three complementary strategies to find where each
refptr entry lives in the PE, tried in order from most specific to
most general.

#### Layer 1: COFF Symbol Table Lookup (Primary)

For each symbol name in `refptr_mappings[]`, the function calls
`find_symbol_rva_from_file()` (from `src/msvcrt/crt_offset_discovery.c`):

```c
uint64_t target_rva = find_symbol_rva_from_file(file_path, nt, sections, name);
```

This function:
1. Opens the **original PE file on disk** (via `file_path`)
2. Reads the COFF symbol table from `nt->FileHeader.PointerToSymbolTable`
3. Searches for a symbol matching the name with **multiple matching strategies**:
   - `.rdata$.refptr.<name>` — preferred (most specific)
   - `.refptr.<name>` — secondary
   - Exact name match — last resort (bare name may be in `.idata` with wrong address)
   - Substring match — fallback for truncated COFF entries
4. **Prefers section-bound symbols** over absolute symbols (section 0) to
   avoid garbage entries
5. Returns the RVA where the symbol lives

If found, `apply_refptr_patch()` is called immediately. This is the
most reliable method for PEs that retain COFF debug info.

#### Layer 2: `.rdata`/`.data` Section Value Scanning (Fallback)

For any mapping **not found by COFF**, the function scans `.rdata` and
`.data` sections for 8-byte values that point into the PE's `.bss`
section:

```c
/* Scan .rdata and .data for 8-byte values pointing into .bss */
const char *scan_names[] = { ".rdata", ".data", NULL };
for (int si = 0; scan_names[si]; si++) {
    // ... iterate over 8-byte aligned entries in section ...
    uint64_t val = *entry;
    if (val >= image_base + bss_start && val < image_base + bss_end) {
        // This entry points into .bss — likely a refptr
        // Match to next unpatched mapping (skipping __CTOR/__DTOR/__xi/__xc)
        apply_refptr_patch(image_base, sec_vaddr + off, map->target, map->name, image_size);
    }
}
```

This heuristic works because `.refptr` entries are typically placed in
`.rdata` and their values point to locations in `.bss` (or `.CRT`).
The scan skips mappings already handled by COFF lookup and avoids
matching constructor/destructor-related entries (those are `.CRT`, not
`.bss`).

#### Layer 3: `.text` Instruction Pattern Scan (Supplement)

If `__imp___initenv` was **not patched** by layers 1 or 2, the
function calls `scan_text_for_refptrs()` (from
`src/msvcrt/crt_offset_discovery.c`):

```c
if (!patched_initenv) {
    scan_text_for_refptrs(image_base, nt, sections, image_size);
}
```

This function scans `.text` for the specific two-level indirection
pattern used by `__imp___initenv`:

1. **Find `mov rax, [rip + disp32]`** (bytes `48 8B 05 disp32`) —
   this loads a refptr entry from a data section
2. **Look ahead up to 40 bytes** for the dereference pattern:
   `mov rax, [rax]` (bytes `48 8B 00`)
3. **Check for a write after the dereference**: a store to `[rax]`
   (patterns `89 00`, `C7 00`, or with REX prefix `48 89 00`, etc.)

The presence of a **store** after the dereference distinguishes
`__imp___initenv` (which does `mov r8, [rax]; mov [rax], r8` to
write `envp`) from other refptrs like callbacks (which deref, test,
and call).

When found, the target address is verified to be in a data section at
an 8-byte-aligned offset, and patched to point to
`&__imp___initenv_stub`.

### `__imp___initenv` Special Handling

`__imp___initenv` requires special treatment. The PE code does:

```asm
mov  rax, [rip + __imp___initenv_refptr]  ; load refptr → gets our stub
mov  rax, [rax]                            ; deref stub → gets PE's .bss+0x18
mov  [rax], r8                             ; write envp to .bss location
```

In `patch_crt_refptrs()`, the stub is set dynamically:

```c
__imp___initenv_stub = (void **)((char *)image_base +
                                  g_crt_ctx.bss_vaddr + CRT_BSS_INITENV);
```

Where `CRT_BSS_INITENV` is `0x018` (from `include/common.h`). This
points to the PE's own `.bss` location where `envp` is stored. The
stub acts as an indirection layer: the refptr entry points to
`__imp___initenv_stub`, and `__imp___initenv_stub` contains the
address of `.bss + 0x018`.

---

## 5. Applying Each Patch: `with_mprotect_rw`

Each individual patch is applied by `apply_refptr_patch()`:

```c
void apply_refptr_patch(void *image_base, uint64_t rva, void *target,
                        const char *name, uint64_t image_size)
{
    if (rva >= image_size) return;

    uint64_t *refptr = (uint64_t *)((char *)image_base + rva);
    char *page_start = (char *)((uint64_t)(char *)refptr & ~(uint64_t)PAGE_MASK);

    // Build a callback argument struct
    refptr_patch_arg.refptr = refptr;
    refptr_patch_arg.target = target;
    refptr_patch_arg.name   = name;
    refptr_patch_arg.rva    = rva;

    // Temporarily set page to RWX, call callback (writes the new value), restore
    if (with_mprotect_rw(page_start, PAGE_SIZE, refptr_patch_cb,
                         &refptr_patch_arg, PROT_READ) != 0) {
        perror("patch_crt_refptrs: with_mprotect_rw");
    }
}
```

The callback pattern from `src/common.c`:

```c
int with_mprotect_rw(void *addr, size_t len, void (*cb)(void *),
                     void *cb_arg, int restore_prot)
{
    void *aligned_addr = (void *)((uintptr_t)addr & ~PAGE_MASK);
    size_t total = (((uintptr_t)addr + len + PAGE_MASK) & ~PAGE_MASK)
                    - (uintptr_t)aligned_addr;

    if (mprotect(aligned_addr, total, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
        return -1;

    cb(cb_arg);   // Write the new value while page is RWX

    if (mprotect(aligned_addr, total, restore_prot) != 0)
        return -1;

    return 0;
}
```

The `.refptr` section is typically mapped as `PROT_READ` only. The
callback pattern atomically makes the page writable (and executable,
since some pages may be in `.text`), performs the write, then restores
the original protection.

---

## 6. The `__acrt_iob_func` Patch

A separate but related patching problem is `__acrt_iob_func`.

### The Problem

The PE has a `jmp *disp32(%rip)` thunk in `.text` that resolves through
the IAT to `__iob_func`. The thunk instruction is:

```asm
__acrt_iob_func:
    ff 25 disp32    ; jmp *disp32(%rip)  → jumps to IAT entry → __iob_func
```

The 6-byte `jmp` instruction is followed by padding, giving a total
of 15 bytes available for patching.

### The Fix

The patch is applied in **`src/loader/guest_setup.c`** inside
`patch_acrt_iob()`, which is called from `apply_final_patches()`
during `setup_guest_and_run()`. This is the single-process model —
no fork, no child process.

`find_text_thunk()` (from `src/loader/import_resolve.c`) scans `.text`
for all `ff 25 disp32` (jmp \*disp(%rip)) instructions and checks if
the dereferenced IAT pointer equals `__iob_func`. It returns the
absolute address of the matching thunk.

The thunk is then overwritten with:

```c
// Callback for with_mprotect_rw
static void acrt_iob_patch_cb(void *arg)
{
    uint8_t *code = (uint8_t *)arg;
    code[0]  = 0x48;           // REX.W
    code[1]  = 0xB8;           // movabs rax, imm64
    *(uint64_t *)(code + 2) = (uint64_t)(uintptr_t)__wine_iob_data();
    code[10] = 0xC3;           // ret
    for (int k = 11; k < 15; k++) code[k] = 0x90;  // NOP padding
}
```

The patched function simply does:

```asm
movabs rax, <__wine_iob_data()>
ret
nop
nop
nop
nop
```

This is a **15-byte overwrite** that bypasses the IAT jump entirely.
The page is made RWX via `with_mprotect_rw()` (with restore to
`PROT_READ | PROT_EXEC` for `.text`).

The `apply_iob_patch()` helper validates the opcode (`ff 25`),
checks bounds against the `.text` section end, and applies the patch.

### Call Chain

```
main.c::main()
  → load PE, call patch_crt_refptrs()  (refptr patching, in crt_refptrs.c)
  → setup_guest_and_run()  (in guest_setup.c)
       → setup_signal_handlers()
       → setup_seh_and_thunks()
       → parse_pe_headers()
       → apply_final_patches()
            → patch_acrt_iob()   ← __acrt_iob_func patch applied here
            → mprotect .bss writable
       → finalize_guest_state()
       → jump_to_guest()  (never returns)
```

---

## 7. Summary

| Component | File | Role |
|---|---|---|
| `refptr_mappings[]` | `src/msvcrt/crt_refptrs.c` | 18 symbol-to-stub mappings |
| `patch_crt_refptrs()` | `src/msvcrt/crt_refptrs.c` | Orchestrates 3-layer discovery + patching |
| `apply_refptr_patch()` | `src/msvcrt/crt_refptrs.c` | Applies individual patch via `with_mprotect_rw()` |
| `find_symbol_rva_from_file()` | `src/msvcrt/crt_offset_discovery.c` | COFF symbol table lookup (Layer 1) |
| Data section scanning | `src/msvcrt/crt_refptrs.c` | Value-based `.bss` pointer scan (Layer 2) |
| `scan_text_for_refptrs()` | `src/msvcrt/crt_offset_discovery.c` | `.text` instruction pattern scan (Layer 3) |
| `discover_crt_offsets()` | `src/msvcrt/crt_offset_discovery.c` | Finds argc/argv/envp `.bss` offsets |
| Stub globals | `src/msvcrt/crt_globals.c` | `ctor_list_stub`, `_fmode`, `g_crt_ctx`, etc. |
| `patch_acrt_iob()` | `src/loader/guest_setup.c` | Patches `__acrt_iob_func` jmp thunk |
| `find_text_thunk()` | `src/loader/import_resolve.c` | Finds `.text` jmp-thunk by IAT target |
| `with_mprotect_rw()` | `src/common.c` | Atomic RWX write with mprotect callback |
| `crt_context_t` | `include/msvcrt.h` | `image_base`, `bss_vaddr`, CRT offsets |
| Declarations | `src/msvcrt/msvcrt_priv.h` | Externs, `refptr_mapping_t`, function prototypes |

---

## Related Documents

- [Onboarding](onboarding.md) — Getting started guide and reading order
- [PE Format Primer](pe_format.md) — PE structure basics
- [Rationale](rationale.md) — Design decisions, requirements, limitations
- [Architecture](architecture.md) — How it works
