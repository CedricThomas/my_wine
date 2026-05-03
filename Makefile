CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -I .
LDFLAGS = -lrt -lpthread -lseccomp

BUILDDIR = build
OBJS = $(BUILDDIR)/my_wine.o $(BUILDDIR)/pe_parser.o $(BUILDDIR)/thunk_gen.o \
       $(BUILDDIR)/signal_handler.o $(BUILDDIR)/dispatcher.o \
       $(BUILDDIR)/ntdll_handle.o $(BUILDDIR)/ntdll_io.o \
       $(BUILDDIR)/ntdll_memory.o $(BUILDDIR)/ntdll_process.o \
       $(BUILDDIR)/ntdll_objects.o $(BUILDDIR)/kernel32.o \
       $(BUILDDIR)/crt_globals.o $(BUILDDIR)/crt_file.o $(BUILDDIR)/crt_startup.o \
       $(BUILDDIR)/crt_stdio.o $(BUILDDIR)/crt_stdlib.o $(BUILDDIR)/crt_refptrs.o \
       $(BUILDDIR)/run_guest.o \
       $(BUILDDIR)/loader/image_mapper.o \
       $(BUILDDIR)/loader/import_resolver.o \
       $(BUILDDIR)/loader/teb_peb.o \
       $(BUILDDIR)/loader/entry.o

all: my_wine

test: all $(BUILDDIR)/test_parse $(BUILDDIR)/test_import_resolution \
		$(BUILDDIR)/test_teb_peb $(BUILDDIR)/test_syscall_dispatch
	@echo "=== Compiling tests ==="
	@echo "=== Running test_parse ==="
	@if [ -f hello.exe ]; then \
		./$(BUILDDIR)/test_parse hello.exe; \
	elif [ -f examples/hello.exe ]; then \
		./$(BUILDDIR)/test_parse examples/hello.exe; \
	else \
		echo "No hello.exe found — running error/negative tests only"; \
		./$(BUILDDIR)/test_parse; \
	fi
	@echo "=== Running test_import_resolution (t7.3) ==="
	./$(BUILDDIR)/test_import_resolution
	@echo "=== Running test_teb_peb (t7.4) ==="
	./$(BUILDDIR)/test_teb_peb
	@echo "=== Running test_syscall_dispatch (t7.5) ==="
	./$(BUILDDIR)/test_syscall_dispatch
	@echo "=== Tests completed ==="

# ── Test: test_parse ──────────────────────────────────────────
$(BUILDDIR)/test_parse: tests/test_parse.c $(BUILDDIR)/pe_parser.o
	$(CC) $(CFLAGS) -I include -o $@ $< $(BUILDDIR)/pe_parser.o

# ── t7.3: test_import_resolution ──────────────────────────────
# Links against pe_parser, image_mapper, import_resolver and all
# CRT/ntdll stubs (for __msvcrt_* and handler_* symbols)
$(BUILDDIR)/test_import_resolution: tests/test_import_resolution.c \
	$(BUILDDIR)/pe_parser.o $(BUILDDIR)/loader/image_mapper.o \
	$(BUILDDIR)/loader/import_resolver.o \
	$(BUILDDIR)/crt_globals.o $(BUILDDIR)/crt_file.o \
	$(BUILDDIR)/crt_startup.o $(BUILDDIR)/crt_stdio.o \
	$(BUILDDIR)/crt_stdlib.o $(BUILDDIR)/crt_refptrs.o \
	$(BUILDDIR)/ntdll_handle.o $(BUILDDIR)/ntdll_io.o \
	$(BUILDDIR)/ntdll_memory.o $(BUILDDIR)/ntdll_process.o \
	$(BUILDDIR)/ntdll_objects.o $(BUILDDIR)/kernel32.o \
	$(BUILDDIR)/thunk_gen.o $(BUILDDIR)/signal_handler.o
	$(CC) $(CFLAGS) -I include -o $@ $^ $(LDFLAGS)

