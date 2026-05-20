# DOOM95 Debug Log

## Scope

Compact resume state for `samples/unpacked/doom95/DOOM95.EXE`.
Keep only the facts needed for the next debugging session.

## Current status

- `make all` succeeds.
- `./scripts/samples.sh run dll_loader` passes.
- `./my_wine samples/unpacked/doom95/DOOM95.EXE` no longer short-circuits out of the launcher dialog path.
- Doom95 still does not reach gameplay.
- The active path is now:
  - `DOOM95.EXE`
  - loads `DOOMLNCH.DLL`
  - resolves `_Launcher@4`
  - creates the launcher top-level window
  - enters `CreateDialogParamA()` for launcher template `0x72`
  - runs the launcher dialog `WM_INITDIALOG` path
  - populates several launcher combo boxes and auxiliary controls
  - no longer crashes in `DOOMLNCH.DLL+0x8c0b`
  - now stalls later in the launcher path after deeper control traffic

## Important confirmed facts

- Early `159` failures were sandbox artifacts, not real PE32 runtime failures.
- PE32 loading, Watcom bootstrap seeding, and the main Doom95 import surface are working.
- Dynamic builtin DLL handling is working for the maintained stub set.
- `DOOMLNCH.DLL` needs real PE32 `DllMain(DLL_PROCESS_ATTACH)` execution.
- The 64-bit `dll_loader` regression came from applying that same attach logic to PE32+ DLLs.
- The launcher was previously hanging because our 32-bit `CRITICAL_SECTION` ABI was wrong.
- `DOOMCFG.DLL` had imports that existed in code but still resolved to `NULL` because the PE32 resolver matched by function name only and ignored DLL name.
- `CreateDialogParamA()` was incorrectly validating dialog templates against the main EXE resource table instead of the caller DLL's `hInstance`.
- Several internal calls into `stdcall` user32 stubs were declared without `KERNEL32_ABI`, causing 32-bit stack corruption once the launcher dialog path became live.
- `DPLAY.dll` ordinal `2` is used by `DOOMLNCH.DLL` during launcher init and must resolve to `DirectPlayEnumerateA`, not be left as thunk value `0x80000002`.
- Our first launcher combo-box crash was not guest-only logic; it depended on both missing combo-box messages and that unresolved DirectPlay ordinal import.

## Key fixes already landed

1. Launcher/runtime path

- `FindWindowA` now does real class/title matching.
- `GetFileAttributesA("")` now fails instead of treating the EXE directory as success.
- Builtin `LoadLibraryA` / `GetModuleHandleA` / `GetProcAddress` / `FreeLibraryA` support late-bound builtin DLL handles.
- `GetLastActivePopup()` and safer `MessageBoxA()` behavior were added for the launcher UI/error path.
- PE32 dynamic DLL loads now call guest `DllMain(DLL_PROCESS_ATTACH/DETACH)`.
- `WideCharToMultiByte()` and `MultiByteToWideChar()` gained minimal working behavior.
- `kernel32!IsTNT()` stub was added.
- `GetCPInfo()` guest layout was fixed.
- `STARTUPINFOA` layout was fixed.
- `CRITICAL_SECTION` layout was fixed for PE32 by using pointer-sized fields.

2. Loader/regression fixes

- PE32+ dynamic DLL loads no longer run DLL entrypoints; this restores `dll_loader` while keeping the PE32 Doom95 launcher path.
- PE32 import resolution now matches both DLL name and function name.

3. Extra `DOOMCFG.DLL` surface

- Added/registered minimal stubs for:
  - `USER32`: `GetDlgCtrlID`, `MapVirtualKeyA`, `IsWindowEnabled`,
    `SendDlgItemMessageA`, `GetWindowTextA`,
    `CreateDialogIndirectParamA`, `GetDialogBaseUnits`, `CopyRect`
  - `ADVAPI32`: `RegDeleteKeyA`, `RegEnumKeyExA`

4. Modal dialog behavior

- `DialogBoxParamA()` no longer hardcodes a synthetic success return.
- `EndDialog()` now preserves the guest-provided modal result.
- Dialog creation tracks per-dialog modal state so guest `EndDialog()` calls can
  flow back to the caller.
- `CreateDialogParamA()` now resolves dialog templates against the caller module
  handle instead of always probing the main EXE resource table.
- Internal launcher/dialog helper calls now use `KERNEL32_ABI` where they call
  `stdcall` user32 exports directly from host C code.
