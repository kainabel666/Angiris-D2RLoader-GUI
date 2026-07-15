// ═══════════════════════════════════════════════════════════════════════
//  plugin_manifest.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Launcher-wide map of plugin DLL filenames → human-readable display
//  names. Lives beside Angiris.exe so a curated default ships with the
//  launcher and per-user renames persist across sessions.
//
//      <launcher folder>\plugin_manifest.json
//
//  Schema (intentionally minimal, extend without breakage):
//
//      {
//        "names": {
//          "d2lod.dll": "D2 LoD Helper",
//          "PD2.dll":   "Project Diablo 2 Core",
//          "d2hd.dll":  "HD Texture Pack"
//        }
//      }
//
//  Lookup is case-insensitive on the DLL filename (Windows treats
//  "PD2.dll" and "pd2.dll" as the same file). The friendly name
//  preserves whatever the author/user typed.
//
//  Same DLL filename always renders the same friendly name across all
//  contexts — global section, mod-local section, manifest mode — so
//  identity is the filename, not the location.
//
//  This module provides Load + lookup + Set + Save. GetPluginFriendlyName
//  is read by the Plugins window during paint to show "Friendly (dll)"
//  rows; the right-click rename modal calls SetPluginFriendlyName +
//  SavePluginManifest to commit user edits.
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// Parse plugin_manifest.json from the launcher folder into the in-memory
// map. Called once at wWinMain startup. Silent on failure — a missing
// or malformed file just means "no friendly names yet" and every plugin
// row falls back to its DLL filename.
//
// Idempotent: re-calling clears the existing map and re-reads from disk.
// Phase E2 will call this again after a successful rename save to keep
// memory + disk in sync.
void LoadPluginManifest();

// Returns the friendly display name for `dllName`, or an empty string
// if no entry exists / the entry maps to an empty string. Lookup is
// case-insensitive on the DLL name.
//
// The caller decides how to render: typical use is
//
//     wstring friendly = GetPluginFriendlyName(dllName);
//     wstring row = friendly.empty()
//                       ? dllName
//                       : friendly + L" (" + dllName + L")";
//
// Returns an empty wstring (not a wstring*) so callers can chain
// .empty() / .c_str() without null-pointer handling.
wstring GetPluginFriendlyName(const wstring& dllName);


// Update or insert the friendly name for a DLL. Pass an empty
// friendlyName to clear the rename — the entry is kept in the map
// (and persisted by SavePluginManifest) so discovery doesn't have
// to re-add it later; lookup just returns "" which falls back to
// the bare DLL filename for display.
//
// Casing of `dllName` matters only for new inserts (case-insensitive
// matching reuses the existing key's casing if one is already there).
// Caller is expected to call SavePluginManifest afterward so the
// on-disk file matches the in-memory map.
void SetPluginFriendlyName(const wstring& dllName, const wstring& friendlyName);


// For each filename in `dllNames`, if no entry currently exists in the
// in-memory map (case-insensitive), add a new entry with an EMPTY
// friendly name. Existing entries are never touched — neither their
// stored casing nor their friendly name.
//
// Returns true if the map was modified (i.e., at least one new entry
// was added). The caller can use this to decide whether SavePluginManifest
// needs to be written back to disk.
//
// Intent: when the user clicks the Plugins button, the launcher discovers
// every DLL the user could possibly want to rename. Adding empty entries
// lets the user just open plugin_manifest.json in a text editor and fill
// in friendly names beside pre-populated keys — no need to look up DLL
// filenames first.
bool EnsureManifestEntries(const vector<wstring>& dllNames);

// Serialize the in-memory map back to <launcher folder>\plugin_manifest.json.
// Pretty-printed for hand-edit friendliness: two-space indent, one entry
// per line, sorted alphabetically (case-insensitive) so the file stays
// stable across saves.
//
// Silent on failure (disk full, permission denied) — the in-memory map
// is still correct, only the on-disk representation lags.
void SavePluginManifest();
