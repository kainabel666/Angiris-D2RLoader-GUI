// ═══════════════════════════════════════════════════════════════════════
//  scaling.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Logical-to-physical pixel scaling. Two factors compose into a
//  single multiplier:
//
//    g_userScale   user-picked size (0.75 / 0.85 / 1.0 / 1.15 / 1.275)
//                  persisted in launcher_config.json
//    g_dpiScale    system DPI / 96.0 — read once at startup
//                  (declared in core.h, not here)
//    g_scale       g_userScale * g_dpiScale
//
//    logical  → physical  via S(int) / SF(REAL)
//    physical → logical   via U(int)
//
//  Every paint coordinate, font size, and HWND dimension is written in
//  logical pixels. The scale macros convert at the boundary. Mouse
//  input arrives in physical pixels and must be unscaled before being
//  tested against logical rects (paint code) or stays physical when
//  used against HWND rects (Win32 controls).
//
//  Also home to the three-stop scale preset table that drives the
//  toolbar "Scale" cycling button. The active subset depends on the
//  current DPI — see ActiveScalePresets.
//
//  Depends on core (for g_dpiScale) and config (for g_cfg.uiScale).
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// Live scale state. g_userScale is what the user picked; g_scale is
// what every S/SF/U call multiplies by. ApplyScaleChange (still in
// Angiris.cpp) is the only caller that mutates both — paint code
// reads them but never writes.
extern double g_userScale;
extern double g_scale;

// Conversion macros. Inline so they vanish at -O2.
//
//   S(x)  : int  logical → int  physical, lround for half-pixel safety
//   SF(x) : REAL logical → REAL physical, GDI+ float-precision form
//   U(x)  : int  physical → int logical, for unscaling mouse coords
inline int  S (int  x) { return (int)lround(x * g_scale); }
inline REAL SF(REAL x) { return (REAL)(x * g_scale); }
inline int  U (int  x) { return (int)lround(x / g_scale); }

// ── Logical-space wrappers around Win32 rect APIs ────────────────────
//
// Two helpers that absorb the scale conversion at the boundary so the
// rest of the launcher can work entirely in logical pixels. Each
// wraps the equivalent Win32 call and converts using S/U.

// InvalidateRect with a LOGICAL rectangle. Win32's InvalidateRect takes
// physical pixels (it doesn't know about g_scale), so we scale up the
// rectangle bounds before the call. nullptr passes through to mean
// "invalidate the entire client area".
inline BOOL InvalidateRectL(HWND hw, const RECT* logical, BOOL erase) {
    if (!logical) return InvalidateRect(hw, nullptr, erase);
    RECT phys = {
        S((int)logical->left),  S((int)logical->top),
        S((int)logical->right), S((int)logical->bottom)
    };
    return InvalidateRect(hw, &phys, erase);
}

// GetClientRect that returns LOGICAL dimensions. Win32's version returns
// physical pixels; this one runs U() on right/bottom so callers can
// operate the rest of their logic in the same logical space as Layout
// and the LO::* constants. left/top are always 0 either way.
inline void GetClientRectL(HWND hw, RECT* out) {
    GetClientRect(hw, out);
    out->right  = U((int)out->right);
    out->bottom = U((int)out->bottom);
}

// SetWindowPos wrapper that takes LOGICAL coordinates and applies S()
// to each before calling the Win32 function. Use whenever Layout or
// other code needs to position a child window in logical space — the
// scale transform is applied uniformly so callers don't have to think
// about g_scale individually.
inline BOOL SPosL(HWND hw, HWND hwAfter, int x, int y, int w, int h, UINT flags) {
    return SetWindowPos(hw, hwAfter, S(x), S(y), S(w), S(h), flags);
}

// ── MemDC: scoped GDI double-buffer ──────────────────────────────────
//
// Construct one inside a WM_PAINT handler to paint into a back buffer;
// the destructor BitBlts the buffer to the destination DC. Eliminates
// the flicker that would otherwise show up between FillRect and the
// subsequent GDI+/text overdraw.
//
// Translates the viewport so (x, y) on the destination DC maps to
// (0, 0) inside the buffer — calling code uses real (x, y, w, h)
// rect coords without needing to know about the back-buffer offset.
//
// Header-only: every paint TU constructs its own instances inline,
// no shared state and no .cpp needed.
struct MemDC {
    HDC dst, dc;
    HBITMAP bmp, oldBmp;
    int x, y, w, h;
    MemDC(HDC d, int X, int Y, int W, int H, COLORREF initFill = 0x000000)
        : dst(d), x(X), y(Y), w(W), h(H) {
        dc     = CreateCompatibleDC(d);
        bmp    = CreateCompatibleBitmap(d, w, h);
        oldBmp = (HBITMAP)SelectObject(dc, bmp);

        // Initialize the back buffer with raw GDI BEFORE any GDI+ work
        // touches it. Without this, if a later GDI+ call silently fails,
        // the destructor's BitBlt copies uninitialized memory to dst.
        RECT br = { 0, 0, w, h };
        HBRUSH hb = CreateSolidBrush(initFill);
        FillRect(dc, &br, hb);
        DeleteObject(hb);

        // Translate so 0,0 is top-left of the area we're drawing.
        SetViewportOrgEx(dc, -x, -y, nullptr);
    }
    ~MemDC() {
        SetViewportOrgEx(dc, 0, 0, nullptr);
        BitBlt(dst, x, y, w, h, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBmp);
        DeleteObject(bmp);
        DeleteDC(dc);
    }
    // Non-copyable — the destructor owns GDI resources.
    MemDC(const MemDC&)            = delete;
    MemDC& operator=(const MemDC&) = delete;
};

// ── Scale preset machinery ───────────────────────────────────────────
//
// The toolbar Scale button is a 3-stop cycling slider. The active
// three stops are picked from this five-entry table depending on the
// monitor's DPI — at 150% Windows scaling, only the smaller three
// values keep the launcher on-screen; at 100% scaling, the larger
// three give the user room to scale up.

struct ScalePreset { const wchar_t* label; double mul; };

// Defined in scaling.cpp. Read-only after init — callers iterate
// or index into it.
extern const ScalePreset g_scalePresets[5];

// Number of entries in g_scalePresets. Provided so callers don't
// have to do sizeof gymnastics.
constexpr int kNumScalePresets = 5;

// Fill (a, b, c) with the three indices into g_scalePresets[] that
// are active under the current g_dpiScale. The boundary is 1.25 —
// at-or-above returns {0,1,2} (75/85/100), below returns {2,3,4}
// (100/115/127).
void ActiveScalePresets(int& a, int& b, int& c);

// Which of the three active stops (0/1/2) matches the current
// g_cfg.uiScale? Used to drive the btn_toggle_*.png choice and as
// the starting position for the cycle-on-click action.
int ScaleToggleState();
