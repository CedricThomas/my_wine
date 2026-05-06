# ── Toolchain ───────────────────────────────────────────────────
CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -g -I. -MMD -MP
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
ROOT_SRC     = $(sort $(shell find src/   -maxdepth 1 -name '*.c'))
STUBS_SRC    = $(sort $(shell find src/stubs   -maxdepth 1 -name '*.c'))
LOADER_SRC   = $(sort $(shell find src/loader  -maxdepth 1 -name '*.c'))
SYSCALL_SRC  = $(sort $(shell find src/syscall -maxdepth 1 -name '*.c' | grep -v dispatcher_generated.c))
HEAP_SRC     = $(sort $(shell find src/heap    -maxdepth 1 -name '*.c'))

ROOT_OBJS    = $(patsubst src/%.c,$(BUILDDIR)/%.o,$(ROOT_SRC))
STUBS_OBJS   = $(patsubst src/stubs/%.c,$(BUILDDIR)/%.o,$(STUBS_SRC))
LOADER_OBJS  = $(patsubst src/loader/%.c,$(BUILDDIR)/%.o,$(LOADER_SRC))
SYSCALL_OBJS = $(patsubst src/syscall/%.c,$(BUILDDIR)/%.o,$(SYSCALL_SRC))
HEAP_OBJS    = $(patsubst src/heap/%.c,$(BUILDDIR)/%.o,$(HEAP_SRC))

OBJS = $(ROOT_OBJS) $(STUBS_OBJS) $(LOADER_OBJS) $(SYSCALL_OBJS) $(HEAP_OBJS) $(BUILDDIR)/run_guest.o $(BUILDDIR)/dispatcher_entry_asm.o

# ── Named object groups for test targets ────────────────────────
PE_OBJS = $(BUILDDIR)/pe_headers.o $(BUILDDIR)/pe_imports.o \
	$(BUILDDIR)/pe_symbols.o $(BUILDDIR)/pe_rip_scan.o

IMPORT_LOADER_OBJS = $(BUILDDIR)/image_mapper.o $(BUILDDIR)/import_table.o \
	$(BUILDDIR)/import_resolve.o $(BUILDDIR)/import_init.o $(BUILDDIR)/ordinal_table.o \
	$(BUILDDIR)/relocations.o

# Shared objects used by import-resolution and teb_peb tests
TEST_IMPORT_OBJS = $(PE_OBJS) $(IMPORT_LOADER_OBJS) $(STUBS_OBJS) $(HEAP_OBJS) \
	$(BUILDDIR)/thunk_gen.o $(BUILDDIR)/dispatcher_entry.o \
	$(BUILDDIR)/gs_base.o $(BUILDDIR)/common.o

# Non-crt stubs (syscall dispatch test doesn't need the CRT stubs)
STUBS_NO_CRT_OBJS = $(filter-out $(BUILDDIR)/crt_%.o, $(STUBS_OBJS))

# Shared objects used by syscall dispatch test
TEST_SYSCALL_OBJS = $(SYSCALL_OBJS) $(STUBS_NO_CRT_OBJS) $(BUILDDIR)/common.o

# ── vpath ───────────────────────────────────────────────────────
vpath %.c src src/stubs src/loader src/syscall src/heap
vpath %.S src src/syscall

# ── Per-target CFLAGS overrides ─────────────────────────────────
# Pattern rule uses $(CFLAGS) as default. Override for files needing
# $(SPECIAL_CFLAGS) (entry points, loader core, stubs, syscall infra).

SPECIAL_OBJS = main.o common.o entry.o teb_peb.o guest_setup.o crash_handlers.o gs_base.o \
	thunk_gen.o dispatcher.o dispatcher_entry_asm.o
