/*
 * kernel32_doom95.c
 *
 * Compatibility-heavy kernel32 behavior used primarily by Doom95 and related
 * sample flows. This file still mixes generic path/process helpers with
 * Doom95-specific policy and is a planned split target during cleanup.
 */

/*
 * kernel32_doom95.c is intentionally empty for now.
 *
 * The generic helper clusters that previously lived here have been extracted
 * into narrower kernel32_* translation units. Keep this file as the named seam
 * for any future Doom95-only kernel32 compatibility that must remain isolated
 * from the generic runtime.
 */
