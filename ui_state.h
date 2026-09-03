// ═══════════════════════════════════════════════════════════════════════
//  ui_state.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Extern declarations for the file-local UI state in Angiris.cpp that
//  paint_main.cpp needs to read.
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
//      g_loaderOpts (current loader.ini values for Stash/Damage/Sockets)
//
//    Font enumeration caches (filled at startup, read by font picker):
//      g_availableFonts, g_availableAbbrevs
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// LoaderOpts mirrors D2RLoader.toml values consumed by the launcher's
// Loader Options section. Definition lives here so paint code can read
// g_loaderOpts.* directly without dragging in Angiris.cpp internals.
struct LoaderOpts {
    // ── [d2rcore.game_rules] (new in D2RLoader 1.1.0) ────────────────
    // String enums, not bools: "3.1" or "3.2", selecting which ruleset
    // version the loader applies. Stored as the literal toml string so
    // an unrecognised hand-edited value round-trips untouched rather
    // than being silently rewritten to a default.
    wstring auraEnchantedSelection  = L"3.2";
    wstring bindDemonCurseSelection = L"3.2";

    // [d2rcore.items]
    bool showGroundSockets   = false;
    bool displayItemLevels   = false;
    bool itemStatRanges      = false;   // 1.1.0
    bool maximumSockets      = false;   // 1.1.0

    // [d2rcore.player]
    bool enableRespec        = false;
    // 1.1.0 — keeps the Toggle Renderer keybind live on RotW characters.
    bool alwaysEnableRotwLegacyKeybind = false;

    // [d2rcore.stash]
    // NOTE: 1.1.0 ships add_shared_tabs = 100 by default. The old 0-16
    // dropdown can't represent that; see the clamp in LoadLoaderOpts.
    int  addSharedTabs       = 0;     // vanilla=0
    int  setMaterialsLimit   = 99;    // vanilla=99, max=255

    // ── [d2rloader] ──────────────────────────────────────────────────
    // default_mod, skip_title_screen and launch_arguments are NOT
    // mirrored here on purpose. The launcher owns all three: as of
    // v1.6.2 the Play handler WRITES default_mod and launch_arguments
    // from the mod picker and flag grid (D2RLoader.toml is the launch
    // config now, not argv), and EnforceLoaderTomlOwnership() forces
    // skip_title_screen off every run. Exposing any of them here would
    // offer a control the launcher immediately overrides.
    bool showTcpipButton     = false;
    bool checkForUpdates     = true;    // 1.1.0 — D2RLoader's own check
    bool alwaysGenerateNewMaps = false; // 1.1.0
    wstring textLocale;                 // 1.1.0 — "" = game default
    wstring audioLocale;                // 1.1.0 — "" = game default

    // ── [d2rloader.backups] (new in 1.1.0) ───────────────────────────
    bool backupsEnabled      = true;
    int  retainedSessions    = 10;    // valid 1-100
    bool backupSharedStashes = true;

    // ── [d2rloader.advanced] (new in 1.1.0) ──────────────────────────
    // The two extension switches gate whether ANY plugin or patch loads.
    // With either off, installs made through the Plugin Manager or the
    // Repository Browser silently do nothing in-game.
    bool allowGlobalExtensions = true;
    bool allowModExtensions    = true;
    bool writeCrashDumps       = false;

    // [d2rloader.developer]
    bool enableConsole       = false;
    bool assertDialogMode    = false;

    // [d2rloader.developer.logs] — child toggles are inert unless
    // logsEnabled is true (D2RLoader treats logsEnabled as the master).
    bool logsEnabled         = false;
    bool logNativeBlizzard   = false;   // 1.1.0
    bool logJsonResources    = false;
    bool logWidgetPanels     = false;
    bool logExcelFiles       = false;
    bool logBinValidation    = false;   // 1.1.0
    bool logFonts            = false;
    bool logSprites          = false;
    bool logChatMessages     = false;
    bool logModels           = false;
    bool logExtensionDetails = false;   // 1.1.0
    bool logCharacterEnv     = false;   // 1.1.0
    bool logArchiveMounts    = false;   // 1.1.0
    bool logCascFetchDecisions = false; // 1.1.0
    bool logCascFiles        = false;   // 1.1.0
};

// ── Layout-computed rects (set by Layout, read by paint code) ────────
extern RECT g_loaderDirRect;
extern RECT g_stashDropdownRect;
extern RECT g_showSocketsRect;    // v1.3: 2-state toggle (false/true) — [Items] show_ground_sockets
extern RECT g_colorDropdownRect;
extern RECT g_fontDropdownRect;
extern RECT g_scaleDropdownRect;
extern RECT g_scaleSliderRect;
// Scale "SCALE" header, drawn above the value box in dropdown mode
// (stacked ON LAUNCH-style layout). Zero-area in cycle mode. Set in
// Layout(), read by paint_main.cpp.
extern RECT g_scaleHeaderRect;
extern RECT g_onLaunchHeaderRect;
extern RECT g_onLaunchRect;
extern RECT g_onLaunchSliderRect;
extern RECT g_versionLabelRect;
// Clickable "D2RLoader Update Available" gold text, positioned above the
// LOADER OPTIONS header. Zero-area when no update is available. Set in
// Layout(), consumed by paint + hit-test in Angiris.cpp.
extern RECT g_d2rloaderUpdateRect;

// ── Seed input virtual-focus state ───────────────────────────────────
extern bool g_seedInputFocused;
extern int  g_seedCaretPos;
extern bool g_seedCaretVisible;
extern int  g_seedLabelLogicalW;

// ── Toolbar pressed state ────────────────────────────────────────────
extern int  g_tbPressed;

// ── Loader options snapshot (D2RLoader.toml) ─────────────────────────
extern LoaderOpts g_loaderOpts;

// LoaderOptHits bundles the per-row rects for the Loader Options
// section in the left rail. ComputeLoaderOptRects returns the
// current frame's values (Layout writes the individual rect globals,
// this helper aggregates them). Used by both PaintLeftRail and
// hit-test code in MainProc.
struct LoaderOptHits {
    RECT stash;         // "Stash Tabs" row
    RECT showSockets;   // v1.3: "Show Sockets" row with 2-state toggle
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

// Left-rail navigation buttons (5 — Mods, Logs, Help, About, Exit).
// Options was removed; use Basic/Developer Options modals in the
// Loader Options section for settings, or edit D2RLoader.toml
// directly.
extern HWND g_hwNavMods;
extern HWND g_hwNavLogs;
extern HWND g_hwNavHelp;
extern HWND g_hwNavAbout;
extern HWND g_hwNavExit;

// Loader Options buttons (path picker + Plugins)
extern HWND g_hwLoaderDirBtn;         // "..." button beside the loader path bar
extern HWND g_hwLoaderPlugins;        // "Plugins" button — opens plugin manager
extern HWND g_hwLoaderBasicOptions;   // "Basic Options" button — opens the modal
extern HWND g_hwLoaderDevOptions;     // "Developer Options" button — opens the modal

// Mod list area buttons
extern HWND g_hwRefresh;          // top-right "Refresh"
extern HWND g_hwBrowseMods;       // bottom-left "Browse Mods"
extern HWND g_hwUpdateMod;        // bottom-right "Update Selected"

// Bottom expansion panel
extern HWND g_hwExpandToggle;     // arrow button
extern HWND g_hwExpandSections[4];  // References / Tools / Downloads / Tutorials
