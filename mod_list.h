// ═══════════════════════════════════════════════════════════════════════
//  mod_list.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Custom mod-list control. Implements the scrollable, painted list
//  of mod rows in the launcher's center panel. Drawn by a dedicated
//  window class (registered via RegisterModListClass) so it can own
//  WM_PAINT, WM_MOUSEMOVE, the wheel, and an in-list custom scrollbar
//  without trying to retrofit a stock listbox.
//
//  Public surface is intentionally tiny: the host code registers the
//  class once, then CreateWindow with MOD_LIST_CLASS to spawn the
//  control. Everything else — paint, hit-test, scrolling, hover,
//  selection — is private to mod_list.cpp.
//
//  Per-window state (scroll offset, hover row, scrollbar drag state)
//  is heap-allocated MLState attached on WM_NCCREATE and freed on
//  WM_NCDESTROY, keyed by HWND in a file-local map. Callers don't
//  need to know about MLState at all.
//
//  Dependencies inbound:
//    assets, fonts, colors, scaling, layout (LO::ROW_H, etc.),
//    mod_scan (g_mods, g_selMod), mod_types (ModInfo),
//    mod_updates (UpdateInfo via GetUpdateInfo),
//    hover_tip (Show/Hide on row enter/leave),
//    config (g_cfg for D2R path / first-row defaults).
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// Window class name. Used both by the registration call and by the
// CreateWindow that spawns the control. Inline so both translation
// units see the same string without an ODR violation.
inline constexpr const wchar_t* MOD_LIST_CLASS = L"Angiris_ModList";

// ── Notifications sent from the mod list to its parent (g_hwMain) ────
//
// ML_REFRESH       — recompute layout + repaint. Sent by the host
//                    after FindMods has been re-run (drop, install,
//                    folder watcher fired) so the list rebuilds.
// ML_NOTIFY_SELECT — wp = new selected index. Sent on a row click
//                    so MainProc can update g_selMod / save state /
//                    refresh the right-panel description.
constexpr UINT ML_REFRESH       = WM_USER + 1;
constexpr UINT ML_NOTIFY_SELECT = WM_USER + 2;

// Register the mod list window class. Idempotent at the OS level —
// safe to call multiple times, but the host code calls it exactly
// once during wWinMain bootstrap. CS_DBLCLKS is set so the proc
// receives WM_LBUTTONDBLCLK (used to launch the double-clicked mod).
void RegisterModListClass(HINSTANCE hInst);

// Compute the on-screen rect of a single mod list row in the list
// control's LOGICAL client space (i.e. the coordinates suitable for
// InvalidateRectL). Returns false if the row index is out of range,
// the HWND is null, or the row is not currently visible (fully
// scrolled out of view).
//
// Called by the host code on selection change to invalidate only
// the rows that visually change — the previously-selected row and
// the newly-selected one — rather than repainting the whole list.
// Expanded by 4 px on all sides to cover the selection halo.
bool MLRowClientRect(HWND hwList, int rowIndex, RECT* out);
