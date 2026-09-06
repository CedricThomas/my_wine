/*
 * relocations.c — Apply base relocations to a mapped PE image
 *
 * Iterates the BASE_RELOC table and patches relocation entries:
 *   - PE32  (DIR32, HIGH, LOW, HIGHLOW): patches with appropriate delta parts
 *   - PE32+ (IMAGE_REL_BASED_DIR64): patches uint64_t values with 64-bit delta
 * ABSOLUTE entries are no-ops (padding).
 *
 * PE32 relocation types (from the PE/COFF spec):
 *   IMAGE_REL_BASED_ABSOLUTE  0  — padding / no-op
 *   IMAGE_REL_BASED_HIGH      1  — add high 16 bits of delta
 *   IMAGE_REL_BASED_LOW       2  — add low 16 bits of delta
 *   IMAGE_REL_BASED_HIGHLOW   3  — add full 32-bit delta (same as DIR32)
 *   IMAGE_REL_BASED_DIR32     4  — add full 32-bit delta
 *   IMAGE_REL_BASED_HIGHADJ   5  — 16-bit adjustment (treated as HIGH + warning)
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "include/nt_constants.h"
#include "include/pe.h"
#include "src/pe_priv.h"
#include "loader_priv.h"

#ifndef IMAGE_REL_BASED_ABSOLUTE
#define IMAGE_REL_BASED_ABSOLUTE 0x0000
#endif

#ifndef IMAGE_REL_BASED_HIGH
#define IMAGE_REL_BASED_HIGH 0x0001
#endif

#ifndef IMAGE_REL_BASED_LOW
#define IMAGE_REL_BASED_LOW 0x0002
#endif

#ifndef IMAGE_REL_BASED_HIGHLOW
#define IMAGE_REL_BASED_HIGHLOW 0x0003
#endif

#ifndef IMAGE_REL_BASED_DIR32
#define IMAGE_REL_BASED_DIR32 0x0004
#endif

#ifndef IMAGE_REL_BASED_HIGHADJ
#define IMAGE_REL_BASED_HIGHADJ 0x0005
#endif

#ifndef IMAGE_REL_BASED_DIR64
#define IMAGE_REL_BASED_DIR64 0x000A
#endif

/**
 * Apply base relocations to a mapped PE image.
 *
 * Iterates the BASE_RELOC table (DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC])
 * and patches relocation entries by adding delta to the referenced address.
 * ABSOLUTE entries are no-ops (padding).
 *
 * For PE32 images, handles DIR32, HIGH, LOW, and HIGHLOW relocation types.
 * For PE32+ images, handles DIR64 relocation type.
 *
 * @param base   The actual mapped base address
 * @param nt     Pointer to the parsed NT headers (valid struct, not pointer into image)
 * @return 0 on success, -1 if relocations are stripped and delta != 0
 */
