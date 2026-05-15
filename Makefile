# ── Toolchain ───────────────────────────────────────────────────
CC       = gcc
CFLAGS   = -Wall -Wextra -Werror -O2 -g -I. -Iinclude -MMD -MP -mno-sse
LDFLAGS  = -lrt -lpthread -ldl

# Special flags for entry points, loader core, stubs, syscall infra.
SPECIAL_CFLAGS = $(CFLAGS) -mno-red-zone -fno-stack-protector -fno-exceptions

# 32-bit child binary toolchain. my_wine32 is a standalone 32-bit ELF that
# loads PE32 images and is dynamically linked with glibc CRT.
MY_WINE32_CC = $(CC) -m32
MY_WINE32_CFLAGS = $(CFLAGS) -DMY_WINE32 -mno-red-zone -fno-stack-protector \
	-fno-exceptions -mno-sse -fno-pie -no-pie -fno-builtin \
	-Werror

# ── Directories ─────────────────────────────────────────────────
BUILDDIR = build
BUILDDIR32 = build32

# ── Generated File Policy ───────────────────────────────────────
# Required generated files are ignored by git but regenerated automatically
# from tracked inputs.
GENERATED_REQUIRED = src/syscall/dispatcher_generated.c

# ── Source Groups: Wrapper And PE32+ ────────────────────────────
# Auto-discover .c per source group; objects flatten into build/.
ROOT_SRC     = $(filter-out src/wrapper_main.c, $(sort $(shell find src/   -maxdepth 1 -name '*.c')))
STUBS_SRC    = $(filter-out src/msvcrt/crt_32_stub.c, \
		$(sort $(shell find src/msvcrt   -maxdepth 1 -name '*.c')))
LOADER_SRC   = $(sort $(shell find src/loader  -maxdepth 1 -name '*.c' | grep -v pe32_entry.c | grep -v pe32_process.c))
SYSCALL_SRC  = $(sort $(shell find src/syscall -maxdepth 1 -name '*.c' | grep -v dispatcher_generated.c))
HEAP_SRC     = $(sort $(shell find src/heap    -maxdepth 1 -name '*.c'))
CRT_SRC      = $(sort $(shell find src/crt     -maxdepth 1 -name '*.c'))

ROOT_OBJS    = $(patsubst src/%.c,$(BUILDDIR)/%.o,$(ROOT_SRC))
STUBS_OBJS   = $(patsubst src/msvcrt/%.c,$(BUILDDIR)/%.o,$(STUBS_SRC))
LOADER_OBJS  = $(patsubst src/loader/%.c,$(BUILDDIR)/%.o,$(LOADER_SRC))
SYSCALL_OBJS = $(patsubst src/syscall/%.c,$(BUILDDIR)/%.o,$(SYSCALL_SRC))
HEAP_OBJS    = $(patsubst src/heap/%.c,$(BUILDDIR)/%.o,$(HEAP_SRC))
CRT_OBJS     = $(patsubst src/crt/%.c,$(BUILDDIR)/%.o,$(CRT_SRC))

OBJS = $(ROOT_OBJS) $(STUBS_OBJS) $(LOADER_OBJS) $(SYSCALL_OBJS) $(HEAP_OBJS) $(CRT_OBJS) $(BUILDDIR)/run_guest.o $(BUILDDIR)/dispatcher_entry_asm.o $(BUILDDIR)/clone64.o

# ── Source Groups: PE32 Child ───────────────────────────────────
# 32-bit stubs: handler_Nt* providers + kernel32 module loading + handle_manager.
# Exclude crt_*.c (64-bit CRT emulation, not needed in standalone 32-bit child),
# but re-include the CRT infra needed by crt_mingw.c for the 32-bit CRT module path.
MY_WINE32_STUBS_SRC = $(filter-out src/msvcrt/crt_%.c, \
	$(sort $(shell find src/msvcrt -maxdepth 1 -name '*.c'))) \
	src/msvcrt/crt_32_stub.c \
	src/msvcrt/crt_globals.c \
	src/msvcrt/crt_offset_discovery.c \
	src/msvcrt/crt_refptrs.c

