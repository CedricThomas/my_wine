# ── Toolchain ───────────────────────────────────────────────────
CC       = gcc
CFLAGS   = -Wall -Wextra -Werror -O2 -g -I. -Iinclude -MMD -MP -mno-sse
LDFLAGS  = -lrt -lpthread -ldl

# SDL2 backend (optional - needed for DOOM95 render backend)
SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || echo "-I/usr/include/SDL2")
SDL2_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null || echo "-lSDL2")
FLUID_LIBS  := $(shell pkg-config --libs fluidsynth 2>/dev/null || echo "-lfluidsynth")

# 32-bit SDL2 detection — test if -m32 linking actually finds a 32-bit SDL2 lib.
# Without lib32-sdl2 installed the linker rejects 64-bit .so files.
SDL2_LIBS_32 := $(shell echo 'int main(void){return 0;}' | $(CC) -m32 -x c - -o /tmp/__sdl2_32_test $(SDL2_LIBS) -lm 2>/dev/null && \
	echo "$(SDL2_LIBS) -lm" && rm -f /tmp/__sdl2_32_test || echo "")

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
LOADER_SRC   = $(sort $(shell find src/loader  -maxdepth 1 -name '*.c' | grep -v pe32_entry.c | grep -v pe32_process.c | grep -v pe32_guest_launch.c | grep -v pe32_bootstrap.c))
SYSCALL_SRC  = $(sort $(shell find src/syscall -maxdepth 1 -name '*.c' | grep -v dispatcher_generated.c))
HEAP_SRC     = $(sort $(shell find src/heap    -maxdepth 1 -name '*.c'))
CRT_SRC      = $(sort $(shell find src/crt     -maxdepth 1 -name '*.c'))

ROOT_OBJS    = $(patsubst src/%.c,$(BUILDDIR)/%.o,$(ROOT_SRC))
STUBS_OBJS   = $(patsubst src/msvcrt/%.c,$(BUILDDIR)/%.o,$(STUBS_SRC))
LOADER_OBJS  = $(patsubst src/loader/%.c,$(BUILDDIR)/%.o,$(LOADER_SRC))
SYSCALL_OBJS = $(patsubst src/syscall/%.c,$(BUILDDIR)/%.o,$(SYSCALL_SRC))
HEAP_OBJS    = $(patsubst src/heap/%.c,$(BUILDDIR)/%.o,$(HEAP_SRC))
CRT_OBJS     = $(patsubst src/crt/%.c,$(BUILDDIR)/%.o,$(CRT_SRC))

# ── Source Groups: SDL2 Backend ─────────────────────────────────
BACKEND_SRC = $(sort $(shell find src/backend -name '*.c' 2>/dev/null))
BACKEND_OBJS = $(patsubst src/backend/%.c,$(BUILDDIR)/backend/%.o,$(BACKEND_SRC))
BACKEND32_OBJS = $(if $(SDL2_LIBS_32),$(patsubst src/backend/%.c,$(BUILDDIR32)/backend/%.o,$(BACKEND_SRC)))

OBJS = $(ROOT_OBJS) $(STUBS_OBJS) $(LOADER_OBJS) $(SYSCALL_OBJS) $(HEAP_OBJS) $(CRT_OBJS) $(BACKEND_OBJS) $(BUILDDIR)/run_guest.o $(BUILDDIR)/dispatcher_entry_asm.o $(BUILDDIR)/clone64.o