- `dll_loader` still passes after this change.

5. Launcher control / DirectPlay path

- `SendMessageA()` now reaches the real dialog-control handler instead of a
  same-TU weak fallback in `user32_message.c`.
- Synthetic dialog controls now expose `GWL_ID` / `GWL_HWNDPARENT` through
  `GetWindowLongA()` / `GetWindowLongPtrA()`.
- Added working combo-box behavior for:
  - `CB_INSERTSTRING (0x14a)`
  - `CB_FINDSTRING (0x14c)`
  - `CB_SELECTSTRING (0x14d)`
  - clearer `CB_SETCURSEL` / `CB_GETITEMDATA` logging and selection handling
- Added minimal stateful handling for the later launcher controls:
  - list-box traffic: `LB_ADDSTRING (0x180)`, `LB_INSERTSTRING (0x181)`,
    `LB_RESETCONTENT (0x184)`, `LB_SETCURSEL (0x186)`,
    `LB_GETCURSEL (0x188)`, `LB_GETTEXT (0x189)`,
    `LB_GETTEXTLEN (0x18a)`, `LB_GETCOUNT (0x18b)`,
    `LB_SELECTSTRING (0x18c)`
  - up-down/spin traffic: `UDM_SETRANGE (0x465)`,
    `UDM_SETPOS (0x467)`, `UDM_GETPOS (0x468)`,
    `UDM_SETBUDDY (0x469)`
- Added `dplay.dll!DirectPlayEnumerateA` and mapped ordinal `2` to it in the
  ordinal resolver and builtin import table.
- Minimal DirectPlay provider enumeration now feeds launcher combo `0x3ed`
  with service-provider GUID/name pairs so the launcher no longer jumps to the
  unresolved thunk address `0x80000002`.

## Current blocker

The old `DOOMLNCH.DLL+0x8c0b` crash is gone. The launcher now gets far enough
to populate:

- service-provider combo `0x3ed`
- skill combo `0x3fc`
- episode/map combo `0x406`

The new live issue is a later launcher stall after additional control traffic.
The latest traced messages that matter here are now identified as:

- control `0x403`: list-box `LB_GETCURSEL`, `LB_GETTEXT`
- control `0x430`: repeated list-box `LB_ADDSTRING`
- control `0x3fa`: up-down `UDM_SETRANGE`, `UDM_SETPOS`, `UDM_SETBUDDY`

Those controls no longer decode as unknown messages, and the launcher survives
through that setup path in bounded reruns. The remaining problem is that it
still does not proceed into gameplay or a confirmed `DOOMCFG.DLL!DoomCfgEditor`
handoff.

## Best next step

Keep the focus on the first live transition after launcher child-control setup
before returning to deeper `DOOMCFG.DLL` work.

Inspect the next live controls around:

- the launcher dialog proc at `DOOMLNCH.DLL+0x5c50`
- the service-provider / game-setup helper region around `DOOMLNCH.DLL+0x83e0`
- the later `WM_INITDIALOG` control traffic for ids `0x403`, `0x430`, and `0x3fa`

Specifically determine:

- the first meaningful launcher action after the now-handled `0x403` / `0x430`
  / `0x3fa` setup traffic
- whether another control id starts receiving generic-zero responses after this
  point
- whether the launcher is waiting on message-loop behavior, async window state,
  or a `WM_COMMAND`/notification path we still do not synthesize
- whether the next successful transition is into `DOOMCFG.DLL!DoomCfgEditor`
  or directly into game startup

After the launcher control path is stable, return to:

- `DOOMCFG.DLL!DoomCfgEditor`
- the dialog proc at `DOOMCFG.DLL+0x13d0`
- the custom control messages already identified there (`0x1307`, `0x1328`, `0x4e`)

## Most relevant files

- [src/msvcrt/kernel32_sync.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/kernel32_sync.c)
- [src/loader/dll_loader.c](/home/arzad/Playground/projects/my_wine/src/loader/dll_loader.c)
- [src/loader/import_resolve.c](/home/arzad/Playground/projects/my_wine/src/loader/import_resolve.c)
- [src/loader/import_table.c](/home/arzad/Playground/projects/my_wine/src/loader/import_table.c)
- [src/msvcrt/launcher_ui_stubs.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/launcher_ui_stubs.c)
- [src/msvcrt/user32_dialog.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/user32_dialog.c)
- [src/msvcrt/advapi32_registry.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/advapi32_registry.c)
