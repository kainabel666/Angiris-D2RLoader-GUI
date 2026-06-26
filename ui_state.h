// ═══════════════════════════════════════════════════════════════════════
//  ui_state.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Extern declarations for the file-local UI state in Angiris.cpp that
//  paint_main.cpp needs to read. Phase 7c introduced this header as a
//  pragmatic catch-all so the paint extraction could ship without
//  reorganizing 18 globals across 5+ existing modules.
//
//  Categories:
//    Rects computed by Layout, painted by paint code:
//      g_loaderDirRect, g_stashDropdownRect,
//      g_colorDropdownRect, g_fontDropdownRect, g_scaleDropdownRect,
//      g_scaleSliderRect, g_onLaunchHeaderRect,
//      g_onLaunchRect, g_onLaunchSliderRect, g_versionLabelRect
//
//    Seed input virtual-focus state (custom input, not Win32 EDIT):
//      g_seedInputFocused, g_seedCaretPos, g_seedCaretVisible
//      g_seedLabelLogicalW (measured at font-load, drives combo X)
//
//    Toolbar pressed state (g_tbPressed):
//      Tracks which title-bar button is currently held down so paint
//      can render the pressed state independent of the OS theme.
//
//    LoaderOpts struct (read-only after LoadLoaderOpts at startup):
//      g_loaderOpts (current loader.ini values for Stash/Damage)
//
//    Font enumeration caches (filled at startup, read by font picker):
//      g_availableFonts, g_availableAbbrevs
//
//  Phase 7d/7e are likely to reshuffle these — for example moving
//  the seed-input state into a dedicated input module, or the rect
//  globals into a paint-state struct. Until then, this header is
//  the single point of access.
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// LoaderOpts mirrors D2RLoader.ini's [Stash]/[Advanced.Logging] values.
// Definition lives here so paint code can read g_loaderOpts.extraSharedTabs
// for the Stash Tabs dropdown without dragging in the larger Angiris.cpp
// internals. Phase 7d/7e may relocate this to its own module.
struct LoaderOpts {
    int extraSharedTabs  = 0;     // [Stash] extra_shared_tabs
    int damageIndicator  = 2;     // [Advanced.Logging] damage_indicator
};

// ── Layout-computed rects (set by Layout, read by paint code) ────────
extern RECT g_loaderDirRect;
extern RECT g_stashDropdownRect;
extern RECT g_colorDropdownRect;
extern RECT g_fontDropdownRect;
extern RECT g_scaleDropdownRect;
extern RECT g_scaleSliderRect;
extern RECT g_onLaunchHeaderRect;
extern RECT g_onLaunchRect;
extern RECT g_onLaunchSliderRect;
extern RECT g_versionLabelRect;

// ── Seed input virtual-focus state ───────────────────────────────────
extern bool g_seedInputFocused;
extern int  g_seedCaretPos;
extern bool g_seedCaretVisible;
extern int  g_seedLabelLogicalW;

// ── Toolbar pressed state ────────────────────────────────────────────
extern int  g_tbPressed;

// ── Loader options snapshot (D2RLoader.ini) ──────────────────────────
extern LoaderOpts g_loaderOpts;

// LoaderOptHits bundles the per-row rects for the Loader Options
// section in the left rail. ComputeLoaderOptRects returns the
// current frame's values (Layout writes the individual rect globals,
// this helper aggregates them). Used by both PaintLeftRail and
// hit-test code in MainProc.
struct LoaderOptHits {
    RECT stash;       // "Stash Tabs" row
};
LoaderOptHits ComputeLoaderOptRects();

// ── Seed input helpers (selection / caret model) ─────────────────────
// The seed input is custom-painted; selection state lives behind
// these accessors so paint and hit-test code don't have to know about
// the underlying g_seedSelStart / g_seedCaretPos coupling.
int  SeedSelLo();
int  SeedSelHi();
bool SeedHasSelection();

// ── ON LAUNCH slider helpers ─────────────────────────────────────────
// The 3-state launch-behavior slider's current state + display label.
// Reads g_cfg.launchBehavior internally; exposed so PaintBody can
// render the current state without reaching into config directly.
int               OnLaunchSliderState();
const wchar_t*    OnLaunchStateLabel();

// ── Font enumeration caches ──────────────────────────────────────────
extern std::vector<std::wstring> g_availableFonts;
extern std::vector<std::wstring> g_availableAbbrevs;

// ── Child window HWNDs (created in CreateControls, positioned by Layout) ───
// All exposed so layout.cpp can position them and so paint code / event
// handlers can reach them across translation units. g_hwMain lives in
// core.h since it's the launcher's top-level window.

// Mod list custom control + its data buttons
extern HWND g_hwList;             // mod list (custom-painted)
extern HWND g_hwLaunch;           // PLAY button (in right column)

// Per-mod link buttons in the MOD DESCRIPTION panel
extern HWND g_hwModDiscord;
extern HWND g_hwModDocs;
extern HWND g_hwModWebsite;

// Left-rail navigation buttons (6 — Mods, Options, Logs, Help, About, Exit)
extern HWND g_hwNavMods;
extern HWND g_hwNavOptions;
extern HWND g_hwNavLogs;
extern HWND g_hwNavHelp;
extern HWND g_hwNavAbout;
extern HWND g_hwNavExit;

// Loader Options buttons (path picker + Plugins)
extern HWND g_hwLoaderDirBtn;     // "..." button beside the loader path bar
extern HWND g_hwLoaderPlugins;    // "Plugins" button — opens plugin manager

// Mod list area buttons
extern HWND g_hwRefresh;          // top-right "Refresh"
extern HWND g_hwBrowseMods;       // bottom-left "Browse Mods"
extern HWND g_hwUpdateMod;        // bottom-right "Update Selected"

// Bottom expansion panel
extern HWND g_hwExpandToggle;     // arrow button
extern HWND g_hwBottomTools[6];   // 6 tool launchers
extern HWND g_hwBottomRefs[3];    // 3 references
extern HWND g_hwBottomDls[3];     // 3 download links
