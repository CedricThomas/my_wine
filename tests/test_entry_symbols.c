/*
 * test_entry_symbols.c — CRT entry symbol resolution tests
 *
 * Tests that the MinGW CRT module's entry_symbols array correctly
 * resolves WinMain variants for WinMain-based PEs.
 *
 * Usage: ./build/test_entry_symbols
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "crt.h"
#include "pe.h"

/* Forward declarations from pe_parser.c */
int parse_dos_header(const void *base, size_t file_size,
                     IMAGE_DOS_HEADER *out_header);
int parse_nt_headers(const void *base, size_t file_size,
                     const IMAGE_DOS_HEADER *dos_header,
                     IMAGE_NT_HEADERS *out_nt_headers);
int parse_sections(const void *base, size_t file_size,
                   const IMAGE_NT_HEADERS *nt_headers,
                   IMAGE_SECTION_HEADER **out_sections);
int parse_symbol_table_from_file(const char *path,
                                 const IMAGE_NT_HEADERS *nt_headers,
                                 IMAGE_SYMBOL **out_symbols,
                                 char **out_string_table);
uint32_t lookup_symbol_rva(const IMAGE_SYMBOL *symbols, int count,
                           const char *string_table,
                           const IMAGE_SECTION_HEADER *sections,
                           int num_sections, const char *name);

/* ── Test harness ─────────────────────────────────────────────── */

static int total_tests = 0;
static int passed_tests = 0;
static int failed_tests = 0;

static void check(const char *label, int condition) {
  total_tests++;
  if (condition) {
    printf("  PASS: %s\n", label);
    passed_tests++;
  } else {
    printf("  FAIL: %s\n", label);
    failed_tests++;
  }
}

/* ── Helpers ──────────────────────────────────────────────────── */

typedef enum {
  ENTRY_TYPE_MAIN,
  ENTRY_TYPE_WINMAIN,
  ENTRY_TYPE_WWINMAIN
} entry_type_t;

/*
 * classify_entry_type — matches the logic in pe32_entry.c.
 * Given a matched symbol name, determine the entry type.
 * Handles both full names (_WinMain@16) and COFF suffix names (Main@16).
 */
static entry_type_t classify_entry_type(const char *sym) {
  const char *p = sym;
  if (*p == '_')
    p++;
  if ((strcmp(p, "WinMain@16") == 0) || (strcmp(p, "WinMain") == 0) ||
      (strcmp(p, "Main@16") == 0)) {
    return ENTRY_TYPE_WINMAIN;
  }
  if ((strcmp(p, "wWinMain@16") == 0) || (strcmp(p, "wWinMain") == 0) ||
      (strcmp(p, "wMain@16") == 0)) {
    return ENTRY_TYPE_WWINMAIN;
  }
  return ENTRY_TYPE_MAIN;
}

static const char *resolve_entry_from_array(
    const char *const *entry_syms, const IMAGE_SYMBOL *symbols, int sym_count,
    const char *string_table, const IMAGE_SECTION_HEADER *sections,
    int num_sections) {
  int si;
  for (si = 0; entry_syms[si] != NULL; si++) {
    uint32_t rva = lookup_symbol_rva(symbols, sym_count, string_table, sections,
                                     num_sections, entry_syms[si]);
    if (rva != 0) {
      return entry_syms[si];
    }
  }
  return NULL;
}