# ── t7.4: test_teb_peb ────────────────────────────────────────
$(BUILDDIR)/test_teb_peb: tests/test_teb_peb.c \
	$(BUILDDIR)/pe_parser.o $(BUILDDIR)/loader/image_mapper.o \
	$(BUILDDIR)/loader/import_resolver.o $(BUILDDIR)/loader/teb_peb.o \
	$(BUILDDIR)/crt_globals.o $(BUILDDIR)/crt_file.o \
	$(BUILDDIR)/crt_startup.o $(BUILDDIR)/crt_stdio.o \
	$(BUILDDIR)/crt_stdlib.o $(BUILDDIR)/crt_refptrs.o \
	$(BUILDDIR)/ntdll_handle.o $(BUILDDIR)/ntdll_io.o \
	$(BUILDDIR)/ntdll_memory.o $(BUILDDIR)/ntdll_process.o \
	$(BUILDDIR)/ntdll_objects.o $(BUILDDIR)/kernel32.o \
	$(BUILDDIR)/thunk_gen.o $(BUILDDIR)/signal_handler.o
	$(CC) $(CFLAGS) -I include -o $@ $^ $(LDFLAGS)

# ── t7.5: test_syscall_dispatch ───────────────────────────────
$(BUILDDIR)/test_syscall_dispatch: tests/test_syscall_dispatch.c \
	$(BUILDDIR)/dispatcher.o $(BUILDDIR)/signal_handler.o \
	$(BUILDDIR)/ntdll_handle.o $(BUILDDIR)/ntdll_io.o \
	$(BUILDDIR)/ntdll_memory.o $(BUILDDIR)/ntdll_process.o \
	$(BUILDDIR)/ntdll_objects.o $(BUILDDIR)/kernel32.o \
	$(BUILDDIR)/thunk_gen.o
	$(CC) $(CFLAGS) -I include -o $@ $^ $(LDFLAGS)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)
	mkdir -p $(BUILDDIR)/loader

my_wine: $(OBJS)
	$(CC) $(CFLAGS) -o my_wine $(OBJS) $(LDFLAGS)

# Explicit compile rules mapping subdirectory sources to build/ .o files
$(BUILDDIR)/my_wine.o: src/main.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -mno-red-zone -c $< -o $@

$(BUILDDIR)/pe_parser.o: src/pe_parser.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/thunk_gen.o: src/syscall/thunk_gen.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/signal_handler.o: src/syscall/signal_handler.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/dispatcher.o: src/syscall/dispatcher.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# ── ntdll split files ────────────────────────────────────────────
$(BUILDDIR)/ntdll_handle.o: src/stubs/ntdll_handle.c src/stubs/ntdll_priv.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/ntdll_io.o: src/stubs/ntdll_io.c src/stubs/ntdll_priv.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/ntdll_memory.o: src/stubs/ntdll_memory.c src/stubs/ntdll_priv.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/ntdll_process.o: src/stubs/ntdll_process.c src/stubs/ntdll_priv.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/ntdll_objects.o: src/stubs/ntdll_objects.c src/stubs/ntdll_priv.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

STUB_CFLAGS = $(CFLAGS) -mno-red-zone

$(BUILDDIR)/kernel32.o: src/stubs/kernel32.c | $(BUILDDIR)
	$(CC) $(STUB_CFLAGS) -c $< -o $@

# ── msvcrt split files ─────────────────────────────────────────
$(BUILDDIR)/crt_globals.o: src/stubs/crt_globals.c src/stubs/msvcrt_priv.h | $(BUILDDIR)
	$(CC) $(STUB_CFLAGS) -c $< -o $@

$(BUILDDIR)/crt_file.o: src/stubs/crt_file.c src/stubs/msvcrt_priv.h | $(BUILDDIR)
	$(CC) $(STUB_CFLAGS) -c $< -o $@

