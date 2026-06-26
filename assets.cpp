// ═══════════════════════════════════════════════════════════════════════
//  assets.cpp — see assets.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "assets.h"
#include "core.h"   // AppDir

// File-local cache. Bitmaps are owned here; AssetImage returns
// non-owning pointers. Cleared by DestroyAssetCache.
//
// nullptr values ARE cached — once a file is determined to be
// missing or undecodable, we don't repeatedly hammer the disk on
// every paint. The cache lifetime is the process lifetime (or
// destroyed early before self-update overwrites the .png files).
static map<wstring, Gdiplus::Bitmap*> g_assetCache;

wstring AssetDir() {
    return AppDir() + L"\\assets\\images";
}

Gdiplus::Bitmap* AssetImage(const wchar_t* name) {
    auto it = g_assetCache.find(name);
    if (it != g_assetCache.end()) return it->second;
    wstring full = AssetDir() + L"\\" + name;
    Gdiplus::Bitmap* b = nullptr;
    if (GetFileAttributes(full.c_str()) != INVALID_FILE_ATTRIBUTES) {
        b = new Gdiplus::Bitmap(full.c_str());
        if (!b || b->GetLastStatus() != Ok) {
            delete b;
            b = nullptr;
        }
    }
    g_assetCache[name] = b;     // cache nullptrs too
    return b;
}

void DestroyAssetCache() {
    for (auto& kv : g_assetCache) delete kv.second;
    g_assetCache.clear();
}

void DrawAssetAt(Gdiplus::Graphics& g, Gdiplus::Bitmap* b, int x, int y) {
    if (!b) return;
    g.DrawImage(b, x, y, (INT)b->GetWidth(), (INT)b->GetHeight());
}

void DrawAssetStretched(Gdiplus::Graphics& g, Gdiplus::Bitmap* b,
                        int x, int y, int w, int h) {
    if (!b) return;
    Gdiplus::InterpolationMode prev = g.GetInterpolationMode();
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.DrawImage(b, x, y, w, h);
    g.SetInterpolationMode(prev);
}

void DrawButton9Slice(Gdiplus::Graphics& g, Gdiplus::Bitmap* b,
                      int dx, int dy, int dw, int dh, int corner) {
    if (!b || dw <= 0 || dh <= 0) return;
    int sw = (int)b->GetWidth();
    int sh = (int)b->GetHeight();
    // Clamp corner so it never exceeds half the source or destination.
    int c = corner;
    if (c * 2 > sw) c = sw / 2;
    if (c * 2 > sh) c = sh / 2;
    if (c * 2 > dw) c = dw / 2;
    if (c * 2 > dh) c = dh / 2;
    if (c < 1) {
        // Degenerate: just stretch the whole image.
        Gdiplus::InterpolationMode prev = g.GetInterpolationMode();
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.DrawImage(b, dx, dy, dw, dh);
        g.SetInterpolationMode(prev);
        return;
    }

    int sCx = sw - c;        // source center x-extent ends at sCx
    int sCy = sh - c;        // source center y-extent ends at sCy
    int sCw = sw - c * 2;    // source center width
    int sCh = sh - c * 2;    // source center height
    int dCx = dx + c;
    int dCy = dy + c;
    int dCw = dw - c * 2;
    int dCh = dh - c * 2;

    Gdiplus::InterpolationMode prev = g.GetInterpolationMode();
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

    // Helper to draw one piece from source rect to dest rect.
    auto piece = [&](int sx, int sy, int spw, int sph,
                     int px, int py, int pw, int ph) {
        if (pw <= 0 || ph <= 0 || spw <= 0 || sph <= 0) return;
        g.DrawImage(b,
            Gdiplus::Rect(px, py, pw, ph),
            (REAL)sx, (REAL)sy, (REAL)spw, (REAL)sph,
            Gdiplus::UnitPixel);
    };

    // Corners (pixel-perfect, no scaling — source size == dest size == c×c)
    piece(0,       0,       c,   c,   dx,        dy,        c,   c);    // TL
    piece(sCx,     0,       c,   c,   dCx + dCw, dy,        c,   c);    // TR
    piece(0,       sCy,     c,   c,   dx,        dCy + dCh, c,   c);    // BL
    piece(sCx,     sCy,     c,   c,   dCx + dCw, dCy + dCh, c,   c);    // BR
    // Edges (stretched along one axis)
    piece(c,       0,       sCw, c,   dCx,       dy,        dCw, c);    // top
    piece(c,       sCy,     sCw, c,   dCx,       dCy + dCh, dCw, c);    // bottom
    piece(0,       c,       c,   sCh, dx,        dCy,       c,   dCh);  // left
    piece(sCx,     c,       c,   sCh, dCx + dCw, dCy,       c,   dCh);  // right
    // Center (stretched both axes)
    piece(c,       c,       sCw, sCh, dCx,       dCy,       dCw, dCh);

    g.SetInterpolationMode(prev);
}

// ── Frame inset + panel region caches ────────────────────────────────
//
// File-local caches. Both extend over the process lifetime — once a
// frame's geometry is measured, it never changes (the .png on disk
// is static between launcher runs, and these are read-only scans).

static map<wstring, FrameInset>   g_frameInsetCache;
static map<wstring, PanelRegions> g_panelRegionCache;

