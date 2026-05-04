#!/bin/bash
# gen_crt_offsets.sh — Generate CRT offset header from current mingw-w64 toolchain
# Usage: ./scripts/gen_crt_offsets.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
OUTPUT="$PROJECT_DIR/include/crt_offsets_generated.h"
SAMPLES_DIR="$PROJECT_DIR/samples"

echo "my_wine: generating CRT offsets from mingw-w64 toolchain..."

# Compile a minimal test program that forces the CRT to include all .refptr entries
TEST_SRC=$(mktemp -d)/test_crt.c
cat > "$TEST_SRC" << 'EOF'
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("Hello from mingw-w64!\n");
    return 0;
}
EOF

TEST_EXE=$(mktemp)/test_crt.exe

# Build via Docker (reuse the same container as samples)
if command -v docker &>/dev/null; then
    docker run --rm \
        -v "$(dirname "$TEST_SRC"):/work" \
        -w /work \
        ghcr.io/msys2/mingw:w64 \
        /bin/bash -c "gcc -O2 -o test_crt.exe test_crt.c" \
        || {
            echo "ERROR: mingw-w64 Docker build failed"
            rm -f "$TEST_EXE"
            exit 1
        }

    # Docker outputs to the mounted dir
    TEST_EXE=$(dirname "$TEST_SRC")/test_crt.exe
else
    echo "ERROR: Docker not found — cannot generate CRT offsets"
    echo "       Install Docker or skip this step (hardcoded fallbacks will be used)"
    exit 1
fi

# Now parse the PE to extract offsets
# Use a small C program that uses the existing PE parser
PARSER_SRC=$(mktemp -d)/parse_offsets.c
cat > "$PARSER_SRC" << 'CEOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define IMAGE_SIZEOF_SYMBOL 18

typedef struct {
    union {
        char    ShortName[8];
        struct { uint32_t Short; uint32_t Long; } LongName;
    } N;
    uint32_t    Value;
    int32_t     SectionNumber;
    uint16_t    Type;
    uint8_t     StorageClass;
    uint8_t     NumberOfAuxSymbols;
} IMAGE_SYMBOL;

static const char *get_symbol_name(const IMAGE_SYMBOL *sym, char *string_table) {
    if (sym->N.LongName.Short != 0 || sym->N.LongName.Long != 0) {
        if (string_table) {
            uint32_t off = sym->N.LongName.Short + 4;
            return string_table + off;
        }
    }
    return sym->N.ShortName;
}

