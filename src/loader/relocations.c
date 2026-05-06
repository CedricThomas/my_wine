/*
 * relocations.c — Apply base relocations to a mapped PE image
 *
 * Iterates the BASE_RELOC table and patches 64-bit relocation
 * entries (DIR64) by adding delta to the referenced address.
 * ABSOLUTE entries are no-ops (padding).
 */

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#include "include/pe.h"
#include "include/nt_constants.h"
#include "loader_priv.h"

/**
 * Apply base relocations to a mapped PE image.
 *
 * Iterates the BASE_RELOC table (DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC])
 * and patches 64-bit relocation entries (DIR64) by adding delta to the
 * referenced address. ABSOLUTE entries are no-ops (padding).
 *
 * @param base   The actual mapped base address
 * @param nt     Pointer to the parsed NT headers (valid struct, not pointer into image)
 * @return 0 on success, -1 if relocations are stripped and delta != 0
 */
int apply_relocations(void *base, IMAGE_NT_HEADERS64 *nt)
{
    uintptr_t delta = (uintptr_t)base - nt->OptionalHeader.ImageBase;

    /* Already at preferred base — nothing to do */
    if (delta == 0) {
        return 0;
    }

    /* Relocations stripped but loaded at non-preferred base */
    if (nt->FileHeader.Characteristics & IMAGE_FILE_RELOCS_STRIPPED) {
        fprintf(stderr,
                "Error: relocations stripped but image loaded at non-preferred base\n");
        return -1;
    }

    /* Get the relocation directory entry */
    IMAGE_DATA_DIRECTORY *dir =
        &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];

    /* No relocation table present */
    if (dir->VirtualAddress == 0 || dir->Size == 0) {
        return 0;
    }

    /* Iterate relocation blocks */
    const uint8_t *block_ptr = (const uint8_t *)base + dir->VirtualAddress;
    const uint8_t *block_end = block_ptr + dir->Size;

    while (block_ptr < block_end) {
        IMAGE_BASE_RELOCATION *block = (IMAGE_BASE_RELOCATION *)block_ptr;

        /* Stop at zero-sized block (shouldn't happen but be safe) */
        if (block->sizeOfBlock == 0) {
            break;
        }

        uint32_t va   = block->virtualAddress;
        uint32_t size = block->sizeOfBlock;

        /* Number of relocation entries in this block */
        uint32_t num_entries =
            (size - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(uint16_t);

        const uint16_t *entries = (const uint16_t *)((const uint8_t *)block + sizeof(IMAGE_BASE_RELOCATION));

        for (uint32_t i = 0; i < num_entries; i++) {
            uint16_t type   = IMAGE_REL_ENTRY_TYPE(entries[i]);
            uint16_t offset = IMAGE_REL_ENTRY_OFFSET(entries[i]);

            if (type == IMAGE_REL_BASED_DIR64) {
                uint64_t *target = (uint64_t *)((char *)base + va + IMAGE_REL_ENTRY_OFFSET(entries[i]));
                *target += delta;
            } else if (type == IMAGE_REL_BASED_ABSOLUTE) {
                /* Padding / no-op */
                continue;
            } else {
                fprintf(stderr,
                        "Unsupported relocation type 0x%04X at RVA 0x%08X\n",
                        type, va + IMAGE_REL_ENTRY_OFFSET(entries[i]));
                continue;
            }
        }

        block_ptr += size;
    }

    return 0;
}