# 32-bit heap: use mmap-based allocator instead of musl (musl atomics are x86_64-only).
MY_WINE32_HEAP_SRC = src/heap/wine_heap.c src/heap/pe32_mmap_heap_backend.c

# 32-bit CRT module sources (use glibc; all CRT calls happen before FS switch).
MY_WINE32_CRT_OBJS = $(patsubst src/crt/%.c,$(BUILDDIR32)/%.o,$(CRT_SRC))

# Flatten paths: src/msvcrt/foo.c -> build32/foo.o, src/heap/foo.c -> build32/foo.o.
MY_WINE32_STUBS_OBJS = $(patsubst src/msvcrt/%.c,$(BUILDDIR32)/%.o,$(MY_WINE32_STUBS_SRC))
MY_WINE32_HEAP_OBJS  = $(patsubst src/heap/%.c,$(BUILDDIR32)/%.o,$(MY_WINE32_HEAP_SRC))

MY_WINE32_OBJS = \
	$(BUILDDIR32)/pe32_entry.o \
	$(BUILDDIR32)/pe32_process.o \
	$(BUILDDIR32)/pe32_run_guest.o \
	$(BUILDDIR32)/crash_handlers.o \
	$(BUILDDIR32)/teb_peb.o \
	$(BUILDDIR32)/image_mapper.o \
	$(BUILDDIR32)/module_list.o \
	$(BUILDDIR32)/peb_ldr.o \
	$(BUILDDIR32)/relocations.o \
	$(BUILDDIR32)/pe_symbols.o \
	$(BUILDDIR32)/thunk_gen.o \
	$(BUILDDIR32)/dispatcher.o \
	$(BUILDDIR32)/dispatcher_entry.o \
	$(BUILDDIR32)/dispatcher_entry_asm.o \
	$(BUILDDIR32)/abi_wrappers.o \
	$(BUILDDIR32)/pe_headers.o \
	$(BUILDDIR32)/pe_imports.o \
	$(BUILDDIR32)/pe_rip_scan.o \
	$(BUILDDIR32)/export_table.o \
	$(BUILDDIR32)/dll_path.o \
	$(BUILDDIR32)/dll_loader.o \
	$(BUILDDIR32)/common.o \
	$(BUILDDIR32)/import_table.o \
	$(BUILDDIR32)/import_init.o \
	$(BUILDDIR32)/ordinal_table.o \
	$(BUILDDIR32)/import_resolve.o \
	$(BUILDDIR32)/mmap2_asm.o \
	$(BUILDDIR32)/clone.o \
	$(MY_WINE32_STUBS_OBJS) \
	$(MY_WINE32_HEAP_OBJS) \
	$(MY_WINE32_CRT_OBJS)

# ── Source Groups: Tests ────────────────────────────────────────
PE_OBJS = $(BUILDDIR)/pe_headers.o $(BUILDDIR)/pe_imports.o \
	$(BUILDDIR)/pe_symbols.o $(BUILDDIR)/pe_rip_scan.o

IMPORT_LOADER_OBJS = $(BUILDDIR)/image_mapper.o $(BUILDDIR)/import_table.o \
	$(BUILDDIR)/import_resolve.o $(BUILDDIR)/import_init.o $(BUILDDIR)/ordinal_table.o \
	$(BUILDDIR)/relocations.o $(BUILDDIR)/export_table.o $(BUILDDIR)/module_list.o \
	$(BUILDDIR)/dll_path.o $(BUILDDIR)/dll_loader.o

# Shared objects used by import-resolution and teb_peb tests.
TEST_IMPORT_OBJS = $(PE_OBJS) $(IMPORT_LOADER_OBJS) $(STUBS_OBJS) $(HEAP_OBJS) \
	$(CRT_OBJS) \
	$(BUILDDIR)/thunk_gen.o $(BUILDDIR)/dispatcher_entry.o $(BUILDDIR)/abi_wrappers.o \
	$(BUILDDIR)/gs_base.o $(BUILDDIR)/common.o $(BUILDDIR)/clone64.o