int main(int argc, char *argv[]) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <pe_file>\n", argv[0]); return 1; }

    const char *path = argv[1];
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror("open"); return 1; }

    struct stat st;
    fstat(fd, &st);
    void *map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) { perror("mmap"); return 1; }

    // Parse DOS header
    uint32_t e_lfanew = *((uint32_t *)((char *)map + 0x3C));

    // Parse NT headers
    uint32_t pe_sig = *((uint32_t *)((char *)map + e_lfanew));
    if (pe_sig != 0x00004550) { fprintf(stderr, "Not a PE\n"); return 1; }

    uint16_t num_symbols = *((uint16_t *)((char *)map + e_lfanew + 4 + 2));
    uint32_t sym_ptr = *((uint32_t *)((char *)map + e_lfanew + 4 + 2 + 4));

    if (sym_ptr == 0 || num_symbols == 0) {
        fprintf(stderr, "No COFF symbol table\n");
        munmap(map, st.st_size);
        return 1;
    }

    IMAGE_SYMBOL *symbols = (IMAGE_SYMBOL *)((char *)map + sym_ptr);
    size_t sym_size = num_symbols * IMAGE_SIZEOF_SYMBOL;
    uint32_t str_off = sym_ptr + sym_size;
    char *string_table = NULL;
    if (str_off + 4 <= st.st_size) {
        uint32_t str_size = *((uint32_t *)((char *)map + str_off));
        if (str_off + 4 + str_size <= st.st_size)
            string_table = (char *)map + str_off + 4;
    }

    // Find section virtual addresses
    uint16_t num_sections = *((uint16_t *)((char *)map + e_lfanew + 4 + 2));
    uint32_t sec_ptr = e_lfanew + 4 + 2 + 2 + 20;  // after FileHeader + OptionalHeader header
    uint16_t opt_hdr_size = *((uint16_t *)((char *)map + e_lfanew + 4 + 2));
    sec_ptr = e_lfanew + 4 + 2 + 2 + opt_hdr_size;

    // Section headers: Name[8] + VirtualAddress + ...
    uint64_t section_vaddrs[96];
    uint64_t section_sizes[96];
    for (int i = 0; i < num_sections && i < 96; i++) {
        section_vaddrs[i] = *((uint32_t *)((char *)map + sec_ptr + i * 40 + 12));
        section_sizes[i] = *((uint32_t *)((char *)map + sec_ptr + i * 40 + 16));
    }

    // Search for .refptr symbols and extract their .bss offsets
    uint32_t bss_vaddr = 0;
    for (int i = 0; i < num_sections && i < 96; i++) {
        char name[9] = {0};
        memcpy(name, (char *)map + sec_ptr + i * 40, 8);
        if (strcmp(name, ".bss") == 0) {
            bss_vaddr = section_vaddrs[i];
            break;
        }
    }

    if (bss_vaddr == 0) {
        fprintf(stderr, "ERROR: .bss section not found\n");
        munmap(map, st.st_size);
        return 1;
    }

    // Target symbols (in order of output)
    const char *targets[] = {
        "__initenv",   // -> CRT_BSS_INITENV
        "__argv",      // -> CRT_BSS_ARGV
        "__argc",      // -> CRT_BSS_ARGC
        "__environ",   // -> CRT_BSS_ENVP
    };
    const char *fallback_targets[] = {
        "_initenv",    // alternative name
        "_argv",       // alternative name
        "_argc",       // alternative name
        "_environ",    // alternative name
    };
    const char *define_names[] = {
        "CRT_BSS_INITENV",
        "CRT_BSS_ARGV",
        "CRT_BSS_ARGC",
        "CRT_BSS_ENVP"
    };
    uint32_t results[4] = {0, 0, 0, 0};

    for (int t = 0; t < 4; t++) {
        for (int i = 0; i < (int)num_symbols; i++) {
            IMAGE_SYMBOL *sym = &symbols[i];
            const char *name = get_symbol_name(sym, string_table);
            if (!name) continue;

            // Check for .refptr prefix or bare name
            int matched = 0;
            if (strstr(name, ".refptr.") != NULL && strstr(name, targets[t]) != NULL)
                matched = 1;
            else if (strstr(name, ".rdata$.refptr.") != NULL && strstr(name, targets[t]) != NULL)
                matched = 1;
            else if (strcmp(name, targets[t]) == 0)
                matched = 1;
            else if (strcmp(name, fallback_targets[t]) == 0)
                matched = 1;

            if (!matched) continue;

            // Prefer section-bound symbols
            if (sym->SectionNumber > 0 && sym->SectionNumber <= num_sections) {
                uint32_t rva = section_vaddrs[sym->SectionNumber - 1] + sym->Value;
                uint32_t offset = rva - bss_vaddr;
                if (results[t] == 0 || offset < results[t]) {
                    results[t] = offset;
                }
            }
        }
    }

    // Generate the header
    printf("/* Auto-generated by scripts/gen_crt_offsets.sh — DO NOT EDIT */\n");
    printf("/* CRT BSS offsets extracted from current mingw-w64 toolchain */\n\n");
    printf("#ifndef MY_WINE_CRT_OFFSETS_GENERATED_H\n");
    printf("#define MY_WINE_CRT_OFFSETS_GENERATED_H\n\n");
    for (int t = 0; t < 4; t++) {
        printf("#define %s          0x%03x   /* from current mingw-w64 */\n",
               define_names[t], results[t]);
    }
    printf("\n#endif /* MY_WINE_CRT_OFFSETS_GENERATED_H */\n");

    munmap(map, st.st_size);
    return 0;
}
CEOF

# Compile the parser
PARSER_EXE=$(mktemp)/parse_offsets
gcc -O2 -o "$PARSER_EXE" "$PARSER_SRC" || { echo "ERROR: parser compilation failed"; exit 1; }

# Run the parser on the test PE
"$PARSER_EXE" "$TEST_EXE" > "$OUTPUT"
echo "" >> "$OUTPUT"

# Clean up
rm -f "$PARSER_EXE" "$PARSER_SRC" "$TEST_EXE"
rm -rf "$(dirname "$TEST_SRC")"

echo "Generated: $OUTPUT"
echo "Contents:"
cat "$OUTPUT"
