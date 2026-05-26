/*
 * pe32_bootstrap.c -- PE32 early bootstrap and setup helpers.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "include/common.h"
#include "include/crt.h"
#include "include/pe_parser.h"
#include "include/syscall_safe_utils.h"
#include "src/pe_priv.h"
#include "import_init.h"
#include "import_table.h"
#include "import_resolve.h"
#include "loader_state.h"
#include "pe32_bootstrap.h"
#include "pe32_process.h"
#include "../syscall/syscalls_inline.h"

extern char _acmdln[];
extern size_t _m_strnlen(const char *s, size_t n);
extern int _m_strncmp(const char *a, const char *b, size_t n);
void *map_image(const char *path, IMAGE_DOS_HEADER *out_dos,
                IMAGE_NT_HEADERS *out_nt, size_t *out_nt_size);

static const char err_map[]    = "my_wine32: failed to map PE image\n";
static const char err_import[] = "my_wine32: import resolution failed\n";

static const char *pe32_getenv(const char *key)
{
    extern char **environ;
    size_t klen = _m_strnlen(key, 4096);

    for (int i = 0; environ[i]; i++) {
        if (_m_strncmp(environ[i], key, klen) == 0 && environ[i][klen] == '=') {
            return environ[i] + klen + 1;
        }
    }
    return NULL;
}

const char *pe32_resolve_path_or_null(int argc, char **argv)
{
    if (argc > 1)
        return argv[1];
    return pe32_getenv("WINE32_PE_PATH");
}

void pe32_init_runtime_debug_level(void)
{
    const char *debug_level = pe32_getenv("MY_WINE_DEBUG_LEVEL");
    g_debug_level = parse_debug_level(debug_level);
}

void pe32_seed_command_line(const char *pe_path)
{
    syscall_safe_copy_str(_acmdln, pe_path, 256);
}

void *pe32_map_image_or_exit(const char *path, IMAGE_NT_HEADERS *out_nt)
{
    IMAGE_DOS_HEADER dos;
    size_t nt_size;
    void *base = map_image(path, &dos, out_nt, &nt_size);

    if (!base) {
        INLINE_SYSCALL_WRITE_ERR(err_map, sizeof(err_map) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }

    if (!pe_is_pe32(out_nt)) {
        const char err_type[] = "my_wine32: not a PE32 image\n";
        INLINE_SYSCALL_WRITE_ERR(err_type, sizeof(err_type) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }

    g_loader.is_32bit = 1;
    return base;
}

void pe32_activate_crt(void *image_base, IMAGE_NT_HEADERS *nt,
                       const char *pe_path)
{
    IMAGE_SECTION_HEADER *sections = get_image_sections(image_base, nt);
    crt_type_t crt_type = crt_detect_type(pe_path, nt);
    const crt_module_t *mod = crt_get_module(crt_type);

    crt_set_active(mod);
    crt_patch_refptrs(mod, pe_path, image_base, nt, sections);
}

void pe32_resolve_imports_or_exit(void *image_base, IMAGE_NT_HEADERS *nt)
{
    init_msvcrt_imports();
    init_import_table();
    if (resolve_imports(image_base, nt) != 0) {
        INLINE_SYSCALL_WRITE_ERR(err_import, sizeof(err_import) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }
}

void pe32_debug_verify_import_state(void *image_base, IMAGE_NT_HEADERS *nt)
{
    IMAGE_DATA_DIRECTORY imp_dir;
    void *ll_addr = (void *)(uintptr_t)0;
    void *ll_iat_ptr = NULL;

    if (g_debug_level < 3)
        return;

    {
        const char msg_iat[] =
            "pe32_entry: imports resolved, testing IAT entry\n";
        INLINE_SYSCALL_WRITE(2, msg_iat, sizeof(msg_iat) - 1);
    }

    if (pe_get_import_dir(nt, &imp_dir) && imp_dir.VirtualAddress != 0) {
        uint32_t import_rva = imp_dir.VirtualAddress;
        IMAGE_IMPORT_DESCRIPTOR *desc =
            pe_rva_to_ptr(image_base, nt, import_rva,
                          sizeof(IMAGE_IMPORT_DESCRIPTOR));
        uint32_t desc_offset = 0;
        while (desc != NULL &&
               desc_offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= imp_dir.Size &&
               desc->Name != 0) {
            const char *dll_name = pe_rva_to_ptr(image_base, nt, desc->Name, 1);
            if (dll_name == NULL)
                break;
            if (syscall_safe_strcasecmp(dll_name, "kernel32.dll") == 0) {
                uint32_t ilt_rva = desc->u1.OriginalFirstThunk != 0
                                   ? desc->u1.OriginalFirstThunk
                                   : desc->FirstThunk;
                uint8_t *orig_base = pe_rva_to_ptr(image_base, nt, ilt_rva,
                                                   sizeof(uint32_t));
                uint8_t *iat_base = pe_rva_to_ptr(image_base, nt,
                                                  desc->FirstThunk,
                                                  sizeof(uint32_t));
                if (orig_base == NULL || iat_base == NULL)
                    break;

                {
                    char buf[128];
                    int n = 0;
                    const char *p;
                    for (p = "IAT check: orig_base=0x"; *p && n < 120; ) buf[n++] = *p++;
                    for (int h = 7; h >= 0; h--) {
                        buf[n++] = "0123456789abcdef"[((uintptr_t)orig_base >> (h * 4)) & 0xf];
                    }
                    for (p = ", iat_base=0x"; *p && n < 120; ) buf[n++] = *p++;
                    for (int h = 7; h >= 0; h--) {
                        buf[n++] = "0123456789abcdef"[((uintptr_t)iat_base >> (h * 4)) & 0xf];
                    }
                    buf[n++] = '\n';
                    INLINE_SYSCALL_WRITE(2, buf, n);
                }

                for (int j = 0; ; j++) {
                    size_t thunk_off = (size_t)j * sizeof(uint32_t);
                    if (thunk_off / sizeof(uint32_t) != (size_t)j ||
                        thunk_off > SIZE_MAX - sizeof(uint32_t) ||
                        !pe_rva_range_is_valid(ilt_rva,
                                               thunk_off + sizeof(uint32_t),
                                               pe_size_of_image(nt)) ||
                        !pe_rva_range_is_valid(desc->FirstThunk,
                                               thunk_off + sizeof(uint32_t),
                                               pe_size_of_image(nt))) {
                        break;
                    }
                    uint32_t thunk_val = (uint32_t)*((uint32_t *)(orig_base + j * 4));
                    if (thunk_val == 0)
                        break;

                    {
                        char buf[80];
                        int n = 0;
                        const char *p;
                        for (p = "  ILT entry j="; *p && n < 70; ) buf[n++] = *p++;
                        { int d = n; int v = j;
                          if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
                          if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
                          n = d; }
                        for (p = ": thunk=0x"; *p && n < 70; ) buf[n++] = *p++;
                        for (int h = 7; h >= 0; h--) {
                            buf[n++] = "0123456789abcdef"[(thunk_val >> (h * 4)) & 0xf];
                        }
                        buf[n++] = '\n';
                        INLINE_SYSCALL_WRITE(2, buf, n);
                    }

                    if (thunk_val & 0x80000000)
                        continue;
                    IMAGE_IMPORT_BY_NAME *imp_name =
                        pe_rva_to_ptr(image_base, nt, thunk_val,
                                      sizeof(IMAGE_IMPORT_BY_NAME));
                    if (imp_name == NULL)
                        break;
                    {
                        const char *fname = (const char *)imp_name->Name;
                        if (fname[0] == 'L' && fname[1] == 'o' && fname[2] == 'a' &&
                            fname[3] == 'd' && fname[4] == 'L' && fname[5] == 'i' &&
                            fname[6] == 'b' && fname[7] == 'r' && fname[8] == 'a' &&
                            fname[9] == 'r' && fname[10] == 'y' &&
                            fname[11] == 'A' && fname[12] == '\0') {
                            ll_addr = (void *)(uintptr_t)*(uint32_t *)(iat_base + j * 4);
                            ll_iat_ptr = (void *)(iat_base + j * 4);
                            {
                                char buf[80];
                                int n = 0;
                                const char *p;
                                uint32_t iat_val = *(uint32_t *)(iat_base + j * 4);
                                for (p = "  IAT["; *p && n < 70; ) buf[n++] = *p++;
                                { int d = n; int v = j;
                                  if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
                                  if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
                                  n = d; }
                                for (p = "]=0x"; *p && n < 70; ) buf[n++] = *p++;
                                for (int h = 7; h >= 0; h--) {
                                    buf[n++] = "0123456789abcdef"[(iat_val >> (h * 4)) & 0xf];
                                }
                                buf[n++] = '\n';
                                INLINE_SYSCALL_WRITE(2, buf, n);
                            }
                            break;
                        }
                    }
                }
                if (ll_addr)
                    break;
            }
            desc_offset += sizeof(IMAGE_IMPORT_DESCRIPTOR);
            desc = pe_rva_to_ptr(image_base, nt, import_rva + desc_offset,
                                 sizeof(IMAGE_IMPORT_DESCRIPTOR));
        }
    }

    {
        char buf[48];
        int i = 0;
        const char *p = "IAT: LoadLibraryA=0x";
        while (*p) buf[i++] = *p++;
        for (int h = 7; h >= 0; h--) {
            uintptr_t v = (uintptr_t)ll_addr;
            buf[i++] = "0123456789abcdef"[(v >> (h * 4)) & 0xf];
        }
        buf[i++] = '\n';
        INLINE_SYSCALL_WRITE(2, buf, i);
    }

    if (ll_iat_ptr) {
        uint32_t final_val = *(uint32_t *)ll_iat_ptr;
        char buf[64];
        int i = 0;
        const char *p = "IAT: LoadLibraryA (final)=0x";
        while (*p) buf[i++] = *p++;
        for (int h = 7; h >= 0; h--) {
            buf[i++] = "0123456789abcdef"[(final_val >> (h * 4)) & 0xf];
        }
        buf[i++] = '\n';
        INLINE_SYSCALL_WRITE(2, buf, i);
    }

    {
        IMAGE_SECTION_HEADER *sections = get_image_sections(image_base, nt);
        int num_sections = pe_section_count(nt);
        uint64_t targets[MAX_THUNK_TARGETS];
        int num_targets = scan_rip_relative_jumps(image_base, nt, sections,
                                                  num_sections, targets,
                                                  MAX_THUNK_TARGETS);
        int zero_count = 0;
        for (int t = 0; t < num_targets; t++) {
            uint64_t target_rva = targets[t];
            if (target_rva > UINT32_MAX)
                continue;
            uint32_t *target_ptr = pe_rva_to_ptr(image_base, nt,
                                                 (uint32_t)target_rva,
                                                 sizeof(uint32_t));
            if (target_ptr == NULL)
                continue;
            if ((uint32_t)*target_ptr == 0x0) {
                zero_count++;
                char buf[80];
                int n = 0;
                const char *p = "  ZERO thunk target at IAT RVA 0x";
                for (; *p && n < 70; ) buf[n++] = *p++;
                for (int h = 7; h >= 0; h--) {
                    buf[n++] = "0123456789abcdef"[(target_rva >> (h * 4)) & 0xf];
                }
                buf[n++] = '\n';
                INLINE_SYSCALL_WRITE(2, buf, n);
            }
        }
        {
            char buf[80];
            int n = 0;
            const char *p = "IAT thunk scan: ";
            for (; *p && n < 70; ) buf[n++] = *p++;
            { int d = n; int v = num_targets;
              if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
              if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
              n = d; }
            for (p = " targets, "; *p && n < 70; ) buf[n++] = *p++;
            { int d = n; int v = zero_count;
              if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
              if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
              n = d; }
            for (p = " zero"; *p && n < 70; ) buf[n++] = *p++;
            buf[n++] = '\n';
            INLINE_SYSCALL_WRITE(2, buf, n);
        }
    }
}

void pe32_seed_crt_bss(void *image_base, IMAGE_NT_HEADERS *nt)
{
    IMAGE_SECTION_HEADER *sections = get_image_sections(image_base, nt);
    const crt_module_t *active = crt_get_active();

    if (crt_has_seed_bss(active)) {
        crt_seed_bss(active, image_base, nt, sections);
    } else {
        seed_pe32_bss_vars(image_base, nt);
    }
}
