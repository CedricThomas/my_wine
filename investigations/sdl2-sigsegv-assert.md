# INV-001: SDL2 Samples SIGSEGV with Corrupted ucontext

## Symptom

All SDL2-based PE samples crash immediately after SDL initialization:

```
WARNING: SDL backends requested video=x11 audio=dummy active video=x11 audio=dummy
CRASH: SIGSEGV
CRASH: si_addr=0x00000000, ucontext=0x0000006b
CRASH: ucontext invalid, skipping register dump
CRASH: fallback ESP=0x0061ee20, EBP=0x0061ee20, signal_ret=0xf467fbc2, stack=[08e0fc00 f467fbc2 0061ee48 52430000]
```

Affecting: `sdl2_window_32`, `sdl2_nccreate_reject_32`, `sdl2_two_window_32`, `sdl2_window_altf4_32`, `sdl2_window_closewindow_32`, `sdl2_window_sigint_32`, `sdl2_window_timeout_32`

## Diff vs Working

**NO DIFFERENCE.** The working version produces the identical crash:

```
$ refs/working/my_wine32 samples/sdl2_window_32/*.exe
WARNING: SDL backends requested video=x11 audio=dummy active video=x11 audio=dummy
CRASH: SIGSEGCRASH: si_addr=0x00000000, ucontext=0x0000006b
CRASH: ucontext invalid, skipping register dump
CRASH: fallback ESP=0x0061ee20, EBP=0x0061ee20, signal_ret=0xf464bbc2, stack=[0a2ec040 f464bbc2 0061ee48 52430000]
```

## Hypotheses

### H1: Signal stack at 0x00800000 is overwritten by SDL2

The alternate signal stack is mapped at `0x00800000` (64KB). If SDL2 allocates or maps memory that overlaps this range, the signal frame gets corrupted.

### H2: glibc TLS corruption from FS-based access

The crash handler runs on the alt signal stack, but it still accesses `__thread` variables via FS. If FS points to guest TEB at signal time, the crash handler reads garbage.

### H3: SDL2 internal signal handler installation

SDL2 may install its own signal handlers that conflict with my_wine's, causing the kernel to use the wrong signal frame.

## Tests

1. **Working version comparison**: Identical crash pattern → **pre-existing issue**
2. **Native tests**: All pass (64-bit binary) → issue is specific to 32-bit
3. **64-bit my_wine with sdl2_window**: Not tested (samples are 32-bit PE only)

## Finding

**This is a pre-existing bug in the working version.** Not introduced by the current branch changes.

The `ucontext=0x0000006b` value is identical in both working and current. The `signal_ret` address differs slightly (0xf467fbc2 vs 0xf464bbc2) due to different binary layouts, but the pattern is the same.

## Root Cause

**Unknown (pre-existing issue).** The signal handler receives a garbage ucontext pointer. Possible causes:
- Signal stack at 0x00800000 conflicts with SDL2 memory allocations
- 32-bit signal frame layout incompatibility with this glibc/kernel version
- SDL2 initializes something that corrupts the signal stack area

This is a separate issue from INV-002 (DOOM95 crash). Interestingly, DOOM95 in the working version reaches the rendering loop despite this crash path existing, suggesting DOOM95 takes a different code path that avoids triggering it.

## Verification

Running `refs/working/my_wine32 samples/sdl2_window_32/*.exe` produces identical output.

## Notes

While this blocks SDL2 sample testing, it does NOT block DOOM95 in the working version. The difference between working DOOM95 (passes) and current DOOM95 (INV-002) is a separate regression.