# ── Source Groups: PE32 Child ───────────────────────────────────
# 32-bit stubs: handler_Nt* providers + kernel32 module loading + handle_manager.
# Exclude crt_*.c (64-bit CRT emulation, not needed in standalone 32-bit child),
# but re-include the CRT infra needed by crt_mingw.c for the 32-bit CRT module path.
MY_WINE32_STUBS_SRC = $(filter-out src/msvcrt/crt_%.c $(if $(SDL2_LIBS_32),,src/msvcrt/user32_window.c src/msvcrt/user32_message.c src/msvcrt/user32_message_hook.c src/msvcrt/user32_input.c), \
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
	$(BUILDDIR32)/pe32_bootstrap.o \
	$(BUILDDIR32)/pe32_doom95_compat.o \
	$(BUILDDIR32)/pe32_entry_resolve.o \
	$(BUILDDIR32)/pe32_guest_launch.o \
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
	$(MY_WINE32_CRT_OBJS) \
	$(BACKEND32_OBJS)

# ── Source Groups: Tests ────────────────────────────────────────
PE_OBJS = $(BUILDDIR)/pe_headers.o $(BUILDDIR)/pe_imports.o \
	$(BUILDDIR)/pe_symbols.o $(BUILDDIR)/pe_rip_scan.o

IMPORT_LOADER_OBJS = $(BUILDDIR)/image_mapper.o $(BUILDDIR)/import_table.o \
	$(BUILDDIR)/import_resolve.o $(BUILDDIR)/import_init.o $(BUILDDIR)/ordinal_table.o \
	$(BUILDDIR)/relocations.o $(BUILDDIR)/export_table.o $(BUILDDIR)/module_list.o \
	$(BUILDDIR)/dll_path.o $(BUILDDIR)/dll_loader.o

# Shared objects used by import-resolution and teb_peb tests.
TEST_IMPORT_OBJS = $(PE_OBJS) $(IMPORT_LOADER_OBJS) $(filter-out $(BUILDDIR)/user32_window.o $(BUILDDIR)/user32_window_lifecycle.o $(BUILDDIR)/user32_window_ops.o $(BUILDDIR)/user32_paint.o $(BUILDDIR)/user32_message.o $(BUILDDIR)/user32_message_dispatch.o $(BUILDDIR)/user32_message_hook.o $(BUILDDIR)/user32_message_queue.o $(BUILDDIR)/user32_input.o, $(STUBS_OBJS)) $(HEAP_OBJS) \
	$(CRT_OBJS) \
	$(BUILDDIR)/thunk_gen.o $(BUILDDIR)/dispatcher_entry.o $(BUILDDIR)/abi_wrappers.o \
	$(BUILDDIR)/gs_base.o $(BUILDDIR)/common.o $(BUILDDIR)/clone64.o

# Non-crt stubs (syscall dispatch test does not need the CRT stubs).
STUBS_NO_CRT_OBJS = $(filter-out $(BUILDDIR)/crt_%.o $(BUILDDIR)/user32_window.o $(BUILDDIR)/user32_window_lifecycle.o $(BUILDDIR)/user32_window_ops.o $(BUILDDIR)/user32_paint.o $(BUILDDIR)/user32_message.o $(BUILDDIR)/user32_message_dispatch.o $(BUILDDIR)/user32_message_hook.o $(BUILDDIR)/user32_message_queue.o $(BUILDDIR)/user32_input.o, $(STUBS_OBJS))

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
TEST_entry_symbols_OBJS = $(PE_OBJS) $(BUILDDIR)/crt.o $(BUILDDIR)/crt_mingw.o $(BUILDDIR)/crt_watcom.o $(BUILDDIR)/crt_globals.o $(BUILDDIR)/crt_offset_discovery.o $(BUILDDIR)/crt_refptrs.o $(BUILDDIR)/common.o $(BUILDDIR)/debug.o
TEST_doom95_paths_OBJS = $(TEST_IMPORT_OBJS) $(BUILDDIR)/peb_ldr.o $(BUILDDIR)/debug.o
TEST_ddraw_OBJS = $(BACKEND_OBJS) $(BUILDDIR)/handle_manager.o $(BUILDDIR)/user32_class_registry.o $(BUILDDIR)/user32_focus.o $(BUILDDIR)/user32_window.o $(BUILDDIR)/user32_window_lifecycle.o $(BUILDDIR)/user32_window_ops.o $(BUILDDIR)/user32_window_state.o $(BUILDDIR)/user32_paint.o $(BUILDDIR)/user32_message.o $(BUILDDIR)/user32_message_dispatch.o $(BUILDDIR)/user32_message_hook.o $(BUILDDIR)/user32_message_queue.o $(BUILDDIR)/user32_input.o $(BUILDDIR)/ddraw_backend.o $(BUILDDIR)/ddraw_core.o $(BUILDDIR)/ddraw_interface.o $(BUILDDIR)/ddraw_clipper.o $(BUILDDIR)/ddraw_mode.o $(BUILDDIR)/ddraw_palette.o $(BUILDDIR)/ddraw_surface.o $(BUILDDIR)/ddraw_surface_desc.o $(BUILDDIR)/ddraw_surface_ops.o $(BUILDDIR)/debug.o
TEST_dsound_OBJS = $(BACKEND_OBJS) $(BUILDDIR)/handle_manager.o $(BUILDDIR)/dsound_interface.o $(BUILDDIR)/dsound_buffer.o $(BUILDDIR)/dsound_buffer_create.o $(BUILDDIR)/dsound_buffer_control.o $(BUILDDIR)/debug.o
TEST_user32_handle_ownership_OBJS = $(BACKEND_OBJS) $(BUILDDIR)/handle_manager.o $(BUILDDIR)/user32_class_registry.o $(BUILDDIR)/user32_focus.o $(BUILDDIR)/user32_window.o $(BUILDDIR)/user32_window_lifecycle.o $(BUILDDIR)/user32_window_ops.o $(BUILDDIR)/user32_window_state.o $(BUILDDIR)/user32_paint.o $(BUILDDIR)/user32_message.o $(BUILDDIR)/user32_message_dispatch.o $(BUILDDIR)/user32_message_hook.o $(BUILDDIR)/user32_message_queue.o $(BUILDDIR)/user32_input.o $(BUILDDIR)/debug.o
TEST_user32_message_dispatch_OBJS = $(BACKEND_OBJS) $(BUILDDIR)/handle_manager.o $(BUILDDIR)/user32_class_registry.o $(BUILDDIR)/user32_focus.o $(BUILDDIR)/user32_window.o $(BUILDDIR)/user32_window_lifecycle.o $(BUILDDIR)/user32_window_ops.o $(BUILDDIR)/user32_window_state.o $(BUILDDIR)/user32_paint.o $(BUILDDIR)/user32_message.o $(BUILDDIR)/user32_message_dispatch.o $(BUILDDIR)/user32_message_hook.o $(BUILDDIR)/user32_message_queue.o $(BUILDDIR)/user32_input.o $(BUILDDIR)/debug.o
TEST_user32_dialog_OBJS = $(TEST_user32_handle_ownership_OBJS) $(BUILDDIR)/user32_dialog.o $(BUILDDIR)/user32_dialog_controls.o $(BUILDDIR)/user32_dialog_doom95.o $(BUILDDIR)/resource_win32.o $(BUILDDIR)/kernel32_path.o $(BUILDDIR)/common.o

# ── Search Paths And Per-target Flags ───────────────────────────
vpath %.c src src/msvcrt src/loader src/syscall src/heap src/crt src/backend tests
vpath %.S src src/syscall

# Pattern rules use $(CFLAGS) by default. Override for files needing
# $(SPECIAL_CFLAGS) (entry points, loader core, stubs, syscall infra).
SPECIAL_OBJS = main.o common.o entry.o teb_peb.o guest_setup.o crash_handlers.o gs_base.o \
	thunk_gen.o dispatcher.o dispatcher_entry_asm.o clone64.o abi_wrappers.o import_resolve.o image_mapper.o dll_path.o dll_loader.o crt.o crt_mingw.o crt_watcom.o
$(foreach obj,$(SPECIAL_OBJS),$(eval CFLAGS_$(obj) = $(SPECIAL_CFLAGS)))
$(foreach obj,$(notdir $(STUBS_OBJS)),$(eval CFLAGS_$(obj) = $(SPECIAL_CFLAGS)))
$(foreach obj,$(notdir $(SYSCALL_OBJS)),$(eval CFLAGS_$(obj) = $(SPECIAL_CFLAGS)))
$(foreach obj,$(notdir $(HEAP_OBJS)),$(eval CFLAGS_$(obj) = $(SPECIAL_CFLAGS)))

# winmm_doom95 uses host multimedia libraries and keeps stack realignment enabled
# for the 32-bit guest-facing build.
CFLAGS_winmm_doom95.o = $(filter-out -mno-sse,$(SPECIAL_CFLAGS)) -mstackrealign

# user32_window calls rb_call_on_host_stack which switches to host stack;
# disable stack protector to avoid false positive canary corruption.
CFLAGS_user32_window.o = $(SPECIAL_CFLAGS)
CFLAGS_user32_message.o = $(SPECIAL_CFLAGS)
CFLAGS_user32_message_hook.o = $(SPECIAL_CFLAGS)
CFLAGS_user32_input.o = $(SPECIAL_CFLAGS)

# pe32plus_musl_malloc_backend needs extra include paths for stubs and musl source.
CFLAGS_pe32plus_musl_malloc_backend.o = $(SPECIAL_CFLAGS) -Isrc/heap/musl_stubs -Isrc/heap/musl_src

# Backend files that use rb_call_on_host_stack need -fno-stack-protector
# because the inline asm switches to a different stack and the canary
# can be corrupted by the host function calling back to guest memory.
$(foreach f,rb_audio.o rb_event.o rb_init.o rb_input.o rb_palette.o rb_surface.o rb_surface_present.o rb_window.o rb_window_state.o,$(eval CFLAGS_backend/sdl2/$(f) = $(SPECIAL_CFLAGS) $(SDL2_CFLAGS)))

# ── Default Target ──────────────────────────────────────────────
all: my_wine my_wine64 my_wine32 samples $(BUILDDIR)/test_parse $(BUILDDIR)/test_import_resolution \
	$(BUILDDIR)/test_teb_peb $(BUILDDIR)/test_syscall_dispatch \
	$(BUILDDIR)/test_relocations $(BUILDDIR)/test_module_registry \
	$(BUILDDIR)/test_export_parsing $(BUILDDIR)/test_pe32 \
	$(BUILDDIR)/test_syscall_safe_utils $(BUILDDIR)/test_entry_symbols \
	$(BUILDDIR)/test_doom95_paths $(BUILDDIR)/test_pe32_launch \
	$(BUILDDIR)/test_ddraw $(BUILDDIR)/test_dsound \
	$(BUILDDIR)/test_sdl2_backend $(BUILDDIR)/test_user32_handle_ownership \
	$(BUILDDIR)/test_user32_message_dispatch $(BUILDDIR)/test_user32_dialog \
	$(if $(SDL2_LIBS_32),$(BUILDDIR32)/test_sdl2_backend,)

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

$(BUILDDIR32)/winmm_doom95.o: src/msvcrt/winmm_doom95.c | $(BUILDDIR32)
	@echo "  CC32 $<"
	@$(MY_WINE32_CC) $(filter-out -mno-sse,$(MY_WINE32_CFLAGS)) -mstackrealign -c $< -o $@

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
	@$(CC) $(CFLAGS) -o my_wine64 $(OBJS) $(LDFLAGS) $(SDL2_LIBS) $(FLUID_LIBS) -lm

# ── PE32 Runtime Binary ─────────────────────────────────────────
# my_wine32 uses pe32_entry.c as main() entry point.
my_wine32: $(MY_WINE32_OBJS)
	@echo "==== Link my_wine32 ===="
	@$(MY_WINE32_CC) -no-pie -o my_wine32 $(MY_WINE32_OBJS) -lpthread $(SDL2_LIBS_32) $(FLUID_LIBS)

# ── Test Targets ────────────────────────────────────────────────
# Test binaries (native ELF) plus the hello_world sample .exe they exercise.
SHELL.EXE = samples/hello_world/hello_world.exe
ENTRY_TEST_32_EXE = samples/entry_test_32/entry_test_32.exe

$(SHELL.EXE):
	@bash scripts/samples.sh build hello_world

$(ENTRY_TEST_32_EXE):
	@bash scripts/samples.sh build entry_test_32

tests: my_wine64 my_wine32 $(SHELL.EXE) $(ENTRY_TEST_32_EXE) $(BUILDDIR)/test_parse $(BUILDDIR)/test_import_resolution \
		$(BUILDDIR)/test_teb_peb $(BUILDDIR)/test_syscall_dispatch \
		$(BUILDDIR)/test_relocations $(BUILDDIR)/test_module_registry \
		$(BUILDDIR)/test_export_parsing $(BUILDDIR)/test_pe32 \
		$(BUILDDIR)/test_syscall_safe_utils $(BUILDDIR)/test_entry_symbols \
		$(BUILDDIR)/test_doom95_paths $(BUILDDIR)/test_pe32_launch \
		$(BUILDDIR)/test_ddraw $(BUILDDIR)/test_dsound \
		$(BUILDDIR)/test_sdl2_backend $(BUILDDIR)/test_user32_handle_ownership \
		$(BUILDDIR)/test_user32_message_dispatch $(BUILDDIR)/test_user32_dialog \
		$(if $(SDL2_LIBS_32),$(BUILDDIR32)/test_sdl2_backend,)

run-tests: tests
	@echo "==== Running tests ===="
	@bash scripts/run_tests.sh $(TEST)

debug-tests: tests
	@echo "==== Running tests with debug ===="
	@bash scripts/run_tests.sh --debug

define TEST_RULE
$(BUILDDIR)/test_$(1): tests/test_$(1).c $(2)
	@echo "  LD $$@"
	@$(CC) $(CFLAGS) -I include -o $$@ $$^ $(LDFLAGS) $(SDL2_LIBS) $(FLUID_LIBS) -lm
endef

$(eval $(call TEST_RULE,parse,$(TEST_parse_OBJS)))
$(eval $(call TEST_RULE,import_resolution,$(TEST_import_resolution_OBJS)))
$(eval $(call TEST_RULE,teb_peb,$(TEST_teb_peb_OBJS)))
$(eval $(call TEST_RULE,syscall_dispatch,$(TEST_syscall_dispatch_OBJS)))
$(eval $(call TEST_RULE,relocations,$(TEST_relocations_OBJS)))
$(eval $(call TEST_RULE,module_registry,$(TEST_module_registry_OBJS)))
$(eval $(call TEST_RULE,export_parsing,$(TEST_export_parsing_OBJS)))
$(eval $(call TEST_RULE,pe32,$(TEST_pe32_OBJS)))
$(eval $(call TEST_RULE,entry_symbols,$(TEST_entry_symbols_OBJS)))
$(eval $(call TEST_RULE,doom95_paths,$(TEST_doom95_paths_OBJS)))
$(eval $(call TEST_RULE,syscall_safe_utils,))
$(eval $(call TEST_RULE,pe32_launch,))

# ── SDL2 Backend Library ────────────────────────────────────────
backend: $(BACKEND_OBJS)
	@echo "==== SDL2 backend objects built ===="

# SDL2 backend test
TEST_sdl2_backend_OBJS = $(BACKEND_OBJS) $(BUILDDIR)/handle_manager.o $(BUILDDIR)/debug.o
$(BUILDDIR)/test_sdl2_backend: tests/test_sdl2_backend.c $(TEST_sdl2_backend_OBJS)
	@echo "  LD $@"
	@$(CC) $(CFLAGS) $(SDL2_CFLAGS) -o $@ $^ $(SDL2_LIBS) -lm

$(BUILDDIR)/test_ddraw: tests/test_ddraw.c $(TEST_ddraw_OBJS)
	@echo "  LD $@"
	@$(CC) $(CFLAGS) $(SDL2_CFLAGS) -o $@ $^ $(SDL2_LIBS) -lm

$(BUILDDIR)/test_dsound: tests/test_dsound.c $(TEST_dsound_OBJS)
	@echo "  LD $@"
	@$(CC) $(CFLAGS) $(SDL2_CFLAGS) -o $@ $^ $(SDL2_LIBS) -lm

$(BUILDDIR)/test_user32_handle_ownership: tests/test_user32_handle_ownership.c $(TEST_user32_handle_ownership_OBJS)
	@echo "  LD $@"
	@$(CC) $(CFLAGS) $(SDL2_CFLAGS) -o $@ $^ $(SDL2_LIBS) -lm

$(BUILDDIR)/test_user32_message_dispatch: tests/test_user32_message_dispatch.c $(TEST_user32_message_dispatch_OBJS)
	@echo "  LD $@"
	@$(CC) $(CFLAGS) $(SDL2_CFLAGS) -o $@ $^ $(SDL2_LIBS) -lm

$(BUILDDIR)/test_user32_dialog: tests/test_user32_dialog.c $(TEST_user32_dialog_OBJS)
	@echo "  LD $@"
	@$(CC) $(CFLAGS) $(SDL2_CFLAGS) -o $@ $^ $(SDL2_LIBS) -lm

$(BUILDDIR)/backend/%.o: %.c | $(BUILDDIR)
	@mkdir -p $(@D)
	@echo "  CC-SDL2 $<"
	@$(CC) $(filter-out -mno-sse,$(CFLAGS)) -mstackrealign -fno-stack-protector $(SDL2_CFLAGS) -c $< -o $@

# 32-bit backend compile rule
$(BUILDDIR32)/backend/%.o: %.c | $(BUILDDIR32)
	@mkdir -p $(@D)
	@echo "  CC32-SDL2 $<"
	@$(MY_WINE32_CC) $(filter-out -mno-sse,$(MY_WINE32_CFLAGS)) -mstackrealign -fno-stack-protector $(SDL2_CFLAGS) -c $< -o $@

# SDL2 backend test (32-bit)
TEST_sdl2_backend32_OBJS = $(BACKEND32_OBJS) $(BUILDDIR32)/handle_manager.o
$(BUILDDIR32)/test_sdl2_backend: tests/test_sdl2_backend.c $(TEST_sdl2_backend32_OBJS)
	@echo "  LD32 $@"
	@$(MY_WINE32_CC) -no-pie $(filter-out -mno-sse,$(MY_WINE32_CFLAGS)) $(SDL2_CFLAGS) -o $@ $^ $(SDL2_LIBS_32)

TEST_dsound32_OBJS = $(BACKEND32_OBJS) $(BUILDDIR32)/handle_manager.o $(BUILDDIR32)/dsound_interface.o $(BUILDDIR32)/dsound_buffer.o $(BUILDDIR32)/debug.o
$(BUILDDIR32)/test_dsound: tests/test_dsound.c $(TEST_dsound32_OBJS)
	@echo "  LD32 $@"
	@$(MY_WINE32_CC) -no-pie $(filter-out -mno-sse,$(MY_WINE32_CFLAGS)) $(SDL2_CFLAGS) -I include -o $@ $^ $(SDL2_LIBS_32)

# ── Sample Scenarios ────────────────────────────────────────────
# Cross-compile sample scenarios to PE .exe via Docker (mingw-w64).
# Unit-style native checks live under tests/. Samples are end-to-end scenario
# programs that exercise the loader like a user-visible PE application.
# See: scripts/samples.sh
#
#   make samples                             build all sample binaries and unpack registered archives
#   make samples SAMPLE=foo                  build one sample binary and unpack its archive if registered
#   make graphical-samples SAMPLE=foo        build one graphical sample binary
#   make run-samples-scenarios SAMPLE=foo    unified console/graphical scenario runner
#   make unpack-samples SAMPLE=foo           unpack one registered sample archive
#   make screenshot-doom95                   capture Doom95 via Docker/Xvfb
SAMPLE ?=

samples:
	@bash scripts/samples.sh build $(SAMPLE)
	@bash scripts/unpack_samples.sh $(SAMPLE)

unpack-samples:
	@bash scripts/unpack_samples.sh $(SAMPLE)

run-samples-scenarios: my_wine my_wine64 my_wine32
	@bash scripts/run_samples.sh $(SAMPLE)
graphical-samples:
	@bash scripts/graphical_samples.sh build $(SAMPLE)

build-docker-image:
	@echo "Building my_wine-samples Docker image..."
	@DOCKER_BUILDKIT=0 docker build -t my_wine-samples . || { echo "FAIL: Docker build failed"; exit 1; }
	@echo "OK  my_wine-samples image ready"

screenshot-doom95:
	@bash scripts/capture_screenshot.sh

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
-include $(wildcard $(BACKEND_OBJS:.o=.d))
-include $(wildcard $(MY_WINE32_OBJS:.o=.d))

.PHONY: all clean fclean re tests run-tests debug-tests samples unpack-samples run-samples-scenarios graphical-samples build-docker-image gen gen-dispatcher check-generated backend $(BUILDDIR) $(BUILDDIR32)