FrameInset MeasureFrameInset(const wchar_t* assetName) {
    auto it = g_frameInsetCache.find(assetName);
    if (it != g_frameInsetCache.end()) return it->second;

    FrameInset r = { 0, 0, 0, 0 };
    Gdiplus::Bitmap* b = AssetImage(assetName);
    if (!b) { g_frameInsetCache[assetName] = r; return r; }

    UINT W = b->GetWidth(), H = b->GetHeight();
    Gdiplus::BitmapData bd = {};
    Gdiplus::Rect lockRect(0, 0, (INT)W, (INT)H);
    if (b->LockBits(&lockRect, Gdiplus::ImageLockModeRead,
                    PixelFormat32bppARGB, &bd) != Ok) {
        g_frameInsetCache[assetName] = r;
        return r;
    }

    // Pixel layout: each row is bd.Stride bytes; ARGB premultiplied is BGRA
    // little-endian (A is byte index 3 within each pixel).
    auto alphaAt = [&](UINT x, UINT y) -> BYTE {
        const BYTE* row = (const BYTE*)bd.Scan0 + (intptr_t)y * bd.Stride;
        return row[x * 4 + 3];
    };

    // Density per row/column: how many pixels along this scan line have
    // alpha > 50. The edge filigree shows as a solid band along the outer
    // edge; the corner ornaments extend further but are sparse.
    auto rowDensity = [&](UINT y) {
        UINT n = 0;
        for (UINT x = 0; x < W; ++x) if (alphaAt(x, y) > 50) ++n;
        return n;
    };
    auto colDensity = [&](UINT x) {
        UINT n = 0;
        for (UINT y = 0; y < H; ++y) if (alphaAt(x, y) > 50) ++n;
        return n;
    };

    // Threshold for "this is the solid edge filigree": at least 30% of the
    // scan line is opaque. Below that we're either in the corner-only
    // region or fully interior.
    const UINT rowThresh = W * 30 / 100;
    const UINT colThresh = H * 30 / 100;

    // Top: scan down from y=0 while row density is above the edge threshold;
    // stop the moment it drops (= we crossed into interior).
    for (UINT y = 0; y < H / 2; ++y) {
        if (rowDensity(y) >= rowThresh) r.top = (int)(y + 1);
        else break;
    }
    // Bottom: scan up
    for (UINT i = 0; i < H / 2; ++i) {
        UINT y = H - 1 - i;
        if (rowDensity(y) >= rowThresh) r.bottom = (int)(i + 1);
        else break;
    }
    // Left: scan right
    for (UINT x = 0; x < W / 2; ++x) {
        if (colDensity(x) >= colThresh) r.left = (int)(x + 1);
        else break;
    }
    // Right: scan left
    for (UINT i = 0; i < W / 2; ++i) {
        UINT x = W - 1 - i;
        if (colDensity(x) >= colThresh) r.right = (int)(i + 1);
        else break;
    }

    b->UnlockBits(&bd);
    g_frameInsetCache[assetName] = r;
    return r;
}

PanelRegions MeasurePanelRegions(const wchar_t* assetName) {
    auto it = g_panelRegionCache.find(assetName);
    if (it != g_panelRegionCache.end()) return it->second;

    PanelRegions p = {};
    Gdiplus::Bitmap* b = AssetImage(assetName);
    if (!b) { g_panelRegionCache[assetName] = p; return p; }

    UINT W = b->GetWidth(), H = b->GetHeight();
    Gdiplus::BitmapData bd = {};
    Gdiplus::Rect lockRect(0, 0, (INT)W, (INT)H);
    if (b->LockBits(&lockRect, Gdiplus::ImageLockModeRead,
                    PixelFormat32bppARGB, &bd) != Ok) {
        g_panelRegionCache[assetName] = p;
        return p;
    }
    auto alphaAt = [&](UINT x, UINT y) -> BYTE {
        const BYTE* row = (const BYTE*)bd.Scan0 + (intptr_t)y * bd.Stride;
        return row[x * 4 + 3];
    };
    // Walk every row; track contiguous "divider" bands (opaque across >50%
    // of width). Collect up to 4 bands: top edge, middle divider, inner
    // divider (between launch options and play), bottom edge.
    int bandStart[8] = {0}, bandEnd[8] = {0};
    int nBands = 0;
    bool inBand = false;
    int bs = 0;
    for (UINT y = 0; y < H && nBands < 8; ++y) {
        UINT n = 0;
        for (UINT x = 0; x < W; ++x) if (alphaAt(x, y) > 50) ++n;
        bool isBand = (n > W / 2);
        if (isBand && !inBand) { bs = (int)y; inBand = true; }
        else if (!isBand && inBand) {
            bandStart[nBands] = bs;
            bandEnd[nBands]   = (int)y - 1;
            ++nBands;
            inBand = false;
        }
    }
    if (inBand && nBands < 8) {
        bandStart[nBands] = bs;
        bandEnd[nBands]   = (int)H - 1;
        ++nBands;
    }

    // We expect 4 bands (top edge, mid divider, inner divider, bottom edge).
    if (nBands >= 4) {
        p.valid       = true;
        p.topPanelY0  = bandEnd[0] + 1;
        p.topPanelY1  = bandStart[1] - 1;
        p.midPanelY0  = bandEnd[1] + 1;
        p.midPanelY1  = bandStart[2] - 1;
        p.botPanelY0  = bandEnd[2] + 1;
        p.botPanelY1  = bandStart[3] - 1;
    }
    // Measure side margin from a representative row inside the top panel.
    if (p.valid) {
        int sampleY = (p.topPanelY0 + p.topPanelY1) / 2;
        int leftIn = 0, rightIn = 0;
        for (UINT x = 0; x < W; ++x) {
            if (alphaAt(x, (UINT)sampleY) < 50) { leftIn = (int)x; break; }
        }
        for (UINT i = 0; i < W; ++i) {
            UINT x = W - 1 - i;
            if (alphaAt(x, (UINT)sampleY) < 50) { rightIn = (int)i; break; }
        }
        p.sideMargin = max(leftIn, rightIn);
    }

    b->UnlockBits(&bd);
    g_panelRegionCache[assetName] = p;
    return p;
}
