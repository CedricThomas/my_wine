CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -I .
LDFLAGS = -lrt -lpthread -lseccomp

# Object files stay in project root
OBJS = my_wine.o pe_parser.o thunk_gen.o signal_handler.o dispatcher.o \
       ntdll.o kernel32.o msvcrt.o

all: my_wine

my_wine: $(OBJS)
	$(CC) $(CFLAGS) -o my_wine $(OBJS) $(LDFLAGS)

# Explicit compile rules mapping subdirectory sources to root .o files
my_wine.o: src/main.c
	$(CC) $(CFLAGS) -c $< -o $@

pe_parser.o: src/pe_parser.c
	$(CC) $(CFLAGS) -c $< -o $@

thunk_gen.o: src/syscall/thunk_gen.c
	$(CC) $(CFLAGS) -c $< -o $@

signal_handler.o: src/syscall/signal_handler.c
	$(CC) $(CFLAGS) -c $< -o $@

dispatcher.o: src/syscall/dispatcher.c
	$(CC) $(CFLAGS) -c $< -o $@

ntdll.o: src/stubs/ntdll.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel32.o: src/stubs/kernel32.c
	$(CC) $(CFLAGS) -c $< -o $@

msvcrt.o: src/stubs/msvcrt.c
	$(CC) $(CFLAGS) -c $< -o $@

# Header dependencies (for recompilation when headers change)
my_wine.o: include/pe.h include/ntdll.h include/kernel32.h include/msvcrt.h
pe_parser.o: include/pe.h
thunk_gen.o: include/syscall/thunk_gen.h include/syscall/signal_handler.h
signal_handler.o:
dispatcher.o: include/ntdll.h include/syscall/dispatcher.h
ntdll.o: include/ntdll.h
kernel32.o: include/kernel32.h include/ntdll.h include/syscall/thunk_gen.h
msvcrt.o: include/msvcrt.h

hello.exe: hello.c build_test.sh
	bash build_test.sh

clean:
	rm -f *.o my_wine test_parse hello.exe

.PHONY: all clean hello.exe