$(foreach obj,$(SPECIAL_OBJS),$(eval CFLAGS_$(obj) = $(SPECIAL_CFLAGS)))
$(foreach obj,$(notdir $(STUBS_OBJS)),$(eval CFLAGS_$(obj) = $(SPECIAL_CFLAGS)))
$(foreach obj,$(notdir $(HEAP_OBJS)),$(eval CFLAGS_$(obj) = $(SPECIAL_CFLAGS)))

# ── Targets ─────────────────────────────────────────────────────

all: my_wine samples $(BUILDDIR)/test_parse $(BUILDDIR)/test_import_resolution \
	$(BUILDDIR)/test_teb_peb $(BUILDDIR)/test_syscall_dispatch \
	$(BUILDDIR)/test_relocations

my_wine: $(OBJS)
	@echo "==== Link my_wine ===="
	@$(CC) $(CFLAGS) -o my_wine $(OBJS) $(LDFLAGS)

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
	@bash samples/samples.sh build hello_world

# test builds the test binaries; run-test builds + runs them

TEST ?=

tests: my_wine $(SHELL.EXE) $(BUILDDIR)/test_parse $(BUILDDIR)/test_import_resolution \
		$(BUILDDIR)/test_teb_peb $(BUILDDIR)/test_syscall_dispatch \
		$(BUILDDIR)/test_relocations

run-test: tests
	@echo "==== Running tests ===="
	@bash scripts/run_tests.sh $(TEST)

# Per-test object groups
TEST_parse_OBJS = $(PE_OBJS) $(BUILDDIR)/debug.o
TEST_import_resolution_OBJS = $(TEST_IMPORT_OBJS) $(BUILDDIR)/debug.o
TEST_teb_peb_OBJS = $(TEST_IMPORT_OBJS) $(BUILDDIR)/teb_peb.o $(BUILDDIR)/debug.o
TEST_syscall_dispatch_OBJS = $(TEST_SYSCALL_OBJS) $(BUILDDIR)/debug.o
TEST_relocations_OBJS = $(BUILDDIR)/relocations.o $(BUILDDIR)/debug.o \
	$(BUILDDIR)/pe_headers.o $(BUILDDIR)/image_mapper.o

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

# ── Auto-generated header dependencies ──────────────────────────
-include $(wildcard $(OBJS:.o=.d))

# ── Samples ──────────────────────────────────────────────────────
# Cross-compile samples to PE .exe via Docker (mingw-w64)
# See: samples/samples.sh
#
#   make samples              build all samples
#   make samples SAMPLE=foo     build one sample
#   make run-sample SAMPLE=foo  build + run under ./my_wine

SAMPLE ?=

samples:
	@bash samples/samples.sh build $(SAMPLE)

run-sample: all
	@bash samples/samples.sh run $(SAMPLE)

clean:
	@echo "  CLEAN build artifacts"
	rm -rf $(BUILDDIR)
	rm -f include/crt_offsets_generated.h
	rm -f src/syscall/dispatcher_generated.c

fclean: clean
	@echo "  FCLEAN end targets"
	rm -f my_wine
	find samples/ -name '*.exe' -delete 2>/dev/null || true

re: fclean
	@$(MAKE) all

# ── Auto-generation ─────────────────────────────────────────────
# Dispatcher switch bodies from include/nt_syscalls.def
src/syscall/dispatcher_generated.c: include/nt_syscalls.def scripts/gen_dispatcher.py
	@python3 scripts/gen_dispatcher.py --generate
$(BUILDDIR)/dispatcher.o: src/syscall/dispatcher_generated.c

gen-crt-offsets:
	@echo "Generating CRT offsets from current mingw-w64 toolchain..."
	@bash scripts/gen_crt_offsets.sh || { echo "WARNING: CRT offset generation failed, using hardcoded fallback"; exit 0; }

gen-dispatcher: src/syscall/dispatcher_generated.c
	@echo "Generated dispatcher switch bodies."

.PHONY: all clean fclean re tests run-test samples run-sample gen-crt-offsets gen-dispatcher $(BUILDDIR)