$(BUILDDIR)/crt_startup.o: src/stubs/crt_startup.c src/stubs/msvcrt_priv.h | $(BUILDDIR)
	$(CC) $(STUB_CFLAGS) -c $< -o $@

$(BUILDDIR)/crt_stdio.o: src/stubs/crt_stdio.c src/stubs/msvcrt_priv.h | $(BUILDDIR)
	$(CC) $(STUB_CFLAGS) -c $< -o $@

$(BUILDDIR)/crt_stdlib.o: src/stubs/crt_stdlib.c src/stubs/msvcrt_priv.h | $(BUILDDIR)
	$(CC) $(STUB_CFLAGS) -c $< -o $@

$(BUILDDIR)/crt_refptrs.o: src/stubs/crt_refptrs.c src/stubs/msvcrt_priv.h | $(BUILDDIR)
	$(CC) $(STUB_CFLAGS) -c $< -o $@

$(BUILDDIR)/run_guest.o: src/run_guest.S | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# ── loader module compile rules ───────────────────────────────
$(BUILDDIR)/loader/image_mapper.o: src/loader/image_mapper.c src/loader/loader_priv.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/loader/import_resolver.o: src/loader/import_resolver.c src/loader/loader_priv.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/loader/teb_peb.o: src/loader/teb_peb.c src/loader/loader_priv.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -mno-red-zone -c $< -o $@

$(BUILDDIR)/loader/entry.o: src/loader/entry.c src/loader/loader_priv.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -mno-red-zone -c $< -o $@

# Header dependencies (for recompilation when headers change)
$(BUILDDIR)/my_wine.o: include/pe.h include/ntdll.h include/kernel32.h include/msvcrt.h
$(BUILDDIR)/pe_parser.o: include/pe.h include/pe_parser.h
$(BUILDDIR)/thunk_gen.o: include/syscall/thunk_gen.h include/syscall/signal_handler.h
$(BUILDDIR)/signal_handler.o: include/syscall/signal_handler.h
$(BUILDDIR)/dispatcher.o: include/ntdll.h include/syscall/dispatcher.h
# Header dependencies for ntdll split files
$(BUILDDIR)/ntdll_handle.o: include/ntdll.h
$(BUILDDIR)/ntdll_io.o: include/ntdll.h
$(BUILDDIR)/ntdll_memory.o: include/ntdll.h include/pe.h
$(BUILDDIR)/ntdll_process.o: include/ntdll.h
$(BUILDDIR)/ntdll_objects.o: include/ntdll.h
$(BUILDDIR)/kernel32.o: include/kernel32.h include/ntdll.h include/syscall/thunk_gen.h
$(BUILDDIR)/crt_globals.o: src/stubs/msvcrt_priv.h
$(BUILDDIR)/crt_file.o: src/stubs/msvcrt_priv.h
$(BUILDDIR)/crt_startup.o: src/stubs/msvcrt_priv.h
$(BUILDDIR)/crt_stdio.o: src/stubs/msvcrt_priv.h
$(BUILDDIR)/crt_stdlib.o: src/stubs/msvcrt_priv.h
$(BUILDDIR)/crt_refptrs.o: src/stubs/msvcrt_priv.h include/pe_parser.h
# Header dependencies for loader modules
$(BUILDDIR)/loader/image_mapper.o: include/pe.h include/pe_parser.h
$(BUILDDIR)/loader/import_resolver.o: include/pe.h include/ntdll.h include/kernel32.h include/msvcrt.h src/stubs/msvcrt_priv.h
$(BUILDDIR)/loader/teb_peb.o: include/pe.h
$(BUILDDIR)/loader/entry.o: include/pe.h include/msvcrt.h include/syscall/thunk_gen.h include/syscall/signal_handler.h include/syscall/dispatcher.h src/stubs/msvcrt_priv.h

hello.exe: hello.c build_test.sh
	bash build_test.sh

clean:
	rm -rf $(BUILDDIR) my_wine hello.exe *.o

.PHONY: all clean test hello.exe $(BUILDDIR)
