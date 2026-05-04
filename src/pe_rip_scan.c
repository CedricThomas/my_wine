/*
 * pe_rip_scan.c — RIP-relative jump scanning
 *
 * Scan .text for "ff 25 disp32" (jmp *disp(%rip)) instructions.
 * Used by import resolution to find IAT thunks.
 */

#include <stdint.h>

#include "include/pe.h"
#include "include/pe_parser.h"
#include "include/common.h"
#include "pe_priv.h"

int scan_rip_relative_jumps(void *image_base,
                            const IMAGE_NT_HEADERS64 *nt,
                            const IMAGE_SECTION_HEADER *sections,
                            int num_sections,
                            uint64_t *targets,
                            int max_targets)
{
    (void)num_sections;

    /* Find .text section */
    const IMAGE_SECTION_HEADER *text = find_section_by_name(nt, sections, ".text");
    if (text == NULL)
        return 0;

    uint64_t text_start = text->VirtualAddress;
    uint64_t text_end   = text_start + text->Misc.VirtualSize;
    if (text_end < text_start || text->Misc.VirtualSize > text->SizeOfRawData)
        text_end = text_start + text->SizeOfRawData;

    uint64_t text_size = text_end - text_start;

    /* Section too small for any valid instruction (need at least 6 bytes) */
    if (text_size < 6)
        return 0;

    uint8_t *text_base = (uint8_t *)image_base + text_start;
    int num_targets = 0;

    for (uint64_t off = 0; off + 6 <= text_size; off++) {
        if (text_base[off] == X86_JMP_RIP && text_base[off + 1] == X86_MOD_RIP) {
            int32_t disp = *(int32_t *)(text_base + off + 2);
            uint64_t instr_addr = text_start + off;
            uint64_t target = instr_addr + 6 + disp;

            /* Deduplicate */
            int dup = 0;
            for (int t = 0; t < num_targets; t++) {
                if (targets[t] == target) { dup = 1; break; }
            }
            if (!dup && num_targets < max_targets) {
                targets[num_targets++] = target;
            }
        }
    }

    /* Sort targets by address (insertion sort) */
    for (int i = 1; i < num_targets; i++) {
        uint64_t key = targets[i];
        int j = i - 1;
        while (j >= 0 && targets[j] > key) {
            targets[j + 1] = targets[j];
            j--;
        }
        targets[j + 1] = key;
    }

    return num_targets;
}

void *find_rip_relative_jump_to(void *image_base,
                                const IMAGE_NT_HEADERS64 *nt,
                                const IMAGE_SECTION_HEADER *sections,
                                int num_sections,
                                void *target_addr)
{
    /* Find .text section */
    const IMAGE_SECTION_HEADER *text = find_section_by_name(nt, sections, ".text");
    if (text == NULL)
        return NULL;

    uint64_t text_start = text->VirtualAddress;
    uint64_t text_end   = text_start + text->Misc.VirtualSize;
    if (text_end < text_start || text->Misc.VirtualSize > text->SizeOfRawData)
        text_end = text_start + text->SizeOfRawData;

    uint64_t text_size = text_end - text_start;

    /* Section too small for any valid instruction (need at least 6 bytes) */
    if (text_size < 6)
        return NULL;

    /* Compute image bounds from all sections for safe pointer dereference */
    uint64_t image_max = 0;
    for (int i = 0; i < num_sections; i++) {
        uint64_t s_end = sections[i].VirtualAddress + sections[i].Misc.VirtualSize;
        if (s_end > image_max)
            image_max = s_end;
    }

    uint8_t *text_base = (uint8_t *)image_base + text_start;
    uint64_t target_val = (uint64_t)(uintptr_t)target_addr;

    for (uint64_t off = 0; off + 6 <= text_size; off++) {
        if (text_base[off] == X86_JMP_RIP && text_base[off + 1] == X86_MOD_RIP) {
            int32_t disp = *(int32_t *)(text_base + off + 2);
            uint64_t instr_addr = text_start + off;
            uint64_t target_rva = instr_addr + 6 + disp;

            /* Bounds check: skip false matches that point outside the image */
            if (target_rva + 8 > image_max)
                continue;

            uint64_t *target_ptr = (uint64_t *)((char *)image_base + target_rva);
            if (*target_ptr == target_val) {
                return (void *)((char *)image_base + instr_addr);
            }
        }
    }
    return NULL;
}
