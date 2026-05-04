# ── Toolchain ───────────────────────────────────────────────────
CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -g -I.
LDFLAGS  = -lrt -lpthread -lseccomp
STUB_CFLAGS = $(CFLAGS) -mno-red-zone -fno-stack-protector -fno-exceptions

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

# vpath: let make find %.c inside subdirectories
vpath %.c src src/stubs src/loader src/syscall
vpath %.S src

# ── Targets ─────────────────────────────────────────────────────

all: my_wine

my_wine: $(OBJS)
	@echo "==== Link my_wine ===="
	@$(CC) $(CFLAGS) -o my_wine $(OBJS) $(LDFLAGS)

# ── Directory creation ──────────────────────────────────────────
$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

# ── Pattern rule ────────────────────────────────────────────────
# Primary rule: handles src/stubs/*.c (STUB_CFLAGS = CFLAGS + -mno-red-zone)
# vpath finds the source in src/stubs
$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	@echo "  CC $<"
	@$(CC) $(STUB_CFLAGS) -c $< -o $@

# ── Explicit overrides (explicit > pattern) ─────────────────────
# These files need flags that differ from the primary pattern rule.
# Must be explicit rules (not pattern) to override the pattern above.

# src/main.c → CFLAGS + -mno-red-zone (entry point, not a stub)
$(BUILDDIR)/main.o: src/main.c | $(BUILDDIR)
	@echo "  CC $<"
	@$(CC) $(CFLAGS) -mno-red-zone -fno-stack-protector -fno-exceptions -c $< -o $@

# PE parser split files → CFLAGS only (no -mno-red-zone)
$(BUILDDIR)/pe_headers.o: src/pe_headers.c | $(BUILDDIR)
	@echo "  CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@
$(BUILDDIR)/pe_imports.o: src/pe_imports.c | $(BUILDDIR)
	@echo "  CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@
$(BUILDDIR)/pe_symbols.o: src/pe_symbols.c | $(BUILDDIR)
	@echo "  CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@
$(BUILDDIR)/pe_rip_scan.o: src/pe_rip_scan.c | $(BUILDDIR)
	@echo "  CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# src/run_guest.S → assembly
$(BUILDDIR)/run_guest.o: src/run_guest.S | $(BUILDDIR)
	@echo "  AS $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# src/syscall/signal_handler.c → CFLAGS + -mno-red-zone
$(BUILDDIR)/signal_handler.o: src/syscall/signal_handler.c | $(BUILDDIR)
	@echo "  CC $<"
	@$(CC) $(CFLAGS) -mno-red-zone -fno-stack-protector -fno-exceptions -c $< -o $@

# src/loader/image_mapper.c → CFLAGS only (no -mno-red-zone)
$(BUILDDIR)/image_mapper.o: src/loader/image_mapper.c | $(BUILDDIR)
	@echo "  CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# src/loader/import_table.c → CFLAGS only (no -mno-red-zone)
$(BUILDDIR)/import_table.o: src/loader/import_table.c | $(BUILDDIR)
	@echo "  CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@
# src/loader/import_resolve.c → CFLAGS only (no -mno-red-zone)
$(BUILDDIR)/import_resolve.o: src/loader/import_resolve.c | $(BUILDDIR)
	@echo "  CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@
# src/loader/import_init.c → CFLAGS only (no -mno-red-zone)
$(BUILDDIR)/import_init.o: src/loader/import_init.c | $(BUILDDIR)
	@echo "  CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# src/loader/teb_peb.c → CFLAGS + -mno-red-zone
$(BUILDDIR)/teb_peb.o: src/loader/teb_peb.c | $(BUILDDIR)
	@echo "  CC $<"
	@$(CC) $(CFLAGS) -mno-red-zone -fno-stack-protector -fno-exceptions -c $< -o $@

# src/loader/entry.c → CFLAGS + -mno-red-zone
$(BUILDDIR)/entry.o: src/loader/entry.c | $(BUILDDIR)
	@echo "  CC $<"
	@$(CC) $(CFLAGS) -mno-red-zone -fno-stack-protector -fno-exceptions -c $< -o $@

# src/loader/child_setup.c → CFLAGS + -mno-red-zone
$(BUILDDIR)/child_setup.o: src/loader/child_setup.c | $(BUILDDIR)
	@echo "  CC $<"
	@$(CC) $(CFLAGS) -mno-red-zone -fno-stack-protector -fno-exceptions -c $< -o $@

