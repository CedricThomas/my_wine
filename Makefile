CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -I .
LDFLAGS = -lrt -lpthread -lseccomp

BUILDDIR = build
OBJS = $(BUILDDIR)/my_wine.o $(BUILDDIR)/pe_parser.o $(BUILDDIR)/thunk_gen.o \
       $(BUILDDIR)/signal_handler.o $(BUILDDIR)/dispatcher.o \
       $(BUILDDIR)/ntdll_handle.o $(BUILDDIR)/ntdll_io.o \
       $(BUILDDIR)/ntdll_memory.o $(BUILDDIR)/ntdll_process.o \
       $(BUILDDIR)/ntdll_objects.o $(BUILDDIR)/kernel32.o $(BUILDDIR)/msvcrt.o \
       $(BUILDDIR)/run_guest.o \
       $(BUILDDIR)/loader/image_mapper.o \
       $(BUILDDIR)/loader/import_resolver.o \
       $(BUILDDIR)/loader/teb_peb.o \
       $(BUILDDIR)/loader/entry.o

all: my_wine

test: all $(BUILDDIR)/pe_parser.o
	@echo "=== Compiling tests ==="
	$(CC) $(CFLAGS) -I include -o $(BUILDDIR)/test_parse tests/test_parse.c $(BUILDDIR)/pe_parser.o
	@echo "=== Running tests ==="
	@if [ -f hello.exe ]; then \
		./$(BUILDDIR)/test_parse hello.exe; \
	elif [ -f examples/hello.exe ]; then \
		./$(BUILDDIR)/test_parse examples/hello.exe; \
	else \
		echo "SKIP: no hello.exe found (build it with 'make hello.exe' first)"; \
	fi
	@echo "=== Tests completed ==="

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

$(BUILDDIR)/msvcrt.o: src/stubs/msvcrt.c | $(BUILDDIR)
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
$(BUILDDIR)/msvcrt.o: include/msvcrt.h include/pe_parser.h
# Header dependencies for loader modules
$(BUILDDIR)/loader/image_mapper.o: include/pe.h include/pe_parser.h
$(BUILDDIR)/loader/import_resolver.o: include/pe.h include/ntdll.h include/kernel32.h include/msvcrt.h
$(BUILDDIR)/loader/teb_peb.o: include/pe.h
$(BUILDDIR)/loader/entry.o: include/pe.h include/msvcrt.h include/syscall/thunk_gen.h include/syscall/signal_handler.h include/syscall/dispatcher.h

hello.exe: hello.c build_test.sh
	bash build_test.sh

clean:
	rm -rf $(BUILDDIR) my_wine hello.exe *.o

.PHONY: all clean test hello.exe $(BUILDDIR)