int apply_relocations(void *base, IMAGE_NT_HEADERS *nt)
{
    uintptr_t delta = (uintptr_t)base - pe_image_base(nt);

    /* Already at preferred base — nothing to do */
    if (delta == 0) {
        return 0;
    }

    /* Relocations stripped but loaded at non-preferred base */
    if (pe_characteristics(nt) & IMAGE_FILE_RELOCS_STRIPPED) {
        fprintf(stderr,
                "Error: relocations stripped but image loaded at non-preferred base\n");
        return -1;
    }

    /* Get the relocation directory entry */
    IMAGE_DATA_DIRECTORY dir;
    if (!pe_get_basereloc_dir(nt, &dir)) {
        return 0;
    }
    const IMAGE_DATA_DIRECTORY *p_dir = &dir;

    /* No relocation table present */
    if (p_dir->VirtualAddress == 0 || p_dir->Size == 0) {
        return 0;
    }

    /* Iterate relocation blocks */
    const uint8_t *block_ptr = pe_rva_to_const_ptr(base, nt,
                                                   p_dir->VirtualAddress,
                                                   p_dir->Size);
    if (block_ptr == NULL) {
        fprintf(stderr, "Error: relocation directory exceeds image bounds\n");
        return -1;
    }
    const uint8_t *block_end = block_ptr + p_dir->Size;

    while (block_ptr < block_end) {
        IMAGE_BASE_RELOCATION *block = (IMAGE_BASE_RELOCATION *)block_ptr;

        /* Stop at zero-sized or malformed block */
        if (block->sizeOfBlock == 0 ||
            block->sizeOfBlock < sizeof(IMAGE_BASE_RELOCATION)) {
            break;
        }

        uint32_t va   = block->virtualAddress;
        uint32_t size = block->sizeOfBlock;
        if ((size_t)(block_end - block_ptr) < size) {
            fprintf(stderr, "Error: relocation block exceeds directory bounds\n");
            return -1;
        }

        /* Number of relocation entries in this block. */
        /* PE spec: entry count = (sizeOfBlock - 8) / 2 */
        uint32_t num_entries =
            (size - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(uint16_t);

        const uint16_t *entries =
            (const uint16_t *)((const uint8_t *)block +
                               sizeof(IMAGE_BASE_RELOCATION));

        bool is_pe32 = pe_is_pe32(nt);

        for (uint32_t i = 0; i < num_entries; i++) {
            uint16_t type   = IMAGE_REL_ENTRY_TYPE(entries[i]);
            uint16_t offset = IMAGE_REL_ENTRY_OFFSET(entries[i]);

            if (type == IMAGE_REL_BASED_ABSOLUTE) {
                /* Padding / no-op */
                continue;
            }

            if (is_pe32) {
                if (va > UINT32_MAX - offset) {
                    fprintf(stderr,
                            "Error: PE32 relocation target RVA overflow "
                            "at VA 0x%x+0x%x\n",
                            va, offset);
                    return -1;
                }
                switch (type) {
                    case IMAGE_REL_BASED_DIR32:
                        /* Add full 32-bit delta */
                    case IMAGE_REL_BASED_HIGHLOW: {
                        uint32_t *target = pe_rva_to_ptr(base, nt, va + offset,
                                                         sizeof(uint32_t));
                        if (target == NULL) {
                            fprintf(stderr,
                                    "Error: PE32 relocation target out of bounds "
                                    "at VA 0x%x+0x%x\n",
                                    va, offset);
                            return -1;
                        }
                        *target += (uint32_t)delta;
                        break;
                    }
                    case IMAGE_REL_BASED_HIGH: {
                        /* Add high 16 bits of delta */
                        uint16_t *target = pe_rva_to_ptr(base, nt, va + offset,
                                                         sizeof(uint16_t));
                        if (target == NULL) {
                            fprintf(stderr,
                                    "Error: PE32 relocation target out of bounds "
                                    "at VA 0x%x+0x%x\n",
                                    va, offset);
                            return -1;
                        }
                        *target += (uint16_t)(delta >> 16);
                        break;
                    }
                    case IMAGE_REL_BASED_LOW: {
                        /* Add low 16 bits of delta */
                        uint16_t *target = pe_rva_to_ptr(base, nt, va + offset,
                                                         sizeof(uint16_t));
                        if (target == NULL) {
                            fprintf(stderr,
                                    "Error: PE32 relocation target out of bounds "
                                    "at VA 0x%x+0x%x\n",
                                    va, offset);
                            return -1;
                        }
                        *target += (uint16_t)(delta & 0xFFFF);
                        break;
                    }
                    case IMAGE_REL_BASED_HIGHADJ: {
                        /*
                         * HIGHADJ is a 16-bit adjustment that pairs with
                         * a preceding HIGH entry.  Treat as HIGH for safety
                         * with a warning.
                         */
                        uint16_t *target = pe_rva_to_ptr(base, nt, va + offset,
                                                         sizeof(uint16_t));
                        if (target == NULL) {
                            fprintf(stderr,
                                    "Error: PE32 relocation target out of bounds "
                                    "at VA 0x%x+0x%x\n",
                                    va, offset);
                            return -1;
                        }
                        fprintf(stderr,
                                "WARNING: treating HIGHADJ as HIGH for PE32 "
                                "relocation type 0x%x at VA 0x%x+0x%x\n",
                                type, va, offset);
                        *target += (uint16_t)(delta >> 16);
                        break;
                    }
                    default:
                        fprintf(stderr,
                                "WARNING: unsupported PE32 relocation type "
                                "0x%x at VA 0x%x+0x%x\n",
                                type, va, offset);
                        break;
                }
            } else {
                /* PE32+ */
                if (type == IMAGE_REL_BASED_DIR64) {
                    if (va > UINT32_MAX - offset) {
                        fprintf(stderr,
                                "Error: PE32+ relocation target RVA overflow "
                                "at VA 0x%x+0x%x\n",
                                va, offset);
                        return -1;
                    }
                    uint64_t *target = pe_rva_to_ptr(base, nt, va + offset,
                                                     sizeof(uint64_t));
                    if (target == NULL) {
                        fprintf(stderr,
                                "Error: PE32+ relocation target out of bounds "
                                "at VA 0x%x+0x%x\n",
                                va, offset);
                        return -1;
                    }
                    *target += delta;
                } else {
                    fprintf(stderr,
                            "WARNING: unsupported PE32+ relocation type "
                            "0x%x at VA 0x%x+0x%x\n",
                            type, va, offset);
                }
            }
        }

        block_ptr += size;
    }

    return 0;
}
