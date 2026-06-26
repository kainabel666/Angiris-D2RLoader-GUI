// ═══════════════════════════════════════════════════════════════════════
//  mod_config.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Persists the per-mod launch-flag state (g_modSettings) into each
//  mod's <D2R>\mods\<modName>\Launcher Files\launcher_mod_cfg.json.
//
//  Why per-mod? Each mod has its own preferred launch profile — one
//  needs -w, another needs -resetofflinemaps, etc. Switching the
//  selected mod loads that mod's saved state into g_modSettings;
//  switching back restores it.
//
//  Depends on:
//    core         — for ReadTextFile/WriteTextFile, JSON helpers
//    mod_types    — for the ModInfo argument type
//    launch_flags — for ModSettings + g_modSettings + EnforceLockedFlags
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"
#include "mod_types.h"

// Read this mod's launcher_mod_cfg.json into g_modSettings. If the
// file doesn't exist yet, g_modSettings is reset to ModSettings{}
// defaults. EnforceLockedFlags is called at the end so a stale
// config can't leave a locked flag off.
void LoadModSettings(const ModInfo& mod);

// Serialize g_modSettings back to launcher_mod_cfg.json under the
// mod's Launcher Files folder. Creates the Launcher Files
// subfolder lazily if it doesn't exist yet (first save for a new
// mod or for a mod the user has never tweaked).
void SaveModSettings(const ModInfo& mod);
