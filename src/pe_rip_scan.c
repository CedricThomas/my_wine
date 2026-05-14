/*
 * pe_rip_scan.c — RIP-relative / absolute jump scanning
 *
 * Scan .text for:
 *   PE32+ : "ff 25 disp32" (jmp *disp(%rip)) — RIP-relative indirect jump
 *   PE32  : "ff 15 disp32" (jmp *disp32)     — absolute indirect jump
 *
 * Used by import resolution to find IAT thunks.
 */

#include <stdint.h>

#include "include/pe.h"
#include "include/pe_parser.h"
#include "include/common.h"
#include "include/pe_priv.h"

/* Callback receives (offset_in_text, target_rva). Return true to continue, false to stop. */
typedef bool (*rip_scan_callback)(uint64_t offset, uint64_t target_rva, void *user_data);

/*
 * Shared scanning loop: find .text, iterate bytes, detect ff 25/ff 15 jumps,
 * compute target RVA, and call the callback for each match.
 */
static int _scan_rip_jumps(void *image_base,
                           const IMAGE_NT_HEADERS *nt,
                           const IMAGE_SECTION_HEADER *sections,
                           int num_sections,
                           rip_scan_callback cb,
                           void *user_data)
{
    (void)num_sections;
    /* Find .text section */
    const IMAGE_SECTION_HEADER *text = find_section_by_name(nt, sections, ".text");
    if (text == NULL)
        return 0;

    uint64_t text_start = text->VirtualAddress;
    uint64_t text_len = text->Misc.VirtualSize;
    if (text_len == 0 || text_len > text->SizeOfRawData)
        text_len = text->SizeOfRawData;
    if (text_len > UINT32_MAX || text_start > UINT32_MAX - text_len)
        return 0;

    uint64_t text_size = text_len;

    /* Section too small for any valid instruction (need at least 6 bytes) */
    if (text_size < 6)
        return 0;

    uint8_t *text_base = pe_rva_to_ptr(image_base, nt, (uint32_t)text_start,
                                       (size_t)text_size);
    if (text_base == NULL)
        return 0;
    bool is32 = pe_is_pe32(nt);
    int count = 0;

    for (uint64_t off = 0; off + 6 <= text_size; off++) {
        if (text_base[off] != X86_JMP_RIP)
            continue;
        uint8_t mod = text_base[off + 1];
        if (is32) {
            /* PE32: mingw generates ff 25 (absolute disp32 in 32-bit mode),
             * not ff 15. Accept both for robustness. */
            if (mod != 0x15 && mod != 0x25)
                continue;
        } else {
            /* PE32+: ff 25 (RIP-relative) */
            if (mod != 0x25)
                continue;
        }
        int32_t disp = *(int32_t *)(text_base + off + 2);
        uint64_t target_rva;

        if (is32) {
            /* PE32: disp is an absolute 32-bit address.
             * Convert to RVA by subtracting the actual mapped base. The
             * operand is relocated when the image is not at its preferred
             * ImageBase, so using OptionalHeader.ImageBase is only correct
             * before relocations or for preferred-base mappings. */
            uint32_t abs_addr = (uint32_t)(uint32_t)disp;
            uint32_t mapped_base = (uint32_t)(uintptr_t)image_base;
            target_rva = (uint64_t)(abs_addr - mapped_base);
        } else {
            /* PE32+: RIP-relative. target = instr_addr + 6 + disp (RVA) */
            uint64_t instr_addr = text_start + off;
            target_rva = instr_addr + 6 + disp;
        }

        if (target_rva > UINT32_MAX ||
            !pe_rva_range_is_valid((uint32_t)target_rva,
                                   is32 ? sizeof(uint32_t) : sizeof(uint64_t),
                                   pe_size_of_image(nt))) {
            continue;
        }

        if (!cb(off, target_rva, user_data))
            break;
        count++;
    }

    return count;
}

/* ------------------------------------------------------------------ */
/* scan_rip_relative_jumps callback data                              */
/* ------------------------------------------------------------------ */
typedef struct {
    uint64_t *targets;
    int max_targets;
    int num_targets;
} rip_scan_collect_ctx;

