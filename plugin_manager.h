// ═══════════════════════════════════════════════════════════════════════
//  plugin_manager.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Plugin manager popup. Opens a modal-ish themed window listing every
//  .dll plugin under both the global plugins folder and the currently
//  selected mod's per-mod plugins folder, with checkboxes that toggle
//  the plugin's active/disabled state. Save commits the moves to disk.
//
//  Plugin locations on disk:
//    Global (G):  <d2rPath>\plugins\
//    Per-mod (M): <d2rPath>\mods\<ModName>\<ModName>.mpq\Plugins\
//
//  Each plugin folder has a sibling Disabled\ subfolder. Plugins in
//  the parent folder are active; plugins in Disabled\ are inactive.
//  The Disabled\ subfolder is created on demand when the user
//  disables their first plugin in a given location.
//
//  Public surface is one function. Everything else (window class,
//  paint, list state, file I/O, save logic) is private to
//  plugin_manager.cpp.
//
//  Depends on mod_types (ModInfo), config (d2rPath).
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"
#include "mod_types.h"

// Open the plugin manager popup for the given selected mod. The popup
// scans both the global plugins folder AND the mod's per-mod plugins
// folder (and each one's Disabled\ subfolder), then displays a single
// scrollable list with per-plugin checkboxes.
//
// `selectedMod` may be nullptr — in that case only global (G) plugins
// appear in the list. `d2rPath` is the resolved D2R install dir
// (g_cfg.d2rPath at the call site).
//
// The popup runs its own message loop until the user clicks Save or
// Cancel (or closes the window). Save commits any toggled plugins
// to disk by moving the .dll files between active and Disabled\
// folders. Cancel makes no disk changes.
//
// Modal with respect to `parent` — the parent is EnableWindow(FALSE)
// while the popup is open and re-enabled on close.
void ShowPluginManager(HWND parent,
                       const ModInfo* selectedMod,
                       const wstring& d2rPath);
