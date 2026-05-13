/*
 * pe32_trampoline.h — Low-memory trampoline system for PE32 images.
 *
 * For PE32 (32-bit) guests, IAT entries are 4-byte pointers. When import
 * resolution writes a stub address (e.g. 0x55555555f2a0) into the IAT,
 * the upper 32 bits are truncated and the guest jumps to garbage.
 *
 * This module allocates executable pages below 4GB and provides:
 * - A ret32 trampoline (32-bit code) for the 64→32 mode switch
 * - Far-pointer structures for mode-switching far jumps
 * - A generic wrapper (64-bit code) for 32→64 mode switching
 * - A fixup stub for restoring ESP and returning to 32-bit callers
 * - Support for thunk_wrapper slots used by thunk_gen.c
 *
 * For PE32, IAT entries are 4-byte pointers. pe32_trampoline_for_addr()
 * returns target directly if < 4GB, or NULL if >= 4GB (the caller must
 * use thunk_gen.c wrappers for mode switching).
 *
 * The blob also contains a ret32 trampoline (32-bit code) and far-pointer
 * structures, all below 4GB so the far-jump offsets fit in 32 bits.
 *
 * Far jumps use ljmp [disp] (FF 25) instead of ljmp [reg] (FF 28)
 * because ljmp [reg] with REX.W crashes on AMD CPUs in 64-bit mode.
 *
 * 32→64→32 switching (no lfaret — invalid in 64-bit mode):
 *   Stage 1 (32-bit): save ESP to a slot, ljmp to 64-bit target
 *   Stage 2 (64-bit): write thunk return addr to a slot, ljmp to fixup
 *   Fixup (32-bit): restore ESP from slot, read return addr, jmp to it
 *
 * Blob layout (all below 4GB, DATA before CODE then trampolines):
 *
 * Data section:
 *   [0x000..0x017]  ret32 code (24 bytes): set DS/FS, call entry, ljmp to 64-bit
 *   [0x018..0x01B]  entry_tramp slot (4 bytes): entry trampoline address
 *   [0x01C..0x01F]  far64_ptr slot (4 bytes): pointer to far64_struct
 *   [0x020..0x029]  far64_call struct (10 bytes): offset(8) + selector(2) for ljmp
 *   [0x02A..0x02D]  esp_save slot (4 bytes): saved ESP for fixup stub
 *   [0x02E..0x031]  far32_ret_addr slot (4 bytes): 32-bit return address for ljmp
 *   [0x032..0x03B]  fixup_far32 struct (10 bytes): offset(8) + selector(2) for ljmp
 *   [0x03C..0x041]  blob_far32 (6 bytes): offset(4) + selector(2) for 64→32 ljmp [reg]
 *   [0x042]         padding (1 byte)
 *   [0x043]         ret32_instr (1 byte): ret instruction (for far32_data path)
 *
 * Code section:
 *   [0x046..0x095]  generic wrapper (~80 bytes): 32→64→32 mode switch (Stage 1+Stage 2)
 *   [0x096..0x0A5]  fixup stub (~16 bytes): restore ESP, read ret addr, add esp, pop, jmp
 *   [0x0A6..0x0B1]  back64_tramp (12 bytes): mov rax, back64_addr; jmp rax
 *   [0x0B2..0x0B7]  far64_struct (6 bytes): offset(4) + selector(2) = 0x0033
 *   [0x0B8..0x0B9]  crash_stub (2 bytes): ud2 (0x0F 0x0B) for unresolved import crash
 *
 * Trampoline section:
 *   [0x0BA..]       per-target trampolines (11 bytes each) + data slots (8 bytes each)
 *
 * Per-target entry (19 bytes):
 *   [0..10]  trampoline (11 bytes): push slot; call wrapper; ret
 *   [11..18] data slot (8 bytes): target address
 *
 * far64 struct (6 bytes) — read by 32-bit ljmp [disp32]:
 *   bytes 0-3: offset (uint32_t, back64_tramp addr)
 *   bytes 4-5: selector (0x0033 = 64-bit code segment)
 *
 * far32 struct (in pe32_trampoline.c .data, patched at runtime):
 *   bytes 0-7: offset (uint64_t, ret32 addr, zero-extended)
 *   bytes 8-9: selector (0x0023 = 32-bit code segment)
 *
 * ret32 code (generated in low memory, runs in 32-bit compat mode, 24 bytes):
 *   66 B8 2B 00           mov ax, 0x002B  ; load GDT user data selector into AX
 *   8E D8                 mov ds, ax      ; set DS = flat 32-bit data segment
 *   66 B8 2B 00           mov ax, 0x002B  ; same selector for FS
 *   8E E0                 mov fs, ax      ; set FS = flat 32-bit data segment
 *   8B 03                 mov eax, [ebx]  ; read PE entry function pointer
 *   FF D0                 call eax        ; call PE entry
 *   89 C1                 mov ecx, eax    ; save return value
 *   57                    push edi        ; preserve EDI for far return
 *   0F 25 <disp32>        ljmp [disp32]   ; FAR jump to far64_struct (64-bit CS)
 *
 * generic_wrapper (generated in low memory, 32→64→32 mode switch):
 *   Stage 1 (32-bit): read target from slot, write to far64_call, save ESP,
 *                     push 64-bit return addr, ljmp to target (64-bit mode)
 *   Stage 2 (64-bit): read PE return addr from [rbp+16], write to far32_ret_addr,
 *                     ljmp to fixup_far32 (back to 32-bit mode)
 *
 * fixup stub (generated in low memory, runs in 32-bit mode, ~16 bytes):
 *   8B 05 <disp32>        mov eax, [esp_save_slot]   ; restore saved ESP
 *   8B E0                 mov esp, eax                ; restore ESP
 *   83 C4 10              add esp, 16                 ; skip old stack frame
 *   5A                    pop edx                     ; pop PE return addr
 *   FF E2                 jmp edx                     ; return to caller
 *
 * crash_stub (2 bytes at offset 0x0B8):
 *   0F 0B                 ud2 — triggers SIGILL for unresolved imports
 *
 * Per-target data slots (8 bytes each, for thunk_gen.c and trampolines):
 *   [0..7]  target_addr (8 bytes): the 64-bit target address
 *
 * For PE32+ images (non-32-bit mode), trampoline_for_addr() returns target unchanged.
 */