static bool rip_scan_collect_cb(uint64_t offset, uint64_t target_rva, void *user_data)
{
    (void)offset;
    rip_scan_collect_ctx *ctx = (rip_scan_collect_ctx *)user_data;

    /* Deduplicate */
    for (int t = 0; t < ctx->num_targets; t++) {
        if (ctx->targets[t] == target_rva)
            return true;
    }
    if (ctx->num_targets < ctx->max_targets) {
        ctx->targets[ctx->num_targets++] = target_rva;
    }
    return true;
}

int scan_rip_relative_jumps(void *image_base,
                            const IMAGE_NT_HEADERS *nt,
                            const IMAGE_SECTION_HEADER *sections,
                            int num_sections,
                            uint64_t *targets,
                            int max_targets)
{
    rip_scan_collect_ctx ctx = { .targets = targets, .max_targets = max_targets, .num_targets = 0 };
    _scan_rip_jumps(image_base, nt, sections, num_sections, rip_scan_collect_cb, &ctx);

    /* Sort targets by address (insertion sort) */
    for (int i = 1; i < ctx.num_targets; i++) {
        uint64_t key = targets[i];
        int j = i - 1;
        while (j >= 0 && targets[j] > key) {
            targets[j + 1] = targets[j];
            j--;
        }
        targets[j + 1] = key;
    }

    return ctx.num_targets;
}

/* ------------------------------------------------------------------ */
/* find_rip_relative_jump_to callback data                            */
/* ------------------------------------------------------------------ */
typedef struct {
    void *image_base;
    bool is32;
    uint64_t target_val;
    uint64_t text_start;
    const IMAGE_NT_HEADERS *nt;
    void **result;
} rip_scan_find_ctx;

static bool rip_scan_find_cb(uint64_t offset, uint64_t target_rva, void *user_data)
{
    rip_scan_find_ctx *ctx = (rip_scan_find_ctx *)user_data;

    /* Bounds check: PE32 uses 4-byte IAT entries, PE32+ uses 8-byte */
    if (ctx->is32) {
        uint32_t *target_ptr = pe_rva_to_ptr(ctx->image_base, ctx->nt,
                                             (uint32_t)target_rva,
                                             sizeof(uint32_t));
        if (target_ptr == NULL)
            return true;
        if ((uint64_t)(uint32_t)*target_ptr == (uint64_t)(uint32_t)ctx->target_val) {
            *ctx->result = pe_rva_to_ptr(ctx->image_base, ctx->nt,
                                         (uint32_t)(ctx->text_start + offset), 1);
            return false;  /* stop scanning */
        }
    } else {
        uint64_t *target_ptr = pe_rva_to_ptr(ctx->image_base, ctx->nt,
                                             (uint32_t)target_rva,
                                             sizeof(uint64_t));
        if (target_ptr == NULL)
            return true;
        if (*target_ptr == ctx->target_val) {
            *ctx->result = pe_rva_to_ptr(ctx->image_base, ctx->nt,
                                         (uint32_t)(ctx->text_start + offset), 1);
            return false;  /* stop scanning */
        }
    }
    return true;
}

void *find_rip_relative_jump_to(void *image_base,
                                const IMAGE_NT_HEADERS *nt,
                                const IMAGE_SECTION_HEADER *sections,
                                int num_sections,
                                void *target_addr)
{
    /* Find .text section */
    const IMAGE_SECTION_HEADER *text = find_section_by_name(nt, sections, ".text");
    if (text == NULL)
        return NULL;

    rip_scan_find_ctx ctx = {
        .image_base = image_base,
        .is32 = pe_is_pe32(nt),
        .target_val = (uint64_t)(uintptr_t)target_addr,
        .text_start = text->VirtualAddress,
        .nt = nt,
        .result = NULL
    };

    void *result = NULL;
    ctx.result = &result;
    _scan_rip_jumps(image_base, nt, sections, num_sections, rip_scan_find_cb, &ctx);
    return result;
}