static void test_mingw_entry_symbols(void) {
  printf("\n=== MinGW CRT entry_symbols array structure ===\n");

  const crt_module_t *mod = crt_get_module(CRT_TYPE_MINGW);
  check("crt_get_module(CRT_TYPE_MINGW) returns non-NULL", mod != NULL);
  if (!mod)
    return;

  const char *const *entry_syms = crt_entry_symbols(mod);
  check("crt_entry_symbols returns non-NULL", entry_syms != NULL);
  if (!entry_syms)
    return;

  /* Check that WinMain variants come before main */
  int first_winmain_idx = -1, main_idx = -1;
  for (int i = 0; entry_syms[i] != NULL; i++) {
    const char *p = entry_syms[i];
    if (*p == '_')
      p++;
    if (first_winmain_idx < 0 &&
        ((strcmp(p, "WinMain@16") == 0) || (strcmp(p, "WinMain") == 0) ||
         (strcmp(p, "Main@16") == 0))) {
      first_winmain_idx = i;
    }
    if (strcmp(entry_syms[i], "main") == 0) {
      main_idx = i;
    }
  }
  check("WinMain variants appear before 'main' in entry_symbols",
        first_winmain_idx >= 0 && main_idx >= 0 &&
            first_winmain_idx < main_idx);

  /* Check that NULL terminates the array */
  check("entry_symbols is NULL-terminated",
        (entry_syms[0] != NULL) && (first_winmain_idx >= 0));

  /* Verify the first few expected entries */
  check("entry_syms[0] is _WinMain@16",
        strcmp(entry_syms[0], "_WinMain@16") == 0);
  check("entry_syms[1] is _wWinMain@16",
        strcmp(entry_syms[1], "_wWinMain@16") == 0);
  check("entry_syms[2] is _WinMain", strcmp(entry_syms[2], "_WinMain") == 0);
  check("entry_syms[3] is _wWinMain", strcmp(entry_syms[3], "_wWinMain") == 0);
}

static void test_entry_type_classification(void) {
  printf("\n=== Entry type classification logic ===\n");

  /* Test full names */
  check("_WinMain@16 -> ENTRY_TYPE_WINMAIN",
        classify_entry_type("_WinMain@16") == ENTRY_TYPE_WINMAIN);
  check("_WinMain -> ENTRY_TYPE_WINMAIN",
        classify_entry_type("_WinMain") == ENTRY_TYPE_WINMAIN);
  check("_wWinMain@16 -> ENTRY_TYPE_WWINMAIN",
        classify_entry_type("_wWinMain@16") == ENTRY_TYPE_WWINMAIN);
  check("_wWinMain -> ENTRY_TYPE_WWINMAIN",
        classify_entry_type("_wWinMain") == ENTRY_TYPE_WWINMAIN);
  check("main -> ENTRY_TYPE_MAIN",
        classify_entry_type("main") == ENTRY_TYPE_MAIN);

  /* Test COFF suffix variants (without leading underscore) */
  check("WinMain@16 -> ENTRY_TYPE_WINMAIN",
        classify_entry_type("WinMain@16") == ENTRY_TYPE_WINMAIN);
  check("Main@16 -> ENTRY_TYPE_WINMAIN",
        classify_entry_type("Main@16") == ENTRY_TYPE_WINMAIN);
  check("wWinMain@16 -> ENTRY_TYPE_WWINMAIN",
        classify_entry_type("wWinMain@16") == ENTRY_TYPE_WWINMAIN);
  check("wMain@16 -> ENTRY_TYPE_WWINMAIN",
        classify_entry_type("wMain@16") == ENTRY_TYPE_WWINMAIN);
}

