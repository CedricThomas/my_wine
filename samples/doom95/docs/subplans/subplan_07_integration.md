# Subplan 7: Final Integration

## Status

Started. The launcher path is now the active blocker: Doom95 gets through
Watcom/bootstrap setup, loads `DOOMLNCH.DLL`, and advances past the earlier
fake-early-exit bugs, but still crashes later in the launcher/runtime UI path.

## Goal

Use real Doom95 runs to determine the final missing pieces after DDraw,
DSound, and system import coverage are in place.

This remains a runtime-driven integration phase, but parsing `DOOM95.EXE`
confirms that both dialog-oriented `user32` APIs and `gdi32` rendering APIs
are real requirements, not guesses.

## Required Sequence

### 7.1 First runnable Doom95 attempt
- [x] unpack Doom95
- [ ] remove or revise `samples/doom95/sample.info` once the sample is runnable beyond the current `skip=true` state
- [x] launch `./my_wine samples/unpacked/doom95/DOOM95.EXE`
- [x] capture the first concrete failing import or runtime crash

Current first concrete failure:

- the original early Watcom crash is no longer the current blocker
- the current real-run failure is later:
  - `DOOMLNCH.DLL` loads
  - `_Launcher@4` resolves and runs
  - the launcher performs late-bound `user32.dll` symbol lookups
  - a runtime/UI error path is reached
  - execution then crashes with `SIGSEGV` at `EIP=0x00000006`

Key fixes already applied during this integration pass:

- `FindWindowA()` now performs a real class/title lookup instead of always
  returning the active window
- `GetFileAttributesA("")` now fails correctly instead of falsely succeeding on
  the EXE directory
- builtin stub DLLs such as `user32.dll` now support late-bound:
  - `LoadLibraryA`
  - `GetModuleHandleA`
  - `GetProcAddress`
  - `FreeLibraryA`
- `GetLastActivePopup()` is now exposed for the launcher's dynamic `user32`
  lookup path

### 7.2 Add only the missing user32/GDI surface
- [x] implement confirmed dialog imports:
  - `CreateDialogParamA`
  - `IsDialogMessageA`
  - `GetDlgItem`
  - `CheckDlgButton`
  - `IsDlgButtonChecked`
  - `SetDlgItemTextA`
  - `LoadStringA`
  - `MessageBoxA`
- [x] implement confirmed `gdi32` imports:
  - `CreateDCA`
  - `CreateDIBitmap`
  - `CreateFontA`
  - `CreatePalette`
  - `DeleteDC`
  - `DeleteObject`
  - `GetDeviceCaps`
  - `GetObjectA`
  - `GetStockObject`
  - `GetSystemPaletteEntries`
  - `RealizePalette`
  - `SelectPalette`
  - `SetBkColor`
  - `SetTextColor`
  - `StretchDIBits`
  - `UnrealizeObject`

These are still minimal shims. They are sufficient for import exposure and
basic runtime probing, but they are not yet a claim that Doom95 dialogs or GDI
fallback rendering are semantically complete.

### 7.3 Promote to maintained sample only when justified
- [ ] update `sample.info` from unsupported/skipped only after startup is stable enough to make automated runs meaningful
- [ ] add the smallest useful harness or smoke test rather than pretending to have full game coverage

## Current Focus

- stay on the real launcher path rather than bypassing `_Launcher@4`
- inspect the call chain after the current `MessageBoxA`/runtime-error path
- determine whether the next required fix is:
  - another missing late-bound `user32` semantic, or
  - a launcher-side state/setup behavior that should prevent the runtime error
    path from being taken at all

## Parsed EXE Notes

- `DOOM95.EXE` is a 32-bit GUI PE with entry point `0x004444d8`
- file size is `775117` bytes
- the resource directory is present and `objdump` reports dialog resources with
  IDs `104`, `130`, and `131`, which matches the older dialog notes
  
