# Subplan 3: Windowing

## Status

Completed in the current tree.

## What Exists Now

- `include/user32_types.h`
- `src/msvcrt/user32_window.c`
- `src/msvcrt/user32_message.c`
- `src/msvcrt/user32_input.c`
- `src/msvcrt/user32_priv.h`
- `src/msvcrt/user32_weak_stub.c` for non-SDL builds
- `src/loader/import_table.c` entries for `user32.dll` and `user32.DLL`

Implemented coverage includes:

- class registration and window creation/destruction
- focus/activation bookkeeping
- message queueing, filtering, dispatch, and quit synthesis
- `SendMessageA`, `CallWindowProcA`, and default close handling
- cursor and keyboard helpers
- DC allocation/release path needed by basic painting stubs

## Verification

Primary coverage is now split across:

- `tests/test_user32_handle_ownership.c`
- `tests/test_user32_message_dispatch.c`
- graphical samples under `samples/sdl2_window*`

These cover class registration, `CreateWindowExA`, `WNDPROC` dispatch,
focus transitions, `WM_CLOSE` destruction paths, message filtering, and cursor/DC
handle ownership.

## Scope Change

This subplan is closed for the core window/message/input path. Remaining
`user32` work for Doom95 should be treated as integration work only:

- dialogs (`CreateDialogParamA`, friends)
- any additional control/menu helpers proven necessary by runtime traces
