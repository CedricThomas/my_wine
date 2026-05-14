#!/bin/bash
#
# PLT IAT dump & JMP thunk scan for dll_loader_32
#
# Dumps the PLT IAT region (0x4074A0+) and scans .text for JMP/CALL thunks
# that target the PLT IAT region. Reads the live IAT slot values at the time
# _main is about to execute (setup_fs_and_jump breakpoint).
#
# Usage:  ./scripts/check_plt_iat.sh
# Output: /tmp/plt_iat_check.log

set -euo pipefail

cd "$(dirname "$0")/.."

LOGFILE="/tmp/plt_iat_check.log"

echo "=== PLT IAT dump & JMP thunk scan for dll_loader_32 ==="
echo "Log:  $LOGFILE"
echo ""

# ── Create a temporary GDB command file ──
GDB_CMDS=$(mktemp /tmp/plt_iat_gdb_XXXXXX.cmd)
trap 'rm -f "$GDB_CMDS"' EXIT

cat > "$GDB_CMDS" << 'GDBEOF'
set confirm off
set pagination off
set width 160
set height 500
set debuginfod enabled off

file ./my_wine32
set args samples/dll_loader_32/dll_loader_32.exe

break setup_fs_and_jump
commands
silent

# ── Dump PLT IAT region ──
printf ">> PLT IAT region: 0x004074a0 (60 halfwords)\n"
x/60hx 0x004074a0

# ── Dump descriptor IAT region for comparison ──
printf "\n>> Descriptor IAT: 0x004070f8 - 0x00407298 (54 dwords)\n"
x/54xw 0x004070f8

# ── Scan .text for JMP thunks (ff 25) and CALL thunks (ff 15) ──
# Classify by target: PLT IAT (>=0x4074a0) vs descriptor IAT (0x4070f8-0x407298)
# Read live IAT slot values for each unique target
python
import struct

text_start   = 0x00401000
text_size    = 0x00001aa4
plt_iat_start = 0x004074a0
desc_iat_start = 0x004070f8
desc_iat_end   = 0x00407298

buf = bytes(gdb.selected_inferior().read_memory(text_start, text_size))

jmp_thunks   = []
call_thunks  = []
plt_targets  = []
desc_targets = []

for i in range(len(buf) - 5):
    b0, b1 = buf[i], buf[i+1]
    if b0 == 0xff and b1 in (0x15, 0x25):
        thunk_va = text_start + i
        iat_addr = struct.unpack_from('<I', buf, i+2)[0]
        ttype = "JMP" if b1 == 0x25 else "CALL"
        if b1 == 0x25:
            jmp_thunks.append((thunk_va, iat_addr))
        else:
            call_thunks.append((thunk_va, iat_addr))
        if iat_addr >= plt_iat_start:
            plt_targets.append((ttype, thunk_va, iat_addr))
        elif desc_iat_start <= iat_addr < desc_iat_end:
            desc_targets.append((ttype, thunk_va, iat_addr))

# ── Print all JMP thunks ──
print(f"\n>> JMP thunks (ff 25) in .text: {len(jmp_thunks)}")
for va, target in jmp_thunks:
    print(f"   0x{va:08x}  jmp  DWORD PTR ds:0x{target:08x}")

# ── Print all CALL thunks ──
print(f"\n>> CALL thunks (ff 15) in .text: {len(call_thunks)}")
for va, target in call_thunks:
    print(f"   0x{va:08x}  call DWORD PTR ds:0x{target:08x}")

# ── Classify targets ──
print(f"\n>> Thunks targeting PLT IAT (>= 0x{plt_iat_start:08x}): {len(plt_targets)}")
if not plt_targets:
    print("   (none — no JMP/CALL thunk in .text targets the PLT IAT region)")
    print("   => PLT IAT is NOT used by this binary; all thunks target descriptor IAT")
else:
    for ttype, va, target in plt_targets:
        raw = bytes(gdb.selected_inferior().read_memory(target, 4))
        val = struct.unpack_from('<I', raw)[0]
        hint = ""
        if val & 0x80000000:
            hint = " [HIBIT32 — unresolved]"
        elif val == 0:
            hint = " [NULL — would crash if called]"
        elif val >= 0x08000000:
            hint = " [resolved to our stub]"
        print(f"   {ttype:4s} 0x{va:08x}  ->  IAT[0x{target:08x}] = 0x{val:08x}{hint}")

print(f"\n>> Thunks targeting descriptor IAT (0x004070f8 - 0x00407298): {len(desc_targets)}")
seen = set()
for ttype, va, target in desc_targets:
    if target not in seen:
        seen.add(target)
        raw = bytes(gdb.selected_inferior().read_memory(target, 4))
        val = struct.unpack_from('<I', raw)[0]
        hint = ""
        if val & 0x80000000:
            hint = " [HIBIT32 — unresolved!]"
        elif val == 0:
            hint = " [NULL — would crash!]"
        elif val >= 0x08000000:
            hint = " [resolved to our stub]"
        print(f"   IAT[0x{target:08x}] = 0x{val:08x}{hint}")

# ── Summary ──
print(f"\n>> Summary")
print(f"   .text range:         0x{text_start:08x} - 0x{text_start + text_size:08x}")
print(f"   JMP thunks:          {len(jmp_thunks)}")
print(f"   CALL thunks:         {len(call_thunks)}")
print(f"   Targeting PLT IAT:   {len(plt_targets)}")
print(f"   Targeting desc IAT:  {len(desc_targets)}")
end

continue
end

# ── Catch crash ──
catch signal SIGSEGV
commands
silent
printf "\n>> CRASH: SIGSEGV\n"
info registers
printf "EIP=0x%08x  EAX=0x%08x  ESP=0x%08x  EBP=0x%08x\n", $eip, $eax, $esp, $ebp
bt 20
printf "\n>> LoadLibraryA IAT (0x00407124) at crash:\n"
x/10xw 0x00407124
printf "\n>> PLT IAT (0x004074a0) at crash:\n"
x/60hx 0x004074a0
printf "\n>> Stack at crash:\n"
x/20wx $esp
quit
end

run
GDBEOF

# ── Execute ──
gdb -batch -x "$GDB_CMDS" 2>&1 | tee "$LOGFILE"

echo ""
echo "=== PLT IAT check complete. Log: $LOGFILE ==="