#ifndef MY_WINE_PE32_TRAMPOLINE_H
#define MY_WINE_PE32_TRAMPOLINE_H

#include <stddef.h>

/* Initialize the trampoline pool and generate the ret32 trampoline.
 * Call once before any lookups. No-op if !g_is_32bit or already initialized. */
void pe32_trampoline_init(void);

/* far32_data: the 10-byte far-pointer struct patched at runtime.
 * run_guest_32.S does ljmp [r10] to jump into 32-bit mode using this struct.
 * The caller of run_guest_32 must pre-load r10 with &far32_data. */
extern uint8_t far32_data[];

/* Return a <4GB address that jumps to `target`.
 * In PE32 mode: if target < 4GB, returns target directly (PE entry points are
 *   in the image, reachable from 32-bit code). If target >= 4GB, generates a
 *   trampoline. Returns the trampoline address.
 * In PE32+ mode: returns target unchanged (no truncation issue).
 * If target is NULL, returns NULL.
 * Lazy-init: auto-creates the trampoline pool on first call. */
void *pe32_trampoline_for_addr(void *target);

/* Return the trampoline blob pointer (NULL if allocation failed or !32-bit). */
void *pe32_trampoline_blob(void);

/* Return the address of the generic wrapper (NULL if allocation failed or !32-bit).
 * Used by thunk_gen.c to call the wrapper from 32-bit thunks. */
void *pe32_trampoline_wrapper_addr(void);

/* Return the address of the 6-byte blob_far32 struct in the trampoline blob.
 * This struct contains a 32-bit ret32 offset + 16-bit selector (0x0023).
 * Used by run_guest_32.S for the 64→32 mode switch via ljmp [reg]. */
void *pe32_trampoline_blob_far32(void);

/* Patch the ret32 trampoline and set up all far-jump structures.
 * entry_tramp: the <4GB trampoline address to call for the PE entry point.
 * back64_addr: the address of the back64 label in run_guest_32.S
 *              (used to generate the back64_tramp and far64_struct).
 *
 * Patches far32_offset (in run_guest_32.S) with ret32 address,
 * generates back64_tramp: mov rax, back64_addr; jmp rax,
 * and generates far64_struct with offset=back64_tramp, selector=0x0033.
 *
 * Returns the address of the entry_tramp slot (pass as first arg to run_guest_32). */
void *pe32_patch_ret32(void *entry_tramp, void *back64_addr);

/* Return the address of the crash stub (ud2 instruction) in the trampoline blob.
 * This is a deterministic fault point for unresolved imports — when an IAT entry
 * cannot be resolved, writing this address ensures a clear SIGILL with a known
 * PC instead of jumping to garbage (e.g. truncated 64-bit address like 0x41cab000).
 * Returns NULL if the trampoline blob wasn't allocated. */
void *pe32_trampoline_crash_stub_addr(void);

/* Unmap the trampoline pool. Call during guest cleanup. */
void cleanup_pe32_trampoline_pages(void);

#endif /* MY_WINE_PE32_TRAMPOLINE_H */
