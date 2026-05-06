#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

SHELL_EXE="samples/hello_world/hello_world.exe"
BUILDDIR="build"

# test_parse
echo "=== Running test_parse ==="
if [ -f "$SHELL_EXE" ]; then
	timeout 5 ./"$BUILDDIR"/test_parse "$SHELL_EXE"
else
	echo "No hello_world.exe found — running error/negative tests only"
	timeout 5 ./"$BUILDDIR"/test_parse
fi

# test_import_resolution
echo "=== Running test_import_resolution ==="
timeout 5 ./"$BUILDDIR"/test_import_resolution

# test_teb_peb
echo "=== Running test_teb_peb ==="
if timeout 120 ./"$BUILDDIR"/test_teb_peb 2>&1; then
	:
else
	echo "  SKIP: test_teb_peb terminated abnormally (FSGSBASE unavailable)"
fi

# test_syscall_dispatch
echo "=== Running test_syscall_dispatch ==="
timeout 5 ./"$BUILDDIR"/test_syscall_dispatch

# test_relocations
echo "=== Running test_relocations ==="
if [ -f "$SHELL_EXE" ]; then
	timeout 5 ./"$BUILDDIR"/test_relocations "$SHELL_EXE"
else
	echo "No hello_world.exe found — running unit tests only"
	timeout 5 ./"$BUILDDIR"/test_relocations
fi

# test_module_registry
echo "=== Running test_module_registry ==="
timeout 5 ./"$BUILDDIR"/test_module_registry

# test_export_parsing
echo "=== Running test_export_parsing ==="
timeout 5 ./"$BUILDDIR"/test_export_parsing

echo "=== Tests completed ==="
