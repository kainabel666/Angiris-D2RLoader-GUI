// ═══════════════════════════════════════════════════════════════════════
//  buttons.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Owner-drawn button taxonomy + state machinery. Every clickable
//  button on the launcher main window and dialogs is created via
//  MkStdBtn, which registers it for hover tracking and paint
//  dispatch in this module. The actual paint happens via
//  PaintOwnerDrawButton (called from WM_DRAWITEM in any wndproc
//  that owns these buttons — MainProc or any dialog proc).
//
//  Public surface:
//    ButtonKind             — taxonomy of button visuals
//    MkStdBtn               — create + register + subclass in one call
//    RegisterButton         — register an externally-created HWND
//    PaintOwnerDrawButton   — call from WM_DRAWITEM to paint the button
//    SetButtonDirty         — flag a button for the "dirty" highlight
//                             (used by Refresh when mods watcher fires)
//
//  Depends on assets (asset cache + 9-slice), fonts (label fonts),
//  colors (Tok::*), scaling (S, SF), layout (LO::BTN_OVERFLOW_PAD),
//  core (g_hInst).
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// Visual + behavioral category for each owner-drawn button.
enum class ButtonKind {
    Nav,
    NavSm,
    Refresh,
    NexusUpdate,
    ModLink,           // legacy — kept so existing call sites compile; not used after the link buttons split
    ModLinkDocs,       // square 85×85 — uses btn_docs.png; falls back to 9-sliced btn_nexus_update + "D"
    ModLinkDiscord,    // square 85×85 — uses btn_discord.png; falls back to 9-sliced btn_nexus_update + "X"
    ModLinkWebsite,    // square 85×85 — uses btn_website.png; falls back to 9-sliced btn_nexus_update + "W"
    Play,
    Ellipse,
    Arrow,
    Plugins,           // Loader Options "Plugins" button — uses btn_nexus_update.png frame
                       // but with no hover-grow (text color shift on hover comes from
                       // the existing highlight path)
};

// Create an owner-drawn BUTTON child window pre-registered with the
// state map (so WM_DRAWITEM knows which asset/transform to use) and
// the hover subclass (so WM_MOUSEMOVE drives the hover highlight).
//
// `enabled` defaults to true and `kind` defaults to Nav so the most
// common call shape (six-arg create) keeps working — historical
// CreateControls call sites omit both.
HWND MkStdBtn(HWND parent, const wchar_t* lbl, int id,
              int x, int y, int w, int h, bool enabled = true,
              ButtonKind kind = ButtonKind::Nav);

// Register an externally-created BUTTON HWND in the state map and
// install the hover subclass. For custom CreateWindow paths that
// can't go through MkStdBtn but still need owner-draw paint.
void RegisterButton(HWND hw, ButtonKind kind);

// Paint a BS_OWNERDRAW BUTTON. Call from WM_DRAWITEM in any wndproc
// that owns buttons registered via MkStdBtn / RegisterButton.
// Returns true if handled (button found and painted), false
// otherwise (caller should fall through for menus / unknown items).
//
//   case WM_DRAWITEM:
//       if (PaintOwnerDrawButton((DRAWITEMSTRUCT*)lp)) return TRUE;
//       // ... fall through for menu items, etc.
bool PaintOwnerDrawButton(DRAWITEMSTRUCT* d);

// Set/clear the "dirty" highlight on a button. Used by Refresh when
// the mod-folder watcher reports pending changes. No-op if `hw`
// isn't a registered owner-draw button.
void SetButtonDirty(HWND hw, bool dirty);
