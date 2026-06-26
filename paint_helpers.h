// ═══════════════════════════════════════════════════════════════════════
//  paint_helpers.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Standalone paint primitives + small chrome painters that the main
//  window's paint code and dialogs share. Extracted from Angiris.cpp
//  in Phase 7b because each one has clean inputs/outputs (no shared
//  state with the bigger PaintBody / PaintLeftRail / etc. — those
//  stay behind for Phase 7c).
//
//  Six functions:
//    FillSolid          — GDI quick-fill (used outside Graphics scope)
//    DrawGoldText       — text with optional dark drop-shadow + tint
//    DrawFlagCheckbox   — themed checkbox glyph (with asset fallback)
//    OPDrawBtnFrame     — bronze-bordered panel button frame
//    PaintTopOrnament   — ornament_gem.png at the top of the main window
//    PaintCornerAccents — corner_tl/tr/bl/br.png brackets at frame corners
//
//  Depends on colors (Tok::*), assets (AssetImage, DrawAssetAt),
//  scaling (SF), layout (LO::*), fonts (no — text rendering uses
//  caller-supplied fonts).
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"
#include "colors.h"   // Tok::Gold default arg in DrawGoldText

// GDI quick-fill — single-call solid rectangle without needing a
// Graphics object. Used in two places where we have an HDC but the
// surrounding code doesn't construct a Gdiplus::Graphics yet.
void FillSolid(HDC hdc, int x, int y, int w, int h, COLORREF c);

// Gold-tinted text with optional 2px-down dark drop-shadow.
// Caller supplies the font + rect + StringFormat. The `main` color
// defaults to Tok::Gold (the runtime color, which can shift via the
// Colour preset); pass any other Color to override.
void DrawGoldText(Gdiplus::Graphics& g, const wstring& s,
                  Gdiplus::Font* fnt, Gdiplus::RectF rect,
                  Gdiplus::StringFormat* sf,
                  Gdiplus::Color main = Tok::Gold, bool glow = true);

// Custom checkbox glyph. State:
//   checked + isLocked → checkbox_locked.png  (asset's yellow-X variant)
//   checked            → checkbox_checked.png (asset's X-mark)
//   unchecked          → checkbox.png         (asset's empty box)
// Falls back to programmatic gold-and-X paint when assets are missing.
void DrawFlagCheckbox(Gdiplus::Graphics& g, REAL x, REAL y,
                      bool checked, bool hover, bool isLocked);

// Panel-button frame: dark fill + 1px bronze border (gold when hot).
// Used by the bottom expansion panel buttons and similar themed
// rectangles where the paint isn't carrying a backdrop asset.
void OPDrawBtnFrame(Gdiplus::Graphics& g, int x, int y, int w, int h,
                    bool hot);

// Top centerpiece: draws ornament_gem.png centered on W/2 at
// LO::ORNAMENT_TOP_Y. Falls back to a programmatic bronze lozenge +
// gemstone if the asset is missing.
void PaintTopOrnament(Gdiplus::Graphics& g, int W);

// Frame corner accents: draws corner_tl.png, corner_tr.png,
// corner_bl.png, corner_br.png at the four content-area corners
// of frame_main.png. Each bracket seats at the inner edge of the
// frame's filigree border.
void PaintCornerAccents(Gdiplus::Graphics& g, int W);
