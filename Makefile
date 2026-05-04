# ── Toolchain ───────────────────────────────────────────────────
CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -g -I. -MMD -MP
LDFLAGS  = -lrt -lpthread -lseccomp

# Special flags for entry points, loader core, stubs, syscall infra
SPECIAL_CFLAGS = $(CFLAGS) -mno-red-zone -fno-stack-protector -fno-exceptions

# ── Build ───────────────────────────────────────────────────────
BUILDDIR = build

# Auto-discover .c per source group; objects flatten into build/
ROOT_SRC     = $(sort $(shell find src/   -maxdepth 1 -name '*.c'))
STUBS_SRC    = $(sort $(shell find src/stubs   -maxdepth 1 -name '*.c'))
LOADER_SRC   = $(sort $(shell find src/loader  -maxdepth 1 -name '*.c'))
SYSCALL_SRC  = $(sort $(shell find src/syscall -maxdepth 1 -name '*.c'))

ROOT_OBJS    = $(patsubst src/%.c,$(BUILDDIR)/%.o,$(ROOT_SRC))
STUBS_OBJS   = $(patsubst src/stubs/%.c,$(BUILDDIR)/%.o,$(STUBS_SRC))
LOADER_OBJS  = $(patsubst src/loader/%.c,$(BUILDDIR)/%.o,$(LOADER_SRC))
SYSCALL_OBJS = $(patsubst src/syscall/%.c,$(BUILDDIR)/%.o,$(SYSCALL_SRC))

OBJS = $(ROOT_OBJS) $(STUBS_OBJS) $(LOADER_OBJS) $(SYSCALL_OBJS) $(BUILDDIR)/run_guest.o

# ── Named object groups for test targets ────────────────────────
PE_OBJS = $(BUILDDIR)/pe_headers.o $(BUILDDIR)/pe_imports.o \
	$(BUILDDIR)/pe_symbols.o $(BUILDDIR)/pe_rip_scan.o

IMPORT_LOADER_OBJS = $(BUILDDIR)/image_mapper.o $(BUILDDIR)/import_table.o \
	$(BUILDDIR)/import_resolve.o $(BUILDDIR)/import_init.o

# Shared objects used by import-resolution and teb_peb tests
TEST_IMPORT_OBJS = $(PE_OBJS) $(IMPORT_LOADER_OBJS) $(STUBS_OBJS) \
	$(BUILDDIR)/thunk_gen.o $(BUILDDIR)/signal_handler.o \
	$(BUILDDIR)/gs_base.o $(BUILDDIR)/common.o

# Non-crt stubs (syscall dispatch test doesn't need the CRT stubs)
STUBS_NO_CRT_OBJS = $(filter-out $(BUILDDIR)/crt_%.o, $(STUBS_OBJS))

# Shared objects used by syscall dispatch test
TEST_SYSCALL_OBJS = $(SYSCALL_OBJS) $(STUBS_NO_CRT_OBJS) $(BUILDDIR)/common.o

# ── vpath ───────────────────────────────────────────────────────
vpath %.c src src/stubs src/loader src/syscall
vpath %.S src

# ── Per-target CFLAGS overrides ─────────────────────────────────
# Pattern rule uses $(CFLAGS) as default. Override for files needing
# $(SPECIAL_CFLAGS) (entry points, loader core, stubs, syscall infra).

# Root src/*.c
CFLAGS_main.o = $(SPECIAL_CFLAGS)
CFLAGS_common.o = $(SPECIAL_CFLAGS)

# Loader src/loader/*.c
CFLAGS_entry.o = $(SPECIAL_CFLAGS)
CFLAGS_teb_peb.o = $(SPECIAL_CFLAGS)
CFLAGS_child_setup.o = $(SPECIAL_CFLAGS)
CFLAGS_crash_handlers.o = $(SPECIAL_CFLAGS)
CFLAGS_gs_base.o = $(SPECIAL_CFLAGS)

# Syscall src/syscall/*.c
CFLAGS_signal_handler.o = $(SPECIAL_CFLAGS)
CFLAGS_thunk_gen.o = $(SPECIAL_CFLAGS)
CFLAGS_dispatcher.o = $(SPECIAL_CFLAGS)

# Stubs (auto-generated from discovered STUBS_OBJS)
$(foreach obj,$(notdir $(STUBS_OBJS)),$(eval CFLAGS_$(obj) = $(SPECIAL_CFLAGS)))

# ── Targets ─────────────────────────────────────────────────────

all: my_wine

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

# Assembly sources: always plain CFLAGS
$(BUILDDIR)/%.o: %.S | $(BUILDDIR)
	@echo "  AS $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# ── Test targets ────────────────────────────────────────────────
# Test binaries (native ELF) + the hello_world sample .exe they exercise.

SHELL.EXE = samples/hello_world/hello_world.exe

$(SHELL.EXE):
	@bash samples/samples.sh build hello_world

# test depends on my_wine, the test binaries, AND hello_world.exe
test: all $(SHELL.EXE) $(BUILDDIR)/test_parse $(BUILDDIR)/test_import_resolution \
		$(BUILDDIR)/test_teb_peb $(BUILDDIR)/test_syscall_dispatch
	@echo "=== Running test_parse ==="
	@if [ -f $(SHELL.EXE) ]; then \
		timeout 5 ./$(BUILDDIR)/test_parse $(SHELL.EXE); \
	else \
		echo "No hello_world.exe found — running error/negative tests only"; \
		timeout 5 ./$(BUILDDIR)/test_parse; \
	fi
	@echo "=== Running test_import_resolution (t7.3) ==="
	timeout 5 ./$(BUILDDIR)/test_import_resolution
	@echo "=== Running test_teb_peb (t7.4) ==="
	timeout 120 ./$(BUILDDIR)/test_teb_peb
	@echo "=== Running test_syscall_dispatch (t7.5) ==="
	timeout 5 ./$(BUILDDIR)/test_syscall_dispatch
	@echo "=== Tests completed ==="

# Each test: prerequisite .c + named object groups; $^ expands to all prereqs
$(BUILDDIR)/test_parse: tests/test_parse.c $(PE_OBJS)
	@echo "  LD $@"
	@$(CC) $(CFLAGS) -I include -o $@ $^

$(BUILDDIR)/test_import_resolution: tests/test_import_resolution.c $(TEST_IMPORT_OBJS)
	@echo "  LD $@"
	@$(CC) $(CFLAGS) -I include -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/test_teb_peb: tests/test_teb_peb.c $(TEST_IMPORT_OBJS) $(BUILDDIR)/teb_peb.o
	@echo "  LD $@"
	@$(CC) $(CFLAGS) -I include -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/test_syscall_dispatch: tests/test_syscall_dispatch.c $(TEST_SYSCALL_OBJS)
	@echo "  LD $@"
	@$(CC) $(CFLAGS) -I include -o $@ $^ $(LDFLAGS)

# ── Auto-generated header dependencies ──────────────────────────
-include $(wildcard $(OBJS:.o=.d))

# ── Samples ──────────────────────────────────────────────────────
# Cross-compile samples to PE .exe via Docker (mingw-w64)
# See: samples/samples.sh
#
#   make samples              build all samples
#   make samples NAME=foo     build one sample
#   make run-sample NAME=foo  build + run under ./my_wine

SAMPLE ?=

samples:
	@bash samples/samples.sh build $(SAMPLE)

run-sample:
	@bash samples/samples.sh run $(SAMPLE)

clean:
	rm -rf $(BUILDDIR) my_wine *.o
	find samples/ -name '*.exe' -delete 2>/dev/null || true

.PHONY: all clean test samples run-sample $(BUILDDIR)
