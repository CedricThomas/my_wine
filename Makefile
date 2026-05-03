CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -I .
LDFLAGS = -lrt -lpthread -lseccomp

BUILDDIR = build
OBJS = $(BUILDDIR)/my_wine.o $(BUILDDIR)/pe_parser.o $(BUILDDIR)/thunk_gen.o \
       $(BUILDDIR)/signal_handler.o $(BUILDDIR)/dispatcher.o \
       $(BUILDDIR)/ntdll.o $(BUILDDIR)/kernel32.o $(BUILDDIR)/msvcrt.o

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

$(BUILDDIR)/ntdll.o: src/stubs/ntdll.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

STUB_CFLAGS = $(CFLAGS) -mno-red-zone

$(BUILDDIR)/kernel32.o: src/stubs/kernel32.c | $(BUILDDIR)
	$(CC) $(STUB_CFLAGS) -c $< -o $@

$(BUILDDIR)/msvcrt.o: src/stubs/msvcrt.c | $(BUILDDIR)
	$(CC) $(STUB_CFLAGS) -c $< -o $@

# Header dependencies (for recompilation when headers change)
$(BUILDDIR)/my_wine.o: include/pe.h include/ntdll.h include/kernel32.h include/msvcrt.h
$(BUILDDIR)/pe_parser.o: include/pe.h
$(BUILDDIR)/thunk_gen.o: include/syscall/thunk_gen.h include/syscall/signal_handler.h
$(BUILDDIR)/signal_handler.o: include/syscall/signal_handler.h
$(BUILDDIR)/dispatcher.o: include/ntdll.h include/syscall/dispatcher.h
$(BUILDDIR)/ntdll.o: include/ntdll.h
$(BUILDDIR)/kernel32.o: include/kernel32.h include/ntdll.h include/syscall/thunk_gen.h
$(BUILDDIR)/msvcrt.o: include/msvcrt.h

hello.exe: hello.c build_test.sh
	bash build_test.sh

clean:
	rm -rf $(BUILDDIR) my_wine hello.exe *.o

.PHONY: all clean test hello.exe $(BUILDDIR)
