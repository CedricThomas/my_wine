#!/bin/bash
# gen_crt_offsets.sh — Generate CRT offset header from current mingw-w64 toolchain
# Usage: ./scripts/gen_crt_offsets.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
OUTPUT="$PROJECT_DIR/include/crt_offsets_generated.h"
SAMPLES_DIR="$PROJECT_DIR/samples"

echo "my_wine: generating CRT offsets from mingw-w64 toolchain..."

# Create a work directory inside the project (so Docker can see it through the sandbox)
WORK_DIR=$(mktemp -d "$PROJECT_DIR/.gen_crt_offsets.XXXXXX")

# Compile a minimal test program that forces the CRT to include all .refptr entries
# Note: __environ doesn't exist in mingw-w64 as a local CRT BSS symbol
# (it's an msvcrt.dll import), so we only reference the ones that do.
TEST_SRC="$WORK_DIR/test_crt.c"
cat > "$TEST_SRC" << 'EOF'
#include <stdio.h>
#include <stdlib.h>

// Force the CRT .refptr symbols into the binary
extern int __argc;
extern char **__argv;
extern char ***__initenv;

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("argc=%d, __argc=%d\n", argc, __argc);
    printf("__argv=%p\n", (void *)__argv);
    printf("__initenv=%p\n", (void *)__initenv);
    return 0;
}
EOF

TEST_EXE="$WORK_DIR/test_crt.exe"

# Build via Docker (reuse the my_wine-samples image)
if command -v docker &>/dev/null; then
    # Ensure the my_wine-samples image exists; build it if not
    if ! docker image inspect my_wine-samples &>/dev/null; then
        echo "my_wine: building my_wine-samples image from samples/Dockerfile..."
        DOCKER_BUILDKIT=0 docker build -t my_wine-samples "$PROJECT_DIR/samples" \
            || { echo "ERROR: Failed to build my_wine-samples image"; rm -rf "$WORK_DIR"; exit 1; }
    fi

    docker run --rm \
        -v "$WORK_DIR:/work" \
        -w /work \
        my_wine-samples \
        /bin/bash -c "x86_64-w64-mingw32-gcc -O2 -o test_crt.exe test_crt.c" \
        || {
            echo "ERROR: mingw-w64 Docker build failed"
            rm -rf "$WORK_DIR"
            exit 1
        }
else
    echo "ERROR: Docker not found — cannot generate CRT offsets"
    echo "       Install Docker or skip this step (hardcoded fallbacks will be used)"
    rm -rf "$WORK_DIR"
    exit 1
fi

# Now parse the PE to extract offsets
# PE/COFF symbol records are exactly IMAGE_SIZEOF_SYMBOL = 18 bytes each.
# Layout: name(8) + value(4) + sectionNumber(4) + type(2) = 18
# There are NO StorageClass or NumberOfAuxSymbols fields in the 18-byte record.
# Aux records also occupy 18 bytes each and are interleaved with regular symbols.
# We iterate through ALL 18-byte records and match by name + section.
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

// PE/COFF symbol: exactly 18 bytes.
// Name is either short (bytes 0-7) or long (bytes 0-3 = string table offset, bytes 4-7 = 0).
typedef struct __attribute__((packed)) {
    union {
        char    name[8];
        struct { uint32_t short_part; uint32_t long_part; } long_name;
    } N;
    uint32_t    Value;
    int32_t     SectionNumber;
    uint16_t    Type;
} IMAGE_SYMBOL;

