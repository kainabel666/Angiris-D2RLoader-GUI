// ═══════════════════════════════════════════════════════════════════════
//  assets.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Image asset loading + caching. Every PNG referenced by the
//  launcher lives under <appdir>\assets\images\ and is loaded on
//  first reference via AssetImage(), cached for the life of the
//  process, and destroyed via DestroyAssetCache (which is called
//  both at exit and before an in-place self-update overwrites
//  files).
//
//  Also exposes three drawing primitives that compose on top of the
//  cache: native-size blit, stretched blit, and 9-slice blit (for
//  bronze frames whose corner art shouldn't stretch).
//
//  Missing/unreadable assets cache as nullptr — every consumer is
//  expected to check for null and degrade gracefully (usually by
//  falling back to a programmatic paint).
//
//  Depends on core (AppDir) and the Win32/GDI+ surface in
//  angiris_common.
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// Where on disk the assets live. <exe directory>\assets\images.
// Exposed so paint helpers can reference loose files (e.g. background
// frames whose name comes from data rather than a literal).
wstring AssetDir();

// Look up (or load on first reference) a named PNG by filename. The
// returned pointer is owned by the cache — do NOT delete it. Returns
// nullptr if the file is missing or fails to decode; the null result
// is cached, so missing assets don't repeatedly hit the disk on
// every paint.
Gdiplus::Bitmap* AssetImage(const wchar_t* name);

// Free every cached bitmap. Called once at exit and once before an
// in-place self-update install begins (the install needs to overwrite
// the .png files on disk, which would otherwise be locked by their
// Bitmap objects' internal file handles).
void DestroyAssetCache();

// ── Drawing primitives ───────────────────────────────────────────────

// Blit an image at its native pixel size, top-left at (x, y).
// No-op when bitmap is null so missing assets degrade gracefully.
void DrawAssetAt(Gdiplus::Graphics& g, Gdiplus::Bitmap* b, int x, int y);

// Stretch an image into the rect (x, y, w, h) using bicubic filtering
// for the smoothest result. Saves and restores the Graphics object's
// interpolation mode.
void DrawAssetStretched(Gdiplus::Graphics& g, Gdiplus::Bitmap* b,
                        int x, int y, int w, int h);

// 9-slice blit: corners are pixel-perfect, edges stretch one axis,
// center stretches both. `corner` is the inset distance from each
// edge that should remain unstretched (the pixel size of the corner
// art — typically 12–20 px for our bronze frames). Auto-clamps if
// `corner` exceeds half the source or destination size.
//
// No-op when bitmap is null.
void DrawButton9Slice(Gdiplus::Graphics& g, Gdiplus::Bitmap* b,
                      int dx, int dy, int dw, int dh, int corner);

// ── Frame inset measurement ──────────────────────────────────────────
//
// Several launcher assets are bronze-bordered "frames" with a
// transparent interior — frame_main.png, frame_expand.png, etc. Paint
// code needs to know how many pixels in from each edge the border art
// extends so content can be positioned inside the visible border.
//
// MeasureFrameInset walks the asset's alpha channel from each edge
// inward, returning the band-width at which the row/column density
// drops below the "this is solid filigree" threshold (30% opaque).
//
// Cached per-asset by name. The cache lives for the life of the
// process — once measured, never re-scanned.

struct FrameInset { int top, bottom, left, right; };

FrameInset MeasureFrameInset(const wchar_t* assetName);

// ── Right panel region measurement ───────────────────────────────────
//
// frame_panel_right.png is split into three vertical regions by
// horizontal dividers in the art:
//   - top    : MOD DESCRIPTION
//   - middle : LAUNCH OPTIONS
//   - bottom : PLAY
//
// MeasurePanelRegions detects the dividers by scanning row density
// (rows that are mostly-opaque across the full width are dividers)
// and returns the interior bounds of each region. Values are in the
// asset's native pixel space, since the asset is drawn 1:1.
//
// `valid` is false if the asset is missing — callers should treat
// the other fields as undefined in that case.

struct PanelRegions {
    bool valid;
    int  topPanelY0, topPanelY1;
    int  midPanelY0, midPanelY1;
    int  botPanelY0, botPanelY1;
    int  sideMargin;
};

PanelRegions MeasurePanelRegions(const wchar_t* assetName);
