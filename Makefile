# ── Toolchain ───────────────────────────────────────────────────
CC       = gcc
CFLAGS   = -Wall -Wextra -Werror -O2 -g -I. -Iinclude -MMD -MP -mno-sse
LDFLAGS  = -lrt -lpthread -ldl

# Special flags for entry points, loader core, stubs, syscall infra
SPECIAL_CFLAGS = $(CFLAGS) -mno-red-zone -fno-stack-protector -fno-exceptions

# Add generated CRT offsets if the header exists
ifneq ($(wildcard include/crt_offsets_generated.h),)
CFLAGS += -DHAVE_GENERATED_CRT_OFFSETS
endif

# ── Build ───────────────────────────────────────────────────────
BUILDDIR = build

# Auto-discover .c per source group; objects flatten into build/
ROOT_SRC     = $(filter-out src/wrapper_main.c, $(sort $(shell find src/   -maxdepth 1 -name '*.c')))
STUBS_SRC    = $(filter-out src/msvcrt/crt_32_stub.c, \
		$(sort $(shell find src/msvcrt   -maxdepth 1 -name '*.c')))
LOADER_SRC   = $(sort $(shell find src/loader  -maxdepth 1 -name '*.c' | grep -v pe32_entry.c))
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

# ── Named object groups for test targets ────────────────────────
PE_OBJS = $(BUILDDIR)/pe_headers.o $(BUILDDIR)/pe_imports.o \
	$(BUILDDIR)/pe_symbols.o $(BUILDDIR)/pe_rip_scan.o

IMPORT_LOADER_OBJS = $(BUILDDIR)/image_mapper.o $(BUILDDIR)/import_table.o \
	$(BUILDDIR)/import_resolve.o $(BUILDDIR)/import_init.o $(BUILDDIR)/ordinal_table.o \
	$(BUILDDIR)/relocations.o $(BUILDDIR)/export_table.o $(BUILDDIR)/module_list.o \
	$(BUILDDIR)/dll_path.o $(BUILDDIR)/dll_loader.o

# Shared objects used by import-resolution and teb_peb tests
TEST_IMPORT_OBJS = $(PE_OBJS) $(IMPORT_LOADER_OBJS) $(STUBS_OBJS) $(HEAP_OBJS) \
	$(CRT_OBJS) \
	$(BUILDDIR)/thunk_gen.o $(BUILDDIR)/dispatcher_entry.o $(BUILDDIR)/abi_wrappers.o \
	$(BUILDDIR)/gs_base.o $(BUILDDIR)/common.o $(BUILDDIR)/clone64.o

# Non-crt stubs (syscall dispatch test doesn't need the CRT stubs)
STUBS_NO_CRT_OBJS = $(filter-out $(BUILDDIR)/crt_%.o, $(STUBS_OBJS))

# Shared objects used by syscall dispatch test
# kernel32_module.c depends on loader functions not available in syscall test,
# so exclude it from the non-CRT stubs used here.
STUBS_SYSCALL_OBJS = $(filter-out $(BUILDDIR)/kernel32_module.o, $(STUBS_NO_CRT_OBJS))
TEST_SYSCALL_OBJS = $(SYSCALL_OBJS) $(STUBS_SYSCALL_OBJS) $(HEAP_OBJS) $(BUILDDIR)/common.o $(BUILDDIR)/clone64.o

# ── vpath ───────────────────────────────────────────────────────
vpath %.c src src/msvcrt src/loader src/syscall src/heap src/crt tests
vpath %.S src src/syscall

# ── Per-target CFLAGS overrides ─────────────────────────────────
# Pattern rule uses $(CFLAGS) as default. Override for files needing
# $(SPECIAL_CFLAGS) (entry points, loader core, stubs, syscall infra).

SPECIAL_OBJS = main.o common.o entry.o teb_peb.o guest_setup.o crash_handlers.o gs_base.o \
	thunk_gen.o dispatcher.o dispatcher_entry_asm.o clone64.o abi_wrappers.o import_resolve.o image_mapper.o dll_path.o dll_loader.o crt.o crt_mingw.o crt_watcom.o