static const char *get_symbol_name(const IMAGE_SYMBOL *sym, char *string_table) {
    // Long name: first dword is offset into string table, second dword is 0
    if (sym->N.long_name.short_part != 0 && sym->N.long_name.long_part == 0) {
        if (string_table) {
            uint32_t off = sym->N.long_name.short_part + 4;
            return string_table + off;
        }
    }
    // Short name: up to 8 chars
    if (sym->N.name[0] != 0)
        return sym->N.name;
    return NULL;
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

    uint32_t e_lfanew = *((uint32_t *)((char *)map + 0x3C));
    if (*((uint32_t *)((char *)map + e_lfanew)) != 0x00004550) {
        fprintf(stderr, "Not a PE\n"); munmap(map, st.st_size); return 1;
    }

    uint16_t num_sections = *((uint16_t *)((char *)map + e_lfanew + 6));
    uint32_t sym_ptr = *((uint32_t *)((char *)map + e_lfanew + 12));
    uint32_t num_symbols = *((uint32_t *)((char *)map + e_lfanew + 16));
    uint16_t opt_hdr_size = *((uint16_t *)((char *)map + e_lfanew + 20));

    if (sym_ptr == 0 || num_symbols == 0) {
        fprintf(stderr, "No COFF symbol table\n");
        munmap(map, st.st_size);
        return 1;
    }

    IMAGE_SYMBOL *symbols = (IMAGE_SYMBOL *)((char *)map + sym_ptr);
    // String table follows all num_symbols * 18-byte records
    uint32_t str_off = sym_ptr + num_symbols * IMAGE_SIZEOF_SYMBOL;
    char *string_table = NULL;
    if (str_off + 4 <= st.st_size) {
        uint32_t str_size = *((uint32_t *)((char *)map + str_off));
        if (str_off + 4 + str_size <= st.st_size)
            string_table = (char *)map + str_off + 4;
    }

    // Section virtual addresses
    uint32_t sec_ptr = e_lfanew + 4 + 20 + opt_hdr_size;
    uint64_t section_vaddrs[96];
    uint32_t bss_vaddr = 0;
    for (int i = 0; i < num_sections && i < 96; i++) {
        section_vaddrs[i] = *((uint32_t *)((char *)map + sec_ptr + i * 40 + 12));
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

    // Target symbols: the COFF names in mingw-w64's .bss are:
    //   envp  (offset 0x018) - the CRT environment pointer variable
    //   argv  (offset 0x020) - the CRT argv pointer variable
    //   argc  (offset 0x028) - the CRT argc int variable
    // __initenv is NOT in .bss (it's an msvcrt.dll import),
    // so CRT_BSS_INITENV falls back to the same value as envp.
    const char *defines[] = {"CRT_BSS_INITENV", "CRT_BSS_ARGV", "CRT_BSS_ARGC", "CRT_BSS_ENVP"};
    uint32_t results[4] = {0, 0, 0, 0};

    // Scan all 18-byte records looking for our target names in .bss
    for (int i = 0; i < (int)num_symbols; i++) {
        IMAGE_SYMBOL *sym = &symbols[i];
        const char *name = get_symbol_name(sym, string_table);
        if (!name || sym->SectionNumber <= 0 || sym->SectionNumber > num_sections)
            continue;

        // Check if this symbol is in the .bss section
        uint32_t rva = section_vaddrs[sym->SectionNumber - 1] + sym->Value;
        uint32_t bss_offset = rva - bss_vaddr;

        if (strcmp(name, "argv") == 0) results[1] = bss_offset;
        else if (strcmp(name, "argc") == 0) results[2] = bss_offset;
        else if (strcmp(name, "envp") == 0) results[3] = bss_offset;
    }

    // CRT_BSS_INITENV falls back to CRT_BSS_ENVP since __initenv is an import
    if (results[0] == 0) results[0] = results[3];

    // Generate header
    printf("/* Auto-generated by scripts/gen_crt_offsets.sh \xe2\x80\x94 DO NOT EDIT */\n");
    printf("/* CRT BSS offsets extracted from current mingw-w64 toolchain */\n\n");
    printf("#ifndef MY_WINE_CRT_OFFSETS_GENERATED_H\n");
    printf("#define MY_WINE_CRT_OFFSETS_GENERATED_H\n\n");
    for (int t = 0; t < 4; t++) {
        printf("#define %-20s 0x%03x   /* from current mingw-w64 */\n",
               defines[t], results[t]);
    }
    printf("\n#endif /* MY_WINE_CRT_OFFSETS_GENERATED_H */\n");

    munmap(map, st.st_size);
    return 0;
}
CEOF

# Compile the parser
PARSER_EXE=$(mktemp -d)/parse_offsets
gcc -O2 -o "$PARSER_EXE" "$PARSER_SRC" || { echo "ERROR: parser compilation failed"; exit 1; }

# Run the parser on the test PE
"$PARSER_EXE" "$TEST_EXE" > "$OUTPUT"
echo "" >> "$OUTPUT"

# Clean up
rm -f "$PARSER_EXE"
rm -rf "$WORK_DIR" "$(dirname "$PARSER_SRC")"

echo "Generated: $OUTPUT"
echo "Contents:"
cat "$OUTPUT"
