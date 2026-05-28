# Enable Doom95 Launcher Dialog At Startup

## Goal

Show the Doom95 launcher dialog at startup without auto-skipping it, and keep
it alive long enough for real user interaction.

## Current State

- `CreateDialogParamA()` creates the dialog window and calls `WM_INITDIALOG`.
- Immediately after init, `user32_dialog_try_doom95_autostart()` force-selects
  provider/WAD/map state and programmatically clicks Start.
- `DialogBoxParamA()` does not run a real modal loop.
- `user32_dialog_run_modal()` currently forces completion by sending `IDOK`,
  then `WM_CLOSE`, then `EndDialog()`.
- Basic dialog/control bookkeeping already exists for text fields, combo boxes,
  list boxes, item data, and a subset of `IsDialogMessageA()` routing.

## First Usable Pass

### 1. Gate or disable Doom95 autostart

Target:

- `src/msvcrt/user32_dialog.c`
- `src/msvcrt/user32_dialog_doom95.c`

Change:

- Stop calling `user32_dialog_try_doom95_autostart()` unconditionally after
  `WM_INITDIALOG`.
- Prefer a narrow runtime gate over deleting the helper outright, so the
  current automated startup path can still be restored if needed for smoke
  testing.

Expected outcome:

- The launcher dialog appears and remains under guest control instead of being
  advanced directly into the game.

### 2. Replace fake modal completion with a real modal loop

Target:

- `src/msvcrt/user32_dialog.c`
- possibly `src/msvcrt/user32_message*.c` if message-pump gaps show up

Change:

- Rework `user32_dialog_run_modal()` so it pumps messages until
  `user32_dialog_end()` marks the modal as ended.
- Do not synthesize `IDOK`, `WM_CLOSE`, or forced `EndDialog()` except as a
  shutdown fallback.
- Keep ownership local to USER32 dialog code; do not mix this with loader or
  Doom95-specific logic.

Expected outcome:

- `DialogBoxParamA()` behaves like an actual blocking modal entrypoint instead
  of a one-shot compatibility stub.

### 3. Validate basic mouse-driven launcher usability

Target behavior:

- Combobox selection changes should reach the guest dialog proc.
- WAD list updates should still populate correctly.
- Start button click should enter the same launch path currently exercised by
  autostart.

Likely checks:

- Provider dropdown can be changed.
- WAD dropdown is populated and selection sticks.
- Start button dispatches its normal `WM_COMMAND`.

## Likely Follow-On Gaps

These may not all be required for a first usable pass, but they are the areas
most likely to break once the launcher remains interactive:

1. Keyboard dialog navigation

- `Tab` / shift-tab focus traversal
- default button behavior for Enter
- Escape / cancel behavior

2. Focus and activation semantics

- initial focused control
- focus changes between parent dialog and child controls
- activation edge cases when SDL window focus changes

3. Dialog-style notifications

- control notification codes beyond the currently exercised path
- combo/listbox selection-change notifications
- command routing details for child handles vs dialog handles

4. Message loop integration

- whether the existing USER32 loop naturally services modal dialogs
- whether `IsDialogMessageA()` is sufficient as implemented
- whether any host-side wait/poll behavior prevents responsive interaction

## Suggested Implementation Order

1. Add an explicit gate around Doom95 autostart.
2. Implement a real modal pump in `user32_dialog_run_modal()`.
3. Run the existing verification loop to ensure non-launcher behavior remains
   stable.
4. Manually launch Doom95 and confirm the launcher stays visible instead of
   auto-entering the game.
5. Fix only the first interaction gaps required to click through the launcher.

## Verification

Keep the existing regression loop:

- `make run-tests`
- `make run-samples-scenarios`
- `env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 timeout 5 ./my_wine32 ./samples/unpacked/doom95/DOOM95.EXE`

Additional manual verification for this feature:

- Launch Doom95 without suppressing the dialog.
- Confirm the launcher window stays open after startup.
- Confirm the user can select a provider/WAD and click Start.
- Confirm the game still reaches WAD discovery/startup after manual launch.

## Risk Assessment

Low risk:

- Gating the explicit autostart helper.

Medium risk:

- Replacing the fake modal completion path with a real loop.

Higher risk:

- Full Win32-like dialog semantics such as keyboard traversal, default buttons,
  and nuanced focus behavior.

## Definition Of Done For First Pass

- Doom95 launcher dialog is shown at startup.
- It is not auto-skipped.
- `DialogBoxParamA()` stays alive until the guest ends the dialog.
- Mouse interaction is sufficient to start the game manually.
- Existing automated verification still passes, or the Doom95 smoke is updated
  in a deliberate and documented way if dialog interactivity changes the
  expected startup behavior.
