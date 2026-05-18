# Subplan 7: Final Integration

## Status

Not started, and intentionally moved to the end.

## Goal

Use real Doom95 runs to determine the final missing pieces after DDraw,
DSound, and system import coverage are in place.

This remains a runtime-driven integration phase, but parsing `DOOM95.EXE`
confirms that both dialog-oriented `user32` APIs and `gdi32` rendering APIs
are real requirements, not guesses.

## Required Sequence

### 7.1 First runnable Doom95 attempt
- [ ] unpack Doom95
- [ ] remove or revise `samples/doom95/sample.info` once the sample is runnable beyond the current `skip=true` state
- [ ] launch `./my_wine samples/unpacked/doom95/DOOM95.EXE`
- [ ] capture the first concrete failing import or runtime crash

### 7.2 Add only the missing user32/GDI surface
- [ ] implement confirmed dialog imports:
  - `CreateDialogParamA`
  - `IsDialogMessageA`
  - `GetDlgItem`
  - `CheckDlgButton`
  - `IsDlgButtonChecked`
  - `SetDlgItemTextA`
  - `LoadStringA`
  - `MessageBoxA`
- [ ] implement confirmed `gdi32` imports:
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

### 7.3 Promote to maintained sample only when justified
- [ ] update `sample.info` from unsupported/skipped only after startup is stable enough to make automated runs meaningful
- [ ] add the smallest useful harness or smoke test rather than pretending to have full game coverage

## Parsed EXE Notes

- `DOOM95.EXE` is a 32-bit GUI PE with entry point `0x004444d8`
- file size is `775117` bytes
- the resource directory is present and `objdump` reports dialog resources with
  IDs `104`, `130`, and `131`, which matches the older dialog notes
  