$(foreach obj,$(SPECIAL_OBJS),$(eval CFLAGS_$(obj) = $(SPECIAL_CFLAGS)))
$(foreach obj,$(notdir $(STUBS_OBJS)),$(eval CFLAGS_$(obj) = $(SPECIAL_CFLAGS)))
$(foreach obj,$(notdir $(SYSCALL_OBJS)),$(eval CFLAGS_$(obj) = $(SPECIAL_CFLAGS)))
$(foreach obj,$(notdir $(HEAP_OBJS)),$(eval CFLAGS_$(obj) = $(SPECIAL_CFLAGS)))

# musl_malloc_wrapper needs extra include paths for stubs and musl source
CFLAGS_musl_malloc_wrapper.o = $(SPECIAL_CFLAGS) -Isrc/heap/musl_stubs -Isrc/heap/musl_src

# ── Targets ─────────────────────────────────────────────────────

all: my_wine my_wine64 my_wine32 samples $(BUILDDIR)/test_parse $(BUILDDIR)/test_import_resolution \
	$(BUILDDIR)/test_teb_peb $(BUILDDIR)/test_syscall_dispatch \
	$(BUILDDIR)/test_relocations $(BUILDDIR)/test_module_registry \
	$(BUILDDIR)/test_export_parsing $(BUILDDIR)/test_pe32

# ── Wrapper binary ──────────────────────────────────────────────
# my_wine: standalone wrapper that detects PE format and dispatches
# to my_wine64 (PE32+) or my_wine32 (PE32).

my_wine: $(BUILDDIR)/wrapper_main.o
	@echo "==== Link my_wine ===="
	@$(CC) $(CFLAGS) -o my_wine $(BUILDDIR)/wrapper_main.o $(LDFLAGS)

# ── Main 64-bit binary ──────────────────────────────────────────
# my_wine64: loads PE32+ images directly.

my_wine64: $(OBJS)
	@echo "==== Link my_wine64 ===="
	@$(CC) $(CFLAGS) -o my_wine64 $(OBJS) $(LDFLAGS)

# ── 32-bit child binary ─────────────────────────────────────────
# my_wine32: standalone 32-bit ELF that loads PE32 images.
# Compiled with -m32, dynamically linked with glibc CRT.
# Uses pe32_entry.c as main() entry point.

MY_WINE32_CC = $(CC) -m32
MY_WINE32_CFLAGS = $(CFLAGS) -DMY_WINE32 -mno-red-zone -fno-stack-protector \
	-fno-exceptions -mno-sse -fno-pie -no-pie -Werror
BUILDDIR32 = build32

# 32-bit stubs: handler_Nt* providers + kernel32 module loading + handle_manager
# Exclude crt_*.c (64-bit CRT emulation, not needed in standalone 32-bit child)
MY_WINE32_STUBS_SRC = $(filter-out src/msvcrt/crt_%.c, \
	$(sort $(shell find src/msvcrt -maxdepth 1 -name '*.c'))) src/msvcrt/crt_32_stub.c
# 32-bit heap: use mmap-based allocator instead of musl (musl atomics are x86_64-only)
MY_WINE32_HEAP_SRC = src/heap/wine_heap.c src/heap/musl_malloc_32_compat.c
# Flatten paths: src/msvcrt/foo.c → build32/foo.o, src/heap/foo.c → build32/foo.o
MY_WINE32_STUBS_OBJS = $(patsubst src/msvcrt/%.c,$(BUILDDIR32)/%.o,$(MY_WINE32_STUBS_SRC))
MY_WINE32_HEAP_OBJS  = $(patsubst src/heap/%.c,$(BUILDDIR32)/%.o,$(MY_WINE32_HEAP_SRC))

MY_WINE32_OBJS = \
	$(BUILDDIR32)/pe32_entry.o \
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
	$(MY_WINE32_HEAP_OBJS)

my_wine32: $(MY_WINE32_OBJS)
	@echo "==== Link my_wine32 ===="
	@$(MY_WINE32_CC) -no-pie -o my_wine32 $(MY_WINE32_OBJS) -lpthread

