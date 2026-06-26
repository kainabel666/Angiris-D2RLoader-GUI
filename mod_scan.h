// ═══════════════════════════════════════════════════════════════════════
//  mod_scan.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Scans <D2R>\mods\ for installed mods, parses each modinfo.json,
//  and owns the in-memory mod list (g_mods). Also runs a directory
//  watcher that fires MSG_MODS_DIRTY to the main window when the
//  folder changes (mod added/removed/renamed), so the UI can rescan.
//
//  Depends on:
//    core   — for AppDir, file I/O, JSON helpers, g_hwMain
//    config — for g_cfg.d2rPath (the parent folder to scan)
//    mod_types — for ModInfo
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"
#include "mod_types.h"

// Custom message posted to g_hwMain by the watcher thread when a
// change is detected under <D2R>\mods\. MainProc receives it and
// arms a debounce timer (IDT_MODS_DEBOUNCE) so a burst of file
// events collapses into a single rescan.
constexpr UINT MSG_MODS_DIRTY    = WM_USER + 11;

// Timer ID used by MainProc to debounce MSG_MODS_DIRTY bursts. The
// timer fires ~250ms after the latest dirty notification; on tick,
// MainProc calls FindMods + Layout + invalidate. Lives in the
// mod_scan header (rather than in some generic ID file) because
// it's part of the mod-scan subsystem's "protocol" with the UI.
constexpr UINT IDT_MODS_DEBOUNCE = 9001;

// Live mod list. Populated by FindMods, mutated only on the UI
// thread. UI code reads through this directly; background threads
// must not touch it without marshalling onto the UI thread.
extern vector<ModInfo> g_mods;

// Index into g_mods of the currently selected mod (-1 = no
// selection / no mods installed).
extern int g_selMod;

// Rescan <D2R>\mods\ from disk and return the discovered mod list.
// The caller is responsible for assigning to g_mods and re-validating
// g_selMod against the new size (the selected mod may have been
// removed/renamed, the list may now be empty, etc.).
//
// Pattern at call sites:  g_mods = FindMods(g_cfg.d2rPath);
//
// Handles both layouts the launcher supports:
//   <D2R>\mods\<modName>\modinfo.json          (flat)
//   <D2R>\mods\<modName>\<sub>.mpq\modinfo.json (nested .mpq subfolder)
vector<ModInfo> FindMods(const wstring& d2rPath);

// Start watching <D2R>\mods\ for changes. No-op if already
// watching the same folder. Replaces any prior watcher if the
// folder has changed. Silent failure if g_cfg.d2rPath is empty
// or the folder doesn't exist.
void StartModsWatcher();

// Stop the background watcher thread and release its handles.
// Blocks up to 1.5s waiting for the thread to exit cleanly.
// Safe to call even if no watcher is active.
void StopModsWatcher();