# src/loader/crash_handlers.c → CFLAGS + -mno-red-zone
$(BUILDDIR)/crash_handlers.o: src/loader/crash_handlers.c | $(BUILDDIR)
	@echo "  CC $<"
	@$(CC) $(CFLAGS) -mno-red-zone -fno-stack-protector -fno-exceptions -c $< -o $@

# src/loader/gs_base.c → CFLAGS + -mno-red-zone
$(BUILDDIR)/gs_base.o: src/loader/gs_base.c | $(BUILDDIR)
	@echo "  CC $<"
	@$(CC) $(CFLAGS) -mno-red-zone -fno-stack-protector -fno-exceptions -c $< -o $@

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

$(BUILDDIR)/test_parse: tests/test_parse.c $(BUILDDIR)/pe_headers.o $(BUILDDIR)/pe_imports.o $(BUILDDIR)/pe_symbols.o $(BUILDDIR)/pe_rip_scan.o
	@echo "  LD $@"
	@$(CC) $(CFLAGS) -I include -o $@ $< $(BUILDDIR)/pe_headers.o $(BUILDDIR)/pe_imports.o $(BUILDDIR)/pe_symbols.o $(BUILDDIR)/pe_rip_scan.o

$(BUILDDIR)/test_import_resolution: tests/test_import_resolution.c \
	$(BUILDDIR)/pe_headers.o $(BUILDDIR)/pe_imports.o $(BUILDDIR)/pe_symbols.o $(BUILDDIR)/pe_rip_scan.o $(BUILDDIR)/image_mapper.o \
	$(BUILDDIR)/import_table.o $(BUILDDIR)/import_resolve.o $(BUILDDIR)/import_init.o \
	$(BUILDDIR)/crt_globals.o $(BUILDDIR)/crt_file.o \
	$(BUILDDIR)/crt_startup.o $(BUILDDIR)/crt_stdio.o \
	$(BUILDDIR)/crt_stdlib.o $(BUILDDIR)/crt_refptrs.o $(BUILDDIR)/crt_offset_discovery.o \
	$(BUILDDIR)/ntdll_handle.o $(BUILDDIR)/ntdll_io.o \
	$(BUILDDIR)/ntdll_memory.o $(BUILDDIR)/ntdll_process.o \
	$(BUILDDIR)/ntdll_objects.o $(BUILDDIR)/kernel32.o \
	$(BUILDDIR)/thunk_gen.o $(BUILDDIR)/signal_handler.o \
	$(BUILDDIR)/gs_base.o $(BUILDDIR)/abi_wrappers.o \
	$(BUILDDIR)/common.o
	@echo "  LD $@"
	@$(CC) $(CFLAGS) -I include -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/test_teb_peb: tests/test_teb_peb.c \
	$(BUILDDIR)/pe_headers.o $(BUILDDIR)/pe_imports.o $(BUILDDIR)/pe_symbols.o $(BUILDDIR)/pe_rip_scan.o $(BUILDDIR)/image_mapper.o \
	$(BUILDDIR)/import_table.o $(BUILDDIR)/import_resolve.o $(BUILDDIR)/import_init.o $(BUILDDIR)/teb_peb.o \
	$(BUILDDIR)/crt_globals.o $(BUILDDIR)/crt_file.o \
	$(BUILDDIR)/crt_startup.o $(BUILDDIR)/crt_stdio.o \
	$(BUILDDIR)/crt_stdlib.o $(BUILDDIR)/crt_refptrs.o $(BUILDDIR)/crt_offset_discovery.o \
	$(BUILDDIR)/ntdll_handle.o $(BUILDDIR)/ntdll_io.o \
	$(BUILDDIR)/ntdll_memory.o $(BUILDDIR)/ntdll_process.o \
	$(BUILDDIR)/ntdll_objects.o $(BUILDDIR)/kernel32.o \
	$(BUILDDIR)/thunk_gen.o $(BUILDDIR)/signal_handler.o \
	$(BUILDDIR)/gs_base.o $(BUILDDIR)/abi_wrappers.o \
	$(BUILDDIR)/common.o
	@echo "  LD $@"
	@$(CC) $(CFLAGS) -I include -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/test_syscall_dispatch: tests/test_syscall_dispatch.c \
	$(BUILDDIR)/dispatcher.o $(BUILDDIR)/signal_handler.o \
	$(BUILDDIR)/ntdll_handle.o $(BUILDDIR)/ntdll_io.o \
	$(BUILDDIR)/ntdll_memory.o $(BUILDDIR)/ntdll_process.o \
	$(BUILDDIR)/ntdll_objects.o $(BUILDDIR)/kernel32.o \
	$(BUILDDIR)/thunk_gen.o $(BUILDDIR)/abi_wrappers.o \
	$(BUILDDIR)/common.o
	@echo "  LD $@"
	@$(CC) $(CFLAGS) -I include -o $@ $^ $(LDFLAGS)