# 32-bit pattern rules — compile with -m32 into build32/
$(BUILDDIR32):
	@mkdir -p $(BUILDDIR32)

$(BUILDDIR32)/%.o: %.c | $(BUILDDIR32)
	@echo "  CC32 $<"
	@$(MY_WINE32_CC) $(MY_WINE32_CFLAGS) -c $< -o $@

# Explicit rule for pe32_run_guest.S
$(BUILDDIR32)/pe32_run_guest.o: src/loader/pe32_run_guest.S | $(BUILDDIR32)
	@echo "  AS32 $<"
	@$(MY_WINE32_CC) $(MY_WINE32_CFLAGS) -c $< -o $@

# Explicit rule for dispatcher_entry_asm.S from src/syscall/
$(BUILDDIR32)/dispatcher_entry_asm.o: src/syscall/dispatcher_entry_asm.S | $(BUILDDIR32)
	@echo "  AS32 $<"
	@$(MY_WINE32_CC) $(MY_WINE32_CFLAGS) -c $< -o $@

# Explicit rule for mmap2_asm.S from src/syscall/
$(BUILDDIR32)/mmap2_asm.o: src/syscall/mmap2_asm.S | $(BUILDDIR32)
	@echo "  AS32 $<"
	@$(MY_WINE32_CC) $(MY_WINE32_CFLAGS) -c $< -o $@

# Explicit rule for clone.S from src/syscall/
$(BUILDDIR32)/clone.o: src/syscall/clone.S | $(BUILDDIR32)
	@echo "  AS32 $<"
	@$(MY_WINE32_CC) $(MY_WINE32_CFLAGS) -c $< -o $@

# 32-bit dispatcher.o needs the generated dispatch switch
$(BUILDDIR32)/dispatcher.o: src/syscall/dispatcher_generated.c

# ── Directory creation ──────────────────────────────────────────
$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

# ── Pattern rules ───────────────────────────────────────────────
# C sources: look up CFLAGS_<basename>.o; fall back to CFLAGS
$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	@echo "  CC $<"
	@$(CC) $(if $(CFLAGS_$(notdir $@)),$(CFLAGS_$(notdir $@)),$(CFLAGS)) -c $< -o $@

# Assembly sources: look up CFLAGS_<basename>.o; fall back to CFLAGS
$(BUILDDIR)/%.o: %.S | $(BUILDDIR)
	@echo "  AS $<"
	@$(CC) $(if $(CFLAGS_$(notdir $@)),$(CFLAGS_$(notdir $@)),$(CFLAGS)) -c $< -o $@

# ── Test targets ────────────────────────────────────────────────
# Test binaries (native ELF) + the hello_world sample .exe they exercise.

SHELL.EXE = samples/hello_world/hello_world.exe

$(SHELL.EXE):
	@bash scripts/samples.sh build hello_world

# test builds the test binaries; run-tests builds + runs them

TEST ?=

tests: my_wine64 $(SHELL.EXE) $(BUILDDIR)/test_parse $(BUILDDIR)/test_import_resolution \
		$(BUILDDIR)/test_teb_peb $(BUILDDIR)/test_syscall_dispatch \
		$(BUILDDIR)/test_relocations $(BUILDDIR)/test_module_registry \
		$(BUILDDIR)/test_export_parsing $(BUILDDIR)/test_pe32

run-tests: tests
	@echo "==== Running tests ===="
	@bash scripts/run_tests.sh $(TEST)

debug-tests: tests
	@echo "==== Running tests with debug ===="
	@bash scripts/run_tests.sh --debug

# Per-test object groups
TEST_parse_OBJS = $(PE_OBJS) $(BUILDDIR)/debug.o
TEST_import_resolution_OBJS = $(TEST_IMPORT_OBJS) $(BUILDDIR)/module_list.o \
	$(BUILDDIR)/export_table.o $(BUILDDIR)/peb_ldr.o $(BUILDDIR)/debug.o \
	$(BUILDDIR)/test_helpers.o