# Non-crt stubs (syscall dispatch test does not need the CRT stubs).
STUBS_NO_CRT_OBJS = $(filter-out $(BUILDDIR)/crt_%.o, $(STUBS_OBJS))

# kernel32_module.c depends on loader functions not available in syscall test,
# so exclude it from the non-CRT stubs used here.
STUBS_SYSCALL_OBJS = $(filter-out $(BUILDDIR)/kernel32_module.o, $(STUBS_NO_CRT_OBJS))
TEST_SYSCALL_OBJS = $(SYSCALL_OBJS) $(STUBS_SYSCALL_OBJS) $(HEAP_OBJS) $(BUILDDIR)/common.o $(BUILDDIR)/clone64.o

TEST_parse_OBJS = $(PE_OBJS) $(BUILDDIR)/debug.o
TEST_import_resolution_OBJS = $(TEST_IMPORT_OBJS) $(BUILDDIR)/module_list.o \
	$(BUILDDIR)/export_table.o $(BUILDDIR)/peb_ldr.o $(BUILDDIR)/debug.o \
	$(BUILDDIR)/test_helpers.o
TEST_teb_peb_OBJS = $(TEST_IMPORT_OBJS) $(BUILDDIR)/teb_peb.o $(BUILDDIR)/peb_ldr.o $(BUILDDIR)/module_list.o $(BUILDDIR)/debug.o
TEST_syscall_dispatch_OBJS = $(TEST_SYSCALL_OBJS) $(BUILDDIR)/debug.o
TEST_relocations_OBJS = $(BUILDDIR)/relocations.o $(BUILDDIR)/debug.o \
	$(BUILDDIR)/pe_headers.o $(BUILDDIR)/image_mapper.o $(BUILDDIR)/common.o
TEST_module_registry_OBJS = $(TEST_IMPORT_OBJS) $(BUILDDIR)/teb_peb.o \
	$(BUILDDIR)/peb_ldr.o $(BUILDDIR)/module_list.o $(BUILDDIR)/debug.o \
	$(BUILDDIR)/test_helpers.o
TEST_export_parsing_OBJS = $(BUILDDIR)/export_table.o $(BUILDDIR)/module_list.o \
	$(BUILDDIR)/debug.o $(PE_OBJS) $(BUILDDIR)/common.o
TEST_pe32_OBJS = $(PE_OBJS) $(BUILDDIR)/relocations.o $(BUILDDIR)/debug.o

# ── Search Paths And Per-target Flags ───────────────────────────
vpath %.c src src/msvcrt src/loader src/syscall src/heap src/crt tests
vpath %.S src src/syscall

# Pattern rules use $(CFLAGS) by default. Override for files needing
# $(SPECIAL_CFLAGS) (entry points, loader core, stubs, syscall infra).
SPECIAL_OBJS = main.o common.o entry.o teb_peb.o guest_setup.o crash_handlers.o gs_base.o \
	thunk_gen.o dispatcher.o dispatcher_entry_asm.o clone64.o abi_wrappers.o import_resolve.o image_mapper.o dll_path.o dll_loader.o crt.o crt_mingw.o crt_watcom.o
$(foreach obj,$(SPECIAL_OBJS),$(eval CFLAGS_$(obj) = $(SPECIAL_CFLAGS)))
$(foreach obj,$(notdir $(STUBS_OBJS)),$(eval CFLAGS_$(obj) = $(SPECIAL_CFLAGS)))
$(foreach obj,$(notdir $(SYSCALL_OBJS)),$(eval CFLAGS_$(obj) = $(SPECIAL_CFLAGS)))
$(foreach obj,$(notdir $(HEAP_OBJS)),$(eval CFLAGS_$(obj) = $(SPECIAL_CFLAGS)))

# pe32plus_musl_malloc_backend needs extra include paths for stubs and musl source.
CFLAGS_pe32plus_musl_malloc_backend.o = $(SPECIAL_CFLAGS) -Isrc/heap/musl_stubs -Isrc/heap/musl_src