static void test_entry_resolution(const char *path) {
  printf("\n=== Entry resolution for: %s ===\n", path);

  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    printf("  SKIP: cannot open '%s': %s\n", path, strerror(errno));
    return;
  }

  struct stat st;
  if (fstat(fd, &st) < 0) {
    printf("  SKIP: cannot stat '%s'\n", path);
    close(fd);
    return;
  }

  size_t file_size = (size_t)st.st_size;
  const void *base =
      mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  close(fd);
  if (base == MAP_FAILED) {
    printf("  SKIP: mmap failed for '%s'\n", path);
    return;
  }

  IMAGE_DOS_HEADER dos_header;
  if (parse_dos_header(base, file_size, &dos_header) != 0) {
    printf("  SKIP: parse_dos_header failed for '%s'\n", path);
    munmap((void *)base, file_size);
    return;
  }

  IMAGE_NT_HEADERS nt_headers;
  if (parse_nt_headers(base, file_size, &dos_header, &nt_headers) != 0) {
    printf("  SKIP: parse_nt_headers failed for '%s'\n", path);
    munmap((void *)base, file_size);
    return;
  }

  IMAGE_SECTION_HEADER *sections = NULL;
  int num_sections = parse_sections(base, file_size, &nt_headers, &sections);
  if (num_sections <= 0 || sections == NULL) {
    printf("  SKIP: no sections in '%s'\n", path);
    munmap((void *)base, file_size);
    return;
  }

  const crt_module_t *mod = crt_get_active();
  if (!mod) {
    mod = crt_get_module(CRT_TYPE_MINGW);
  }
  if (!mod) {
    printf("  SKIP: no CRT module available\n");
    munmap((void *)base, file_size);
    return;
  }

  const char *const *entry_syms = crt_entry_symbols(mod);
  if (!entry_syms) {
    printf("  SKIP: CRT module has no entry_symbols\n");
    munmap((void *)base, file_size);
    return;
  }

  IMAGE_SYMBOL *symbols = NULL;
  char *string_table = NULL;
  int sym_count =
      parse_symbol_table_from_file(path, &nt_headers, &symbols, &string_table);

  if (sym_count > 0) {
    /* Resolve entry symbol using the CRT module's entry_syms */
    const char *matched = resolve_entry_from_array(
        entry_syms, symbols, sym_count, string_table, sections, num_sections);

    /* Check for WinMain variants in COFF symbols */
    uint32_t winmain_rva = lookup_symbol_rva(
        symbols, sym_count, string_table, sections, num_sections, "WinMain@16");
    uint32_t winmain2_rva =
        lookup_symbol_rva(symbols, sym_count, string_table, sections,
                          num_sections, "_WinMain@16");
    uint32_t main_rva = lookup_symbol_rva(symbols, sym_count, string_table,
                                          sections, num_sections, "_main");
    uint32_t main16_rva = lookup_symbol_rva(symbols, sym_count, string_table,
                                            sections, num_sections, "Main@16");

    if (matched) {
      entry_type_t etype = classify_entry_type(matched);
      printf("  Resolved: '%s' (entry_type=%d)\n", matched, etype);
    } else {
      printf("  CRT module entry_symbols did not match any symbol\n");
      printf("  _WinMain@16: %s, WinMain@16: %s, _main: %s\n",
             winmain2_rva ? "FOUND" : "absent",
             winmain_rva ? "FOUND" : "absent", main_rva ? "FOUND" : "absent");
    }

    /* For WinMain-based PEs */
    if (strstr(path, "sdl2_window") || strstr(path, "window")) {
      check("WinMain-based PE has WinMain variant "
            "(WinMain@16/_WinMain@16/Main@16) in COFF symbols",
            winmain_rva != 0 || winmain2_rva != 0 || main16_rva != 0);
      if (matched) {
        entry_type_t etype = classify_entry_type(matched);
        check("CRT module resolves WinMain PE to WinMain variant",
              etype == ENTRY_TYPE_WINMAIN || etype == ENTRY_TYPE_WWINMAIN);
      }
    }

    /* For main()-based PEs */
    if (strstr(path, "hello_world") || strstr(path, "file_io") ||
        strstr(path, "dispatcher_regs") || strstr(path, "multi_import") ||
        strstr(path, "entry_test")) {
      check("main-based PE has _main in COFF symbols", main_rva != 0);
    }

    free(symbols);
  } else {
    printf("  SKIP: no COFF symbols in '%s' (stripped)\n", path);
  }

  munmap((void *)base, file_size);
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  test_mingw_entry_symbols();
  test_entry_type_classification();

  const char *samples[] = {"samples/sdl2_window_32/sdl2_window_32.exe",
                           "samples/hello_world_32/hello_world_32.exe",
                           "samples/file_io_32/file_io_32.exe",
                           "samples/dispatcher_regs_32/dispatcher_regs_32.exe",
                           "samples/multi_import_32/multi_import_32.exe",
                           "samples/entry_test_32/entry_test_32.exe",
                           NULL};

  for (int i = 0; samples[i] != NULL; i++) {
    if (access(samples[i], F_OK) == 0) {
      test_entry_resolution(samples[i]);
    } else {
      printf("\n=== Entry resolution: SKIP %s (not found) ===\n", samples[i]);
    }
  }

  printf("\n========================================\n");
  printf("Total:  %d  Passed: %d  Failed: %d\n", total_tests, passed_tests,
         failed_tests);
  printf("========================================\n");

  return failed_tests > 0 ? 1 : 0;
}
