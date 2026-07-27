// ═══════════════════════════════════════════════════════════════════════
//  control_ids.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Win32 control + menu IDs shared across translation units. Lives in
//  a header so any module that creates a control with one of these
//  IDs, or sends/receives WM_COMMAND with one of these IDs, can see
//  the same numeric values without each module re-declaring its own.
//
//  ID ranges (avoid collisions when adding new entries):
//    100..199   child controls (buttons, listboxes, dropdowns)
//    200..299   bottom expansion panel buttons (tool/ref/dl rows)
//    500..599   TrackPopupMenu command IDs (right-click menus)
//    600..699   launcher self-update dialog buttons
//
//  Header-only — these are compile-time constants, no .cpp needed.
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

enum {
    IDC_MOD_LIST       = 100,
    IDC_LAUNCH_BTN     = 101,

    // Mod Description panel link buttons (per-selected-mod)
    IDC_MOD_DISCORD    = 110,
    IDC_MOD_DOCS       = 111,
    IDC_MOD_WEBSITE    = 112,

    // Left rail navigation buttons (open external paths / files)
    IDC_NAV_MODS       = 120,
    // 121 (was IDC_NAV_OPTIONS) reserved / retired — the Options nav
    // button was removed; D2RLoader.toml can be edited directly by
    // the user, and Basic/Developer Options modals cover the settings
    // that were formerly reachable through this nav slot.
    IDC_NAV_LOGS       = 122,
    IDC_NAV_HELP       = 123,
    IDC_NAV_ABOUT      = 124,
    IDC_NAV_EXIT       = 125,

    // Loader Directory: read-only path text + Browse (...) button
    IDC_LOADER_DIR_BTN        = 130,
    IDC_LOADER_PLUGINS        = 131,    // "Plugins" button — opens plugin manager popup
    IDC_LOADER_BASIC_OPTIONS  = 132,    // opens Basic Options modal
    IDC_LOADER_DEV_OPTIONS    = 133,    // opens Developer Options modal

    // Mod list refresh button (top-right of list column)
    IDC_REFRESH_BTN    = 140,

    // Browse / Update Selected buttons below the mod list
    IDC_BROWSE_MODS    = 141,
    IDC_UPDATE_MOD     = 142,

    // Bottom expansion panel toggle (arrow button)
    IDC_EXPAND_TOGGLE  = 150,

    // Expand-panel SECTION buttons — four buttons (References / Tools /
    // Downloads / Tutorials) that each open a section modal. The modal
    // uses its own local button IDs, so these per-item ranges are no
    // longer wired to live controls; kept for reference / possible reuse.
    IDC_SECTION_FIRST  = 190,    // 190..193  (4 section buttons)

    IDC_TOOL_FIRST     = 200,    // (unused since v1.5 modal restructure)
    IDC_REF_FIRST      = 210,    // (unused since v1.5 modal restructure)
    IDC_DL_FIRST       = 220,    // (unused since v1.5 modal restructure)
    IDC_TUT_FIRST      = 230,    // (unused since v1.5 modal restructure)

    // Mod list right-click context menu commands. These aren't real
    // child-control IDs — they're TrackPopupMenu command IDs that come
    // back through WM_COMMAND when the user picks a menu item.
    IDM_MOD_OPEN_FOLDER  = 500,
    IDM_MOD_BACKUP_SAVES = 501,
    IDM_MOD_REZIP        = 502,
    IDM_MOD_UNINSTALL    = 503,

    // Restart button shown on the progress dialog when a launcher
    // self-update install reaches stage 4. Clicking it spawns the
    // newly-installed exe and closes the running one.
    IDC_LAUNCHER_RESTART_BTN = 600,
};

// Notification sent up from child controls when any flag changes.
constexpr UINT OPT_NOTIFY_CHANGED = WM_USER + 10;
