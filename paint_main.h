// ═══════════════════════════════════════════════════════════════════════
//  paint_main.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Main window paint entry. Called from MainProc's WM_PAINT handler;
//  paints the entire client area in one pass (frame, panels, mod
//  description, launch options, bottom expansion panel, toolbar).
//
//  Internal helpers (PaintLeftRail, PaintModDescription,
//  PaintLaunchOptions, PaintBottomPanel, PaintToolbarControl,
//  BreakLaunchArgsAtDash) are file-static in paint_main.cpp since
//  only PaintBody coordinates them.
//
//  Depends on: assets, fonts, colors, scaling, layout, ui_state,
//  paint_helpers, buttons, config, update_cache, mod_scan,
//  mod_config, launch_flags, seeds, launcher_self_update.
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// Paint the entire client area. `W` and `H` arrive in LOGICAL pixels
// (caller has run U()); paint_main.cpp installs a GDI+ ScaleTransform
// so all the paint code below works in logical space.
//
// Called from MainProc's WM_PAINT after PreservedRenderState setup.
void PaintBody(HDC hdc, int W, int H);

// Drop the cached static-stone backdrop bitmaps, forcing a rebuild on the
// next paint. The cache keys itself on window size and UI scale, so those
// changes are handled automatically — call this only when the underlying
// ASSETS change (e.g. the asset cache is rebuilt), which the size/scale
// check can't detect.
void InvalidateStoneCache();
