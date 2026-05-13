# Improvements for Later

> Non-blocking enhancements that can be tackled after the core PE32 path works.

---

## WoW64 In-Process Migration: ABORTED

The in-process mode-switching approach (GDT setup + `lcall` + dual-stack) was studied
but abandoned. The dual-process fork+exec model is the permanent architecture.

---

## my_wine_32: Switch from Static to Dynamic Linking

**Problem:** `my_wine_32` is built with `-static -no-pie -Wl,--no-dynamic-linker`,
pulling the entire glibc static archive into the binary. Result: 1.2MB binary
(~885KB text is glibc internals that are never called).

**Current (Makefile line 127):**
```make
@$(MY_WINE_32_CC) -static -no-pie -o my_wine_32 $(MY_WINE_32_OBJS) \
	-nostartfiles -Wl,--no-dynamic-linker -lpthread \
	-Wl,--defsym=_DYNAMIC=0
```

**Change to:**
```make
@$(MY_WINE_32_CC) -no-pie -o my_wine_32 $(MY_WINE_32_OBJS) -lpthread
```

Remove `-static`, `-Wl,--no-dynamic-linker`, `-Wl,--defsym=_DYNAMIC=0`.

**Expected result:** Binary drops from ~1.2MB to ~200-300KB. The linker
delegates `libc.so.6` and `ld-linux.so.2` resolution to the dynamic loader.

**Safety:** The 32-bit code path always switches FS→TEB as the last step
before guest entry. All glibc calls (`mmap`, `setenv`, `qsort`) happen
while FS still points to glibc TLS. No glibc is called after the switch.

**Trade-off:** Requires `glibc.i686` (Arch: `glibc` multilib provides `/usr/lib32/`)
on any machine that runs the binary. Static linking was chosen for portability
(distribute `my_wine_32` without glibc dependency). If you only run on your
own machine, dynamic is fine. Revert to static if cross-machine portability
is ever needed.

**Prerequisites on Arch:** `/lib/ld-linux.so.2` and `/usr/lib32/libc.so.6`
must exist. Already present on multilib-enabled Arch installs.

---

## musl_malloc_32_compat.c vs glibc malloc

**Problem:** The 32-bit build links `musl_malloc_32_compat.c` (a custom
mmap-based allocator), but the `-static` glibc linkage also pulls in
glibc's `malloc` arena machinery. The musl allocator code is compiled
but may never actually be used — glibc's `malloc` is the default
allocator for any glibc function that allocates.

**Future option:** If we switch to dynamic linking (above), glibc malloc
is available via the shared library and the musl shim becomes irrelevant
unless we actively route `malloc` calls to it.

**Alternative path (high effort):** Replace glibc entirely with musl libc
for the 32-bit build. This gives a small static binary (~100KB) with
no TLS issues. Requires: musl-i686 toolchain, rewriting `#include` paths,
adapting syscall wrappers. Not worth the effort for this project scope.

---

## Add `--gc-sections` Linker Flag

**If staying static:** Adding `-Wl,--gc-sections` + `-ffunction-sections -fdata-sections`
to the 32-bit compile flags would let the linker discard unused glibc sections,
shrinking the static binary significantly (estimated 40-60% reduction).
Requires marking all used symbols with `__attribute__((used))`.

---

## Generalize PE32 beyond DOOM95

The current 32-bit path is optimized for DOOM95 (Watcom CRT, `D_DoomMain` entry).
For broader PE32 support, consider:

- Auto-detect CRT type (Watcom vs MinGW vs MSVC) and select entry strategy
- Implement a minimal `HeapAlloc`/`HeapCreate` so Win32 heap APIs work
- Add `LoadLibraryA`/`GetProcAddress` stubs for dynamic DLL loading
- Implement `GetModuleHandle` to return the PE base

-------

NEW enhancements

-------

Add a wrapper called my_wine and then 2 binaries called my_wine32 and my_wine64 that are more specialized and have the same features / behaviors (same env usage, same arg pparsing etc)


--------

Try to use the DEBUG flag on all tests and samples to see if it crash.
Try to use the DEBUG mode in the AI workflow too 

--------

Unify the samples output for better readability
=> sample.info should have an expected output field and we should compare it top the real output
Have a single test output with the sum of all cases instead of several lists ?
