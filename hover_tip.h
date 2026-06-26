// ═══════════════════════════════════════════════════════════════════════
//  hover_tip.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Mod-list hover tooltip. When the cursor sits over a mod row past
//  the dwell time, a small parchment popup appears next to the
//  cursor showing the mod's playtime + last played stats. Single
//  popup at a time — ShowHoverTip tears down any prior tip before
//  creating a new one, so callers don't have to manage the
//  lifecycle.
//
//  Two-function API; everything else (window class, paint, popup
//  state, positioning math) is private to hover_tip.cpp.
//
//  Mod list code calls these on row-hover / row-leave. No other
//  caller in the launcher right now, but the API is intentionally
//  generic — pass any mod index and you get its hover card.
//
//  Depends on assets (bg_stone.png + Tok::Gold), fonts (g_fBtn),
//  mod_scan (g_mods), and playtime (FormatPlaytime / FormatLastPlayed).
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// Position the hover tip near the cursor showing playtime + last
// played for g_mods[modIdx]. Tears down any prior visible tip
// first. No-op (after teardown) if modIdx is out of range — that
// behavior is the documented escape valve for the "no current
// hover" caller pattern: call ShowHoverTip(-1) to hide.
//
// Stays out of focus and out of the taskbar (WS_EX_TOOLWINDOW +
// WS_EX_NOACTIVATE). Auto-positions to stay on the working monitor
// if the natural down-right offset would push off-screen.
void ShowHoverTip(int modIdx);

// Destroy the tooltip if visible. Idempotent — safe to call from
// any cursor-leave or selection-change path.
void HideHoverTip();
