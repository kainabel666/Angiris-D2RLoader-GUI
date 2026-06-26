// ═══════════════════════════════════════════════════════════════════════
//  colors.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Design tokens — every color and spacing value in the launcher
//  flows through one of these constants. Inline-defined colors at
//  paint sites are forbidden by convention; if you need a new shade,
//  add it here.
//
//  Two namespaces:
//    Tok::    GDI+ Color tokens + GDI COLORREF tokens
//    Sp::     spacing scale (4, 8, 12, 16, 24, 32)
//
//  Most Tok:: entries are const. The Gold + GoldBright pair is
//  NON-const — the toolbar Colour dropdown reassigns them at runtime
//  so every existing paint site picks up the user's pick on the next
//  brush/pen construction. See ApplyColorChange below.
//
//  Depends on core for the Win32/GDI+ surface (Color, COLORREF, RGB).
//  No upward dependencies — colors are leaves of the dep graph.
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// GDI+ Color shortcuts: GP = fully opaque, GPA = with explicit alpha.
// Inline so they fold away at -O2.
inline Gdiplus::Color GP (int r, int g, int b) {
    return Gdiplus::Color(255, r, g, b);
}
inline Gdiplus::Color GPA(int a, int r, int g, int b) {
    return Gdiplus::Color(a, r, g, b);
}

namespace Tok {
    // ── Backgrounds ──────────────────────────────────────────────────
    extern const Gdiplus::Color BgDeep;
    extern const Gdiplus::Color BgPanel;
    extern const Gdiplus::Color BgPanel2;

    // ── Gold accents (NON-const) ─────────────────────────────────────
    //
    // Gold + GoldBright are reassigned at runtime by ApplyColorChange
    // when the user picks a Colour preset in the toolbar. Every paint
    // site reads these freshly each frame, so changes propagate on the
    // next RedrawWindow without rebuilding any caches.
    extern Gdiplus::Color Gold;
    extern Gdiplus::Color GoldBright;
    extern const Gdiplus::Color GoldDim;
    extern const Gdiplus::Color GoldDeep;

    // ── Bronze structural ────────────────────────────────────────────
    extern const Gdiplus::Color Bronze;
    extern const Gdiplus::Color BronzeDim;
    extern const Gdiplus::Color BronzeBright;

    // ── Launch red ───────────────────────────────────────────────────
    extern const Gdiplus::Color RedDark;
    extern const Gdiplus::Color RedBright;
    extern const Gdiplus::Color RedGlow;

    // ── Text ─────────────────────────────────────────────────────────
    extern const Gdiplus::Color TextParchment;
    extern const Gdiplus::Color TextDim;
    extern const Gdiplus::Color ParchmentInk;

    // ── GDI COLORREFs (for native controls / WM_CTLCOLOR) ────────────
    extern const COLORREF crBgDeep;
    extern const COLORREF crBgPanel;
    extern const COLORREF crGold;
    extern const COLORREF crGoldBright;
    extern const COLORREF crBronzeDim;
    extern const COLORREF crText;
}

// Spacing scale — only these six values are used across the launcher.
// If a layout calls for a different gap, extend this enum rather than
// inline-pasting a number.
namespace Sp {
    constexpr int s1 =  4;
    constexpr int s2 =  8;
    constexpr int s3 = 12;
    constexpr int s4 = 16;
    constexpr int s5 = 24;
    constexpr int s6 = 32;
}

// ── Color preset machinery ───────────────────────────────────────────
//
// Twelve color choices for the toolbar Colour dropdown. Index 0 is
// the default ("Dark Red") — historical reasons; the launcher's
// out-of-box look uses the hard-coded Gold values above, not preset 0.
// A negative or out-of-range g_cfg.fontColorIdx in ApplyColorChange
// resolves to those hard-coded defaults instead.

struct ColorPreset { const wchar_t* label; COLORREF rgb; };

// Defined in colors.cpp. Read-only after init.
extern const ColorPreset g_colorPresets[12];
constexpr int kNumColorPresets = 12;

// Reassign Tok::Gold and Tok::GoldBright based on g_cfg.fontColorIdx,
// then force a full RedrawWindow on g_hwMain so every paint site
// picks up the new color. Called from the Colour dropdown's setter
// AND once at wWinMain startup to apply the saved fontColorIdx.
void ApplyColorChange();
