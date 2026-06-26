// ═══════════════════════════════════════════════════════════════════════
//  mod_types.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Core data type for a single installed mod. Header-only: no .cpp
//  partner is needed because the struct is a plain POD-with-strings.
//
//  Several modules need to see ModInfo — mod_scan builds the list,
//  launch_flags reads the selected mod's folder to build CLI args,
//  the paint/UI code in Angiris.cpp renders each row, etc. Splitting
//  this struct into its own header avoids those modules transitively
//  pulling in mod_scan.h just to get the type definition.
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

struct ModInfo {
    wstring name;          // display name (from modinfo "name" / "savepath" / folder)
    wstring folder;        // folder name under <D2R>\mods\, used as -mod arg
    wstring dir;           // full path to the mod's root directory
    wstring launcherDir;   // <mod>\Launcher Files
    wstring savePath;      // modinfo "savepath" — folder name under
                           //   %USERPROFILE%\Saved Games\Diablo II
                           //   Resurrected\Mods\ where D2R puts this
                           //   mod's character + stash data. Required
                           //   by Blizzard's modinfo.json schema, so
                           //   always populated. Used by the Backup
                           //   Saves feature.

    // Optional modinfo.json fields
    wstring title;         // overrides "name" as the display label
    wstring description;   // short tagline shown on the mod row
    wstring overview;      // longer description (unused for now)
    wstring version;       // shown in hero meta line
    wstring author;        // shown in hero meta line
    wstring bannerPath;    // resolved banner file path; empty if absent
    wstring docsUrl;       // shows the Documents button when non-empty
    wstring websiteUrl;    // shows the Website button when non-empty
    wstring discordUrl;    // shows the Discord button when non-empty

    // Update-check opt-in. Either field enables checks for this mod.
    // update_github wins if both are set.
    wstring updateGithub;     // "owner/repo" — GitHub releases shortcut
    wstring updateManifest;   // explicit URL to a JSON manifest
};
