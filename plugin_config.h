// ═══════════════════════════════════════════════════════════════════════
//  plugin_config.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Optional per-mod manifest declaring which plugin DLLs a mod expects
//  to ship with. Lives at:
//
//      <d2rPath>\mods\<ModName>\plugin_config.json
//
//  When the file is absent, plugin manager behavior is unchanged from
//  the v1.2 baseline — globals + per-mod plugins are listed together
//  with toggle checkboxes.
//
//  When the file is present, the Plugins window enters "manifest mode":
//    • All current global plugins are physically moved to the global
//      Disabled\ folder before the window opens (so D2RLoader can't
//      load them at next launch even if the user crashes mid-session).
//    • The window shows only the per-mod plugin list, header-grouped,
//      with no toggles — it's an inventory view, not an editor.
//    • Any manifest entry not currently in the mod's plugins folder is
//      searched for in (global enabled / global disabled / mod disabled),
//      moved back into the mod's plugins folder if found, or greyed out
//      in the UI if still missing.
//
//  Schema (v1, intentionally minimal — extend later without breakage):
//
//      {
//        "plugins": [
//          "d2lod.dll",
//          "d2hd.dll",
//          "PD2.dll"
//        ]
//      }
//
//  Filenames are treated case-insensitively when comparing against the
//  filesystem (Windows is case-preserving but case-insensitive). Empty
//  strings, non-.dll entries, and duplicate entries are dropped silently
//  during parse so an authoring slip doesn't break the launcher.
//
//  Depends on angiris_common (wstring, vector).
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// Parsed manifest for one mod. `present` distinguishes "no manifest file
// on disk" (false → run the legacy plugin manager flow) from "manifest
// file exists but lists zero plugins" (true with empty `plugins` →
// manifest mode kicks in, but the mod section will be empty).
struct PluginConfig {
    bool             present = false;
    vector<wstring>  plugins;          // dll filenames, in manifest order
    wstring          author;           // optional "author" field (v1.6);
                                       // empty if not provided
};

// Load the manifest for a given mod directory. `modDir` is the absolute
// path to <d2rPath>\mods\<ModName>\ (with or without a trailing slash).
//
// Returns a PluginConfig with present=false if:
//   • the file doesn't exist
//   • the file exists but is empty / malformed / has no "plugins" key
//   • the "plugins" key exists but isn't an array of strings
//
// Returns present=true with a populated plugins vector if parse succeeds.
// Never throws; logs nothing — silent fallback to legacy behavior is
// the design intent so a broken manifest never bricks the launcher.
PluginConfig LoadPluginConfig(const wstring& modDir);


// ─────────────────────────────────────────────────────────────────────
//  Recovery sweep
// ─────────────────────────────────────────────────────────────────────
//
// For each filename in `manifestPlugins`, ensure a copy lives in the
// mod's active folder. Each entry may be a plugin (.dll) or a patch
// (.json) — the sweep routes each file by extension to the appropriate
// active folder within <modD2rLoaderDir>. Search order when an entry
// is not already in the active folder:
//   1. <d2rPath>\d2rloader\<kind>              (global enabled)     — COPY
//   2. <d2rPath>\d2rloader\<kind>\Disabled     (global disabled)    — COPY
//   3. <modD2rLoaderDir>\<kind>\Disabled       (mod's own disabled) — MOVE
// where <kind> is "plugins" for .dll or "patches" for .json.
//
// First match wins. Globals are sourced via COPY so other mods that
// also depend on the same file still find it after the sweep — the
// global folders are shared property. Mod-disabled is the mod's own
// state, so it's MOVED into active.
//
// On all paths the destination directory is created if necessary.
//
// Returns a parallel `vector<bool>` of the same length as
// `manifestPlugins`: out[i]==true iff manifestPlugins[i] is present in
// its destination folder after the sweep completes (whether it was
// already there, was just copied/moved in, or — false — couldn't be
// located anywhere).
//
// `modD2rLoaderDir` is the mod's d2rloader base folder — typically
// <d2rPath>\mods\<ModName>\d2rloader. The helper derives its
// plugins\ and patches\ subfolders + their Disabled\ siblings, as
// well as the corresponding global folders under <d2rPath>\d2rloader\.
//
// Best-effort: a failed copy/move is treated as "not found" so the UI
// greys out the entry; the source file is left untouched and the user
// can retry by reopening the Plugins window.
vector<bool> RunPluginRecoverySweep(const wstring& modD2rLoaderDir,
                                    const wstring& d2rPath,
                                    const vector<wstring>& manifestPlugins);


// ─────────────────────────────────────────────────────────────────────
//  Globals-to-disabled sweep
// ─────────────────────────────────────────────────────────────────────
//
// Physically move every plugin/patch currently in
// <d2rPath>\d2rloader\plugins\  and  <d2rPath>\d2rloader\patches
// into their respective Disabled\ subfolders. Called in two places:
//
//   1. When the Plugins window opens for a mod that has a manifest.
//      Ensures the inventory view's "no globals" promise is true on
//      disk, not just in the UI.
//
//   2. When PLAY is pressed for a mod that has a manifest. Closes the
//      loophole where the user could open Plugins on Mod A, switch to
//      Mod B without manifest, re-enable globals, then switch back to
//      Mod A and launch — D2RLoader would otherwise scan the globals
//      folders at launch and load them alongside Mod A's plugins.
//
// Globals are NOT restored on Plugins-window close (per spec: this is a
// one-way operation, documented in the README so users understand they
// need to re-enable manually if they switch to a non-manifest mod).
//
// Non-matching files in the plugins/patches folders (e.g. a .dll in
// patches\, or a .txt in plugins\) are left untouched. Subfolders
// (including the Disabled\ folders themselves) are not recursed into.
// Creates the Disabled\ folders if they don't yet exist.
//
// Best-effort + silent: a failed move (file locked, permission denied)
// leaves the file in place. The next Plugins-window scan will surface
// the residual state; this function never raises a dialog.
void MoveGlobalPluginsToDisabled(const wstring& d2rPath);