# ── Header dependencies ─────────────────────────────────────────

# Root
$(BUILDDIR)/my_wine.o: include/pe.h include/ntdll.h include/kernel32.h include/msvcrt.h
$(BUILDDIR)/pe_headers.o: include/pe.h include/pe_parser.h include/common.h src/pe_priv.h
$(BUILDDIR)/pe_imports.o: include/pe.h include/pe_parser.h src/pe_priv.h
$(BUILDDIR)/pe_symbols.o: include/pe.h include/pe_parser.h
$(BUILDDIR)/pe_rip_scan.o: include/pe.h include/pe_parser.h include/common.h src/pe_priv.h

# Stubs
$(BUILDDIR)/ntdll_handle.o: include/ntdll.h src/stubs/ntdll_priv.h src/stubs/handler_abi.h
$(BUILDDIR)/ntdll_io.o: include/ntdll.h src/stubs/ntdll_priv.h src/stubs/handler_abi.h
$(BUILDDIR)/ntdll_memory.o: include/ntdll.h include/pe.h src/stubs/ntdll_priv.h src/stubs/handler_abi.h
$(BUILDDIR)/ntdll_process.o: include/ntdll.h src/stubs/ntdll_priv.h src/stubs/handler_abi.h
$(BUILDDIR)/ntdll_objects.o: include/ntdll.h src/stubs/ntdll_priv.h src/stubs/handler_abi.h
$(BUILDDIR)/kernel32.o: include/kernel32.h include/ntdll.h include/syscall/thunk_gen.h src/stubs/ntdll_priv.h
$(BUILDDIR)/crt_globals.o: src/stubs/msvcrt_priv.h
$(BUILDDIR)/crt_file.o: src/stubs/msvcrt_priv.h
$(BUILDDIR)/crt_startup.o: src/stubs/msvcrt_priv.h
$(BUILDDIR)/crt_stdio.o: src/stubs/msvcrt_priv.h
$(BUILDDIR)/crt_stdlib.o: src/stubs/msvcrt_priv.h
$(BUILDDIR)/crt_refptrs.o: include/pe_parser.h src/stubs/msvcrt_priv.h
$(BUILDDIR)/crt_offset_discovery.o: include/pe_parser.h src/stubs/msvcrt_priv.h

# Loader
$(BUILDDIR)/image_mapper.o: include/pe.h include/pe_parser.h src/loader/loader_priv.h
$(BUILDDIR)/import_table.o: src/loader/loader_priv.h include/ntdll.h include/kernel32.h include/msvcrt.h
$(BUILDDIR)/import_resolve.o: include/pe.h include/pe_parser.h src/loader/loader_priv.h
$(BUILDDIR)/import_init.o: src/loader/loader_priv.h src/stubs/msvcrt_priv.h
$(BUILDDIR)/teb_peb.o: include/pe.h src/loader/loader_priv.h
$(BUILDDIR)/entry.o: src/loader/loader_priv.h
$(BUILDDIR)/child_setup.o: include/pe.h include/msvcrt.h include/nt_constants.h include/syscall/thunk_gen.h include/syscall/signal_handler.h include/syscall/dispatcher.h include/common.h src/loader/loader_priv.h
$(BUILDDIR)/crash_handlers.o: include/common.h include/syscall/thunk_gen.h include/syscall/signal_handler.h include/syscall/dispatcher.h src/loader/loader_priv.h
# gs_base.c uses only sys/syscall.h, asm/prctl.h, stdio.h, errno.h, string.h, unistd.h
# No header dependency needed (all system headers)
# $(BUILDDIR)/gs_base.o: 

# Syscall
$(BUILDDIR)/thunk_gen.o: include/syscall/thunk_gen.h include/syscall/signal_handler.h
$(BUILDDIR)/signal_handler.o: include/syscall/signal_handler.h
$(BUILDDIR)/dispatcher.o: include/ntdll.h include/syscall/dispatcher.h

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
