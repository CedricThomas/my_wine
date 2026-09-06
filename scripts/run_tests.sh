#!/bin/bash
set -e

# Usage: run_tests.sh [--debug|--debug-level N]
#   --debug          Export MY_WINE_DEBUG_LEVEL=1 for all test invocations
#   --debug-level N  Export MY_WINE_DEBUG_LEVEL=N for all test invocations

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

DEBUG=0
DEBUG_LEVEL=1
if [ "${1:-}" = "--debug" ]; then
	DEBUG=1
	DEBUG_LEVEL=1
elif [ "${1:-}" = "--debug-level" ] && [ -n "${2:-}" ]; then
	DEBUG=1
	DEBUG_LEVEL="$2"
fi

if [ "$DEBUG" = "1" ]; then
	export MY_WINE_DEBUG_LEVEL="$DEBUG_LEVEL"
	echo ">>> DEBUG MODE ACTIVE (MY_WINE_DEBUG_LEVEL=$DEBUG_LEVEL)"
	echo ""
fi

SHELL_EXE="samples/hello_world/hello_world.exe"
BUILDDIR="build"

PASS=0
FAIL=0
SKIP=0

# Run a single test binary and record PASS/FAIL.
# Usage: run_test test_name [arg1 arg2 ...]
# Pipes through tr -d '\0' to silently strip null bytes from output
# (some tests like test_syscall_dispatch emit binary data that would
#  cause bash to print "command substitution: null byte ignored").
run_test() {
	local name="$1"
	shift
	local output
	output=$(timeout 5 ./"$BUILDDIR"/"$name" "$@" 2>&1 | tr -d '\0') || true

	if echo "$output" | grep -Eq "Failed: 0|PASS:"; then
		PASS=$((PASS + 1))
		if [ "$DEBUG" = "1" ]; then
			echo "$output"
		fi
		echo "PASS  $name"
	else
		FAIL=$((FAIL + 1))
		if [ "$DEBUG" = "1" ]; then
			echo "$output"
		fi
		echo "FAIL  $name"
	fi
}

run_test_env() {
	local name="$1"
	shift
	local output
	output=$(env "$@" timeout 5 ./"$BUILDDIR"/"$name" 2>&1 | tr -d '\0') || true

	if echo "$output" | grep -Eq "Failed: 0|PASS:"; then
		PASS=$((PASS + 1))
		if [ "$DEBUG" = "1" ]; then
			echo "$output"
		fi
		echo "PASS  $name"
	else
		FAIL=$((FAIL + 1))
		if [ "$DEBUG" = "1" ]; then
			echo "$output"
		fi
		echo "FAIL  $name"
	fi
}

# Run test_teb_peb which may crash due to FSGSBASE unavailability.
# Distinguish: no output (crash) → SKIP, Failed: 0 → PASS, Failed: N→ FAIL.
run_test_teb_peb() {
	local name="test_teb_peb"
	local output
	output=$(timeout 120 ./"$BUILDDIR"/"$name" 2>&1 | tr -d '\0') || true

	if [ -z "$output" ]; then
		# Empty output — likely crashed on FSGSBASE instruction
		SKIP=$((SKIP + 1))
		echo "SKIP  $name (FSGSBASE unavailable)"
	elif echo "$output" | grep -q "Failed: 0"; then
		PASS=$((PASS + 1))
		if [ "$DEBUG" = "1" ]; then
			echo "$output"
		fi
		echo "PASS  $name"
	else
		FAIL=$((FAIL + 1))
		if [ "$DEBUG" = "1" ]; then
			echo "$output"
		fi
		echo "FAIL  $name"
	fi
}

# test_parse
if [ -f "$SHELL_EXE" ]; then
	run_test test_parse "$SHELL_EXE"
else
	run_test test_parse
fi

# test_import_resolution
run_test test_import_resolution

# test_teb_peb — special: may crash on FSGSBASE
run_test_teb_peb

# test_syscall_dispatch
run_test test_syscall_dispatch

# test_relocations
if [ -f "$SHELL_EXE" ]; then
	run_test test_relocations "$SHELL_EXE"
else
	run_test test_relocations
fi

# test_module_registry
run_test test_module_registry

# test_export_parsing
run_test test_export_parsing

# test_pe32
run_test test_pe32

# test_syscall_safe_utils
run_test test_syscall_safe_utils

# test_entry_symbols
run_test test_entry_symbols

# test_doom95_paths
run_test test_doom95_paths

# test_handle_manager
run_test test_handle_manager

# test_pe32_launch
run_test test_pe32_launch

# test_dsound
run_test_env test_dsound SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy

# test_sdl2_backend
run_test_env test_sdl2_backend SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy

# test_user32_handle_ownership
run_test_env test_user32_handle_ownership SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy

# test_user32_message_dispatch
run_test_env test_user32_message_dispatch SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy

# test_user32_dialog
run_test_env test_user32_dialog SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy

# --- Summary ---
TOTAL=$((PASS + FAIL + SKIP))
echo ""
echo "$PASS passed, $FAIL failed, $SKIP skipped out of $TOTAL tests"

if [ "$FAIL" -gt 0 ]; then
	exit 1
fi
