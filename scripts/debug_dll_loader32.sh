#!/bin/bash
#
# Debug script for dll_loader_32 crash tracing
# Traces CRT startup through LoadLibraryA call and monitors IAT at 0x00407124.
#
# Usage:  ./scripts/debug_dll_loader32.sh
# Output: /tmp/dll_loader_debug.log
#
# Key addresses (PE mapped at 0x00400000):
#   LoadLibraryA IAT:       0x00407124  (should resolve to 0x08055b80)
#   _mainCRTStartup:        0x004014c0  (jumps to ___tmainCRTStartup at 0x00401170)
#   __pei386_runtime_relocator: 0x00401b80
#   _main entry:            0x00402740
#   Before LoadLibraryA:    0x0040277b  (mov DWORD PTR [esp],0x404065)

set -euo pipefail

LOGFILE="/tmp/dll_loader_debug.log"
EXE="./my_wine32"
TARGET="samples/dll_loader_32/dll_loader_32.exe"

echo "=== dll_loader_32 GDB crash trace ==="
echo "Executable: $EXE $TARGET"
echo "Log:        $LOGFILE"
echo ""

gdb -batch \
    -ex "set confirm off" \
    -ex "set pagination off" \
    -ex "set width 160" \
    -ex "set height 500" \
    -ex "set backtrace limit 20" \
    -ex "set print pretty on" \
    -ex "file ${EXE}" \
    -ex "set args ${TARGET}" \
    \
    # ---- Breakpoint 1: setup_fs_and_jump (host function, after PE is mapped) ----
    #     Nested within its commands: all additional breakpoints + hardware watchpoint
    #     This ensures they're all armed before guest code starts executing.
    -ex "break setup_fs_and_jump" \
    -ex "commands" \
    -ex "silent" \
    -ex "printf \"\\n\\\\x1b[1m========= setup_fs_and_jump — guest entry point =========\\\\x1b[0m\\n\"" \
    -ex "printf \"Host -> guest transition after PE mapping.\\n\"" \
    -ex "info registers" \
    -ex "printf \"\\nLoadLibraryA IAT at 0x00407124:\\n\"" \
    -ex "x/20x 0x00407124" \
    -ex "x/4i 0x00407124" \
    -ex "printf \"Expected target 0x08055b80:\\n\"" \
    -ex "x/4i 0x08055b80" \
    -ex "printf \"\\nIAT region dump (0x00407000 - 0x00407200):\\n\"" \
    -ex "x/64x 0x00407000" \
    \
    # Set nested breakpoint at __pei386_runtime_relocator (0x00401b80)
    -ex "break *0x00401b80" \
    -ex "commands" \
    -ex "silent" \
    -ex "printf \"\\n\\\\x1b[1m========== __pei386_runtime_relocator (0x00401b80) ==========\\\\x1b[0m\\n\"" \
    -ex "info registers" \
    -ex "bt 20" \
    -ex "printf \"\\nLoadLibraryA IAT at 0x00407124:\\n\"" \
    -ex "x/20x 0x00407124" \
    -ex "x/4i 0x00407124" \
    -ex "printf \"\\nDisassembly at 0x00401b80:\\n\"" \
    -ex "x/10i 0x00401b80" \
    -ex "printf \"\\nIAT region:\\n\"" \
    -ex "x/32x 0x00407100" \
    -ex "continue" \
    -ex "end" \
    \
    # Set nested breakpoint at _mainCRTStartup (0x004014c0)
    -ex "break *0x004014c0" \
    -ex "commands" \
    -ex "silent" \
    -ex "printf \"\\n\\\\x1b[1m========== _mainCRTStartup (0x004014c0) ==========\\\\x1b[0m\\n\"" \
    -ex "info registers" \
    -ex "bt 20" \
    -ex "printf \"\\nLoadLibraryA IAT at 0x00407124:\\n\"" \
    -ex "x/20x 0x00407124" \
    -ex "x/4i 0x00407124" \
    -ex "printf \"\\nDisassembly at 0x004014c0:\\n\"" \
    -ex "x/10i 0x004014c0" \
    -ex "printf \"\\nIAT region:\\n\"" \
    -ex "x/32x 0x00407100" \
    -ex "continue" \
    -ex "end" \
    \
    # Set nested breakpoint at _main entry (0x00402740)
    -ex "break *0x00402740" \
    -ex "commands" \
    -ex "silent" \
    -ex "printf \"\\n\\\\x1b[1m========== _main entry (0x00402740) ==========\\\\x1b[0m\\n\"" \
    -ex "info registers" \
    -ex "bt 20" \
    -ex "printf \"\\nLoadLibraryA IAT at 0x00407124:\\n\"" \
    -ex "x/20x 0x00407124" \
    -ex "x/4i 0x00407124" \
    -ex "printf \"\\nDisassembly at 0x00402740:\\n\"" \
    -ex "x/10i 0x00402740" \
    -ex "printf \"\\nIAT region:\\n\"" \
    -ex "x/32x 0x00407100" \
    -ex "printf \"\\nStack contents:\\n\"" \
    -ex "x/20x \$esp" \
    -ex "continue" \
    -ex "end" \
    \
    # Set nested breakpoint at 0x0040277b — just before LoadLibraryA call in _main
    -ex "break *0x0040277b" \
    -ex "commands" \
    -ex "silent" \
    -ex "printf \"\\n\\\\x1b[1m========== Before LoadLibraryA call (0x0040277b) ==========\\\\x1b[0m\\n\"" \
    -ex "printf \"mov DWORD PTR [esp],0x404065 — pushing DLL path string\\n\"" \
    -ex "info registers" \
    -ex "bt 20" \
    -ex "printf \"\\nLoadLibraryA IAT at 0x00407124:\\n\"" \
    -ex "x/20x 0x00407124" \
    -ex "x/4i 0x00407124" \
    -ex "printf \"\\nDisassembly at 0x0040277b:\\n\"" \
    -ex "x/20i 0x0040277b" \
    -ex "printf \"\\nIAT region:\\n\"" \
    -ex "x/32x 0x00407100" \
    -ex "printf \"\\nStack contents:\\n\"" \
    -ex "x/30x \$esp" \
    -ex "printf \"\\nDLL path string at 0x00404065:\\n\"" \
    -ex "x/20s 0x00404065" \
    -ex "continue" \
    -ex "end" \
    \
    # Hardware watchpoint on LoadLibraryA IAT entry
    -ex "watch *(uint32_t *)0x00407124" \
    -ex "commands" \
    -ex "silent" \
    -ex "printf \"\\n\\\\x1b[1;31m========== IAT WRITE WATCHPOINT HIT at 0x00407124 ==========\\\\x1b[0m\\n\"" \
    -ex "info registers" \
    -ex "bt 20" \
    -ex "printf \"\\nIAT value after write:\\n\"" \
    -ex "x/4x 0x00407124" \
    -ex "printf \"\\nIAT region:\\n\"" \
    -ex "x/32x 0x00407100" \
    -ex "continue" \
    -ex "end" \
    \
    -ex "continue" \
    -ex "end" \
    \
    # ---- SIGSEGV catch to capture crash state ----
    -ex "catch signal SIGSEGV" \
    -ex "commands" \
    -ex "printf \"\\n\\\\x1b[1;31m========== SIGSEGV CRASH ==========\\\\x1b[0m\\n\"" \
    -ex "info registers" \
    -ex "bt 20" \
    -ex "printf \"\\nEIP (crash address):\\n\"" \
    -ex "print/x \$eip" \
    -ex "printf \"\\nESP at crash:\\n\"" \
    -ex "x/30x \$esp" \
    -ex "printf \"\\nEBP at crash:\\n\"" \
    -ex "x/20x \$ebp" \
    -ex "printf \"\\nIAT at crash:\\n\"" \
    -ex "x/64x 0x00407000" \
    -ex "printf \"\\nCrash instruction:\\n\"" \
    -ex "x/20i \$eip" \
    -ex "printf \"\\nFull PLT region:\\n\"" \
    -ex "x/64x 0x00403000" \
    -ex "quit" \
    -ex "end" \
    \
    2>&1 | tee "$LOGFILE"

EXIT_CODE=${PIPESTATUS[0]}

echo ""
echo "=== Debug complete (exit code: $EXIT_CODE). Log: $LOGFILE ==="
echo "Review: less $LOGFILE"