# ── Default Target ──────────────────────────────────────────────
all: my_wine my_wine64 my_wine32 samples $(BUILDDIR)/test_parse $(BUILDDIR)/test_import_resolution \
	$(BUILDDIR)/test_teb_peb $(BUILDDIR)/test_syscall_dispatch \
	$(BUILDDIR)/test_relocations $(BUILDDIR)/test_module_registry \
	$(BUILDDIR)/test_export_parsing $(BUILDDIR)/test_pe32 \
	$(BUILDDIR)/test_syscall_safe_utils

# ── Generated Files ─────────────────────────────────────────────
# Dispatcher switch bodies from include/nt_syscalls.def.
$(GENERATED_REQUIRED): include/nt_syscalls.def scripts/gen_dispatcher.py
	@python3 scripts/gen_dispatcher.py --generate

$(BUILDDIR)/dispatcher.o: $(GENERATED_REQUIRED)
$(BUILDDIR32)/dispatcher.o: $(GENERATED_REQUIRED)

gen: gen-dispatcher

check-generated:
	@python3 scripts/gen_dispatcher.py --check

gen-dispatcher: $(GENERATED_REQUIRED)
	@echo "Generated dispatcher switch bodies."

# ── Directories And Pattern Rules ───────────────────────────────
$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(BUILDDIR32):
	@mkdir -p $(BUILDDIR32)

# C sources: look up CFLAGS_<basename>.o; fall back to CFLAGS.
$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	@echo "  CC $<"
	@$(CC) $(if $(CFLAGS_$(notdir $@)),$(CFLAGS_$(notdir $@)),$(CFLAGS)) -c $< -o $@

# Assembly sources: look up CFLAGS_<basename>.o; fall back to CFLAGS.
$(BUILDDIR)/%.o: %.S | $(BUILDDIR)
	@echo "  AS $<"
	@$(CC) $(if $(CFLAGS_$(notdir $@)),$(CFLAGS_$(notdir $@)),$(CFLAGS)) -c $< -o $@

# 32-bit C sources compile with -m32 into build32/.
$(BUILDDIR32)/%.o: %.c | $(BUILDDIR32)
	@echo "  CC32 $<"
	@$(MY_WINE32_CC) $(MY_WINE32_CFLAGS) -c $< -o $@

$(BUILDDIR32)/pe32_run_guest.o: src/loader/pe32_run_guest.S | $(BUILDDIR32)
	@echo "  AS32 $<"
	@$(MY_WINE32_CC) $(MY_WINE32_CFLAGS) -c $< -o $@

$(BUILDDIR32)/dispatcher_entry_asm.o: src/syscall/dispatcher_entry_asm.S | $(BUILDDIR32)
	@echo "  AS32 $<"
	@$(MY_WINE32_CC) $(MY_WINE32_CFLAGS) -c $< -o $@

$(BUILDDIR32)/mmap2_asm.o: src/syscall/mmap2_asm.S | $(BUILDDIR32)
	@echo "  AS32 $<"
	@$(MY_WINE32_CC) $(MY_WINE32_CFLAGS) -c $< -o $@

$(BUILDDIR32)/clone.o: src/syscall/clone.S | $(BUILDDIR32)
	@echo "  AS32 $<"
	@$(MY_WINE32_CC) $(MY_WINE32_CFLAGS) -c $< -o $@

# ── Wrapper Binary ──────────────────────────────────────────────
# my_wine detects PE format and dispatches to my_wine64 (PE32+) or my_wine32 (PE32).
my_wine: $(BUILDDIR)/wrapper_main.o
	@echo "==== Link my_wine ===="
	@$(CC) $(CFLAGS) -o my_wine $(BUILDDIR)/wrapper_main.o $(LDFLAGS)

# ── PE32+ Runtime Binary ────────────────────────────────────────
# my_wine64 loads PE32+ images directly.
my_wine64: $(OBJS)
	@echo "==== Link my_wine64 ===="
	@$(CC) $(CFLAGS) -o my_wine64 $(OBJS) $(LDFLAGS)

# ── PE32 Runtime Binary ─────────────────────────────────────────
# my_wine32 uses pe32_entry.c as main() entry point.
my_wine32: $(MY_WINE32_OBJS)
	@echo "==== Link my_wine32 ===="
	@$(MY_WINE32_CC) -no-pie -o my_wine32 $(MY_WINE32_OBJS) -lpthread