TEST_teb_peb_OBJS = $(TEST_IMPORT_OBJS) $(BUILDDIR)/teb_peb.o $(BUILDDIR)/peb_ldr.o $(BUILDDIR)/module_list.o $(BUILDDIR)/debug.o
TEST_syscall_dispatch_OBJS = $(TEST_SYSCALL_OBJS) $(BUILDDIR)/debug.o
TEST_relocations_OBJS = $(BUILDDIR)/relocations.o $(BUILDDIR)/debug.o \
	$(BUILDDIR)/pe_headers.o $(BUILDDIR)/image_mapper.o $(BUILDDIR)/common.o

# Module registry + PEB LDR test
TEST_module_registry_OBJS = $(TEST_IMPORT_OBJS) $(BUILDDIR)/teb_peb.o \
	$(BUILDDIR)/peb_ldr.o $(BUILDDIR)/module_list.o $(BUILDDIR)/debug.o \
	$(BUILDDIR)/test_helpers.o

# Export parsing test
TEST_export_parsing_OBJS = $(BUILDDIR)/export_table.o $(BUILDDIR)/module_list.o \
	$(BUILDDIR)/debug.o $(PE_OBJS) $(BUILDDIR)/common.o

# PE32 (32-bit) parsing and relocation test
TEST_pe32_OBJS = $(PE_OBJS) $(BUILDDIR)/relocations.o $(BUILDDIR)/debug.o

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

# ── Auto-generated header dependencies ──────────────────────────
-include $(wildcard $(OBJS:.o=.d))

# ── Samples ──────────────────────────────────────────────────────
# Cross-compile samples to PE .exe via Docker (mingw-w64)
# See: scripts/samples.sh
#
#   make samples              build all samples
#   make samples SAMPLE=foo     build one sample
#   make run-samples SAMPLE=foo  build + run under ./my_wine

SAMPLE ?=

samples:
	@bash scripts/samples.sh build $(SAMPLE)

run-samples: all
	@bash scripts/samples.sh run $(SAMPLE)

debug-samples: all
	@echo "==== Running samples with debug ===="
	@bash scripts/samples.sh run --debug

clean:
	@echo "  CLEAN build artifacts"
	rm -rf $(BUILDDIR) $(BUILDDIR32)
	rm -f include/crt_offsets_generated.h
	rm -f src/syscall/dispatcher_generated.c

fclean: clean
	@echo "  FCLEAN all end targets"
	rm -f my_wine my_wine64 my_wine32
	find samples/ -name '*.exe' -delete 2>/dev/null || true
	find samples/ -name '*.dll' -delete 2>/dev/null || true
	rm -rf samples/unpacked/
	find tests/ -maxdepth 1 -type f -executable ! -name '*.c' -delete 2>/dev/null || true

re: fclean
	@$(MAKE) all

# ── Auto-generation ─────────────────────────────────────────────
# Dispatcher switch bodies from include/nt_syscalls.def
src/syscall/dispatcher_generated.c: include/nt_syscalls.def scripts/gen_dispatcher.py
	@python3 scripts/gen_dispatcher.py --generate
$(BUILDDIR)/dispatcher.o: src/syscall/dispatcher_generated.c

build-docker-image:
	@echo "Building my_wine-samples Docker image..."
	@DOCKER_BUILDKIT=0 docker build -t my_wine-samples . || { echo "FAIL: Docker build failed"; exit 1; }
	@echo "OK  my_wine-samples image ready"

gen-crt-offsets:
	@echo "Generating CRT offsets from current mingw-w64 toolchain..."
	@bash scripts/gen_crt_offsets.sh || { echo "WARNING: CRT offset generation failed"; echo "  Try: make build-docker-image"; exit 0; }

gen-dispatcher: src/syscall/dispatcher_generated.c
	@echo "Generated dispatcher switch bodies."

.PHONY: all clean fclean re tests run-tests debug-tests samples run-samples debug-samples build-docker-image gen-crt-offsets gen-dispatcher $(BUILDDIR) $(BUILDDIR32)
