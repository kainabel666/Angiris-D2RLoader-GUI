// ═══════════════════════════════════════════════════════════════════════
//  config.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Persistent launcher configuration — stored in launcher_config.json
//  next to the exe. One LauncherCfg instance (g_cfg) holds the live
//  values; LoadCfg / SaveCfg sync it with disk.
//
//  Per-mod config (flags, seed) lives elsewhere — those are tied to
//  the ModInfo type which hasn't been extracted yet. Phase 3 will
//  pull mod_scan + per-mod cfg into mod_scan.h.
//
//  Depends on core for ReadTextFile/WriteTextFile/JSON helpers and
//  the shared g_dpiScale value.
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// What the launcher does after D2R has been confirmed running.
enum LaunchBehavior {
    LB_STAY     = 0,   // stay open, no monitoring
    LB_MINIMIZE = 1,   // minimize on launch, restore + focus on D2R exit
    LB_CLOSE    = 2,   // close launcher after D2R confirmed running
};

struct LauncherCfg {
    wstring d2rPath;
    wstring lastMod;
    wstring toolsDir;       // user's modding tools folder
    wstring toolExcel;      // resolved AFJ Sheet Editor Pro.exe
    wstring toolStrings;    // resolved Code.exe
    wstring toolSprite;     // resolved D2RModding-SpriteEdit-2.0.exe
    wstring toolModels;     // resolved models editor (.exe or shortcut)
    wstring toolTextures;   // resolved textures editor (.exe or shortcut)
    wstring toolParticles;  // resolved particles editor (.exe or shortcut)
    int launchBehavior = LB_MINIMIZE;

    // Mod-update behaviour. backupCount = how many old mod folders to
    // keep when updating (0 disables backups). backupSaves = whether to
    // back up the mod's save folder before applying an update; user is
    // asked once, the answer becomes the default. backupSavesPrompted
    // tracks whether we've already asked.
    int  backupCount         = 1;
    bool backupSaves         = true;
    bool backupSavesPrompted = false;

    // UI scale multiplier (independent of system DPI). Persisted as
    // "ui_scale" in launcher_config.json. The final scale factor used
    // by S()/SF()/U() is this value times the system DPI scale.
    double uiScale = 0.85;

    // Preferred display font face name. Persisted as "font_name".
    // Empty = use the default (Exocet Blizzard Medium for headings,
    // Cinzel for the logo, etc.). The toolbar font dropdown lists
    // every .ttf in assets/fonts/ and stores the user's choice here.
    wstring fontName;

    // Preferred text-color preset index (0..7 — see g_colorPresets[]).
    // Persisted as "font_color". -1 = default (existing Tok::Gold).
    int fontColorIdx = -1;

    // System DPI scale at the last time the config was written. If
    // the current DPI differs on load (user changed Windows scaling
    // between sessions), uiScale is reset to 1.0 — otherwise a setting
    // tuned for one DPI environment could leave the launcher unusable
    // on a different one.
    double lastDpiScale = 1.0;

    // Launcher self-update — "skipped" version. When the startup
    // update check finds a latest GitHub release whose tag matches
    // this string, the user-facing dialog is suppressed (the user
    // chose Skip Version on a previous prompt for this same release).
    // Cleared automatically when a newer version than the skipped
    // one becomes available, so a user who skipped v1.2 still gets
    // prompted for v1.3.
    wstring skippedLauncherVersion;
};

// Single shared instance. All UI code reads through this; LoadCfg
// fills it from disk at startup and SaveCfg writes back on change.
extern LauncherCfg g_cfg;

// Read launcher_config.json into g_cfg. Missing file → g_cfg keeps
// its default-initialized values. Out-of-range numeric fields are
// snapped to safe defaults (e.g. ui_scale to nearest preset).
void LoadCfg();

// Serialize g_cfg back to launcher_config.json. Writes the current
// g_dpiScale into the "last_dpi_scale" field for the between-session
// DPI-change detection on next load.
void SaveCfg();