# ── Test Targets ────────────────────────────────────────────────
# Test binaries (native ELF) plus the hello_world sample .exe they exercise.
SHELL.EXE = samples/hello_world/hello_world.exe

$(SHELL.EXE):
	@bash scripts/samples.sh build hello_world

tests: my_wine64 $(SHELL.EXE) $(BUILDDIR)/test_parse $(BUILDDIR)/test_import_resolution \
		$(BUILDDIR)/test_teb_peb $(BUILDDIR)/test_syscall_dispatch \
		$(BUILDDIR)/test_relocations $(BUILDDIR)/test_module_registry \
		$(BUILDDIR)/test_export_parsing $(BUILDDIR)/test_pe32 \
		$(BUILDDIR)/test_syscall_safe_utils

run-tests: tests
	@echo "==== Running tests ===="
	@bash scripts/run_tests.sh $(TEST)

debug-tests: tests
	@echo "==== Running tests with debug ===="
	@bash scripts/run_tests.sh --debug

define TEST_RULE
$(BUILDDIR)/test_$(1): tests/test_$(1).c $(2)
	@echo "  LD $$@"
	@$(CC) $(CFLAGS) -I include -o $$@ $$^ $(LDFLAGS)
endef

$(eval $(call TEST_RULE,parse,$(TEST_parse_OBJS)))
$(eval $(call TEST_RULE,import_resolution,$(TEST_import_resolution_OBJS)))
$(eval $(call TEST_RULE,teb_peb,$(TEST_teb_peb_OBJS)))
$(eval $(call TEST_RULE,syscall_dispatch,$(TEST_syscall_dispatch_OBJS)))
$(eval $(call TEST_RULE,relocations,$(TEST_relocations_OBJS)))
$(eval $(call TEST_RULE,module_registry,$(TEST_module_registry_OBJS)))
$(eval $(call TEST_RULE,export_parsing,$(TEST_export_parsing_OBJS)))
$(eval $(call TEST_RULE,pe32,$(TEST_pe32_OBJS)))
$(eval $(call TEST_RULE,syscall_safe_utils,))

# ── Samples ─────────────────────────────────────────────────────
# Cross-compile samples to PE .exe via Docker (mingw-w64).
# See: scripts/samples.sh
#
#   make samples                build all samples
#   make samples SAMPLE=foo     build one sample
#   make run-samples SAMPLE=foo build + run under ./my_wine
SAMPLE ?=

samples:
	@bash scripts/samples.sh build $(SAMPLE)

run-samples: all
	@bash scripts/samples.sh run $(SAMPLE)

debug-samples: all
	@echo "==== Running samples with debug ===="
	@bash scripts/samples.sh run --debug

build-docker-image:
	@echo "Building my_wine-samples Docker image..."
	@DOCKER_BUILDKIT=0 docker build -t my_wine-samples . || { echo "FAIL: Docker build failed"; exit 1; }
	@echo "OK  my_wine-samples image ready"

# ── Clean And Rebuild Helpers ───────────────────────────────────
clean:
	@echo "  CLEAN build artifacts"
	rm -rf $(BUILDDIR) $(BUILDDIR32)
	rm -f $(GENERATED_REQUIRED)

fclean: clean
	@echo "  FCLEAN all end targets"
	rm -f my_wine my_wine64 my_wine32
	find samples/ -name '*.exe' -delete 2>/dev/null || true
	find samples/ -name '*.dll' -delete 2>/dev/null || true
	rm -rf samples/unpacked/
	find tests/ -maxdepth 1 -type f -executable ! -name '*.c' -delete 2>/dev/null || true

re: fclean
	@$(MAKE) all

# ── Auto-generated Header Dependencies ──────────────────────────
-include $(wildcard $(OBJS:.o=.d))

.PHONY: all clean fclean re tests run-tests debug-tests samples run-samples debug-samples build-docker-image gen gen-dispatcher check-generated $(BUILDDIR) $(BUILDDIR32)
