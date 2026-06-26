// ═══════════════════════════════════════════════════════════════════════
//  colors.cpp — see colors.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "colors.h"
#include "core.h"     // g_hwMain
#include "config.h"   // g_cfg.fontColorIdx

namespace Tok {
    const Gdiplus::Color BgDeep      = GP(0x0A, 0x07, 0x05);
    const Gdiplus::Color BgPanel     = GP(0x1A, 0x11, 0x08);
    const Gdiplus::Color BgPanel2    = GP(0x24, 0x17, 0x10);

    // NON-const — reassigned at runtime by ApplyColorChange.
    Gdiplus::Color Gold        = GP(0xC8, 0xA8, 0x4B);
    Gdiplus::Color GoldBright  = GP(0xFF, 0xD7, 0x00);
    const Gdiplus::Color GoldDim     = GP(0x8B, 0x69, 0x14);
    const Gdiplus::Color GoldDeep    = GP(0x6B, 0x4F, 0x1A);

    const Gdiplus::Color Bronze      = GP(0x5C, 0x3A, 0x1E);
    const Gdiplus::Color BronzeDim   = GP(0x3A, 0x25, 0x10);
    const Gdiplus::Color BronzeBright= GP(0x8B, 0x5A, 0x2B);

    const Gdiplus::Color RedDark     = GP(0x64, 0x00, 0x00);
    const Gdiplus::Color RedBright   = GP(0xDC, 0x28, 0x00);
    const Gdiplus::Color RedGlow     = GP(0xFF, 0x48, 0x18);

    const Gdiplus::Color TextParchment = GP(0xD4, 0xB4, 0x83);
    const Gdiplus::Color TextDim       = GP(0x78, 0x60, 0x40);
    const Gdiplus::Color ParchmentInk  = GP(0x2A, 0x1A, 0x0A);

    const COLORREF crBgDeep    = RGB(0x0A, 0x07, 0x05);
    const COLORREF crBgPanel   = RGB(0x1A, 0x11, 0x08);
    const COLORREF crGold      = RGB(0xC8, 0xA8, 0x4B);
    const COLORREF crGoldBright= RGB(0xFF, 0xD7, 0x00);
    const COLORREF crBronzeDim = RGB(0x3A, 0x25, 0x10);
    const COLORREF crText      = RGB(0xD4, 0xB4, 0x83);
}

// ── Color presets ────────────────────────────────────────────────────
//
// Order is historical — the saved fontColorIdx in old configs points
// at the array position from when the launcher shipped, not the
// "intuitive" sort. Don't reorder; add new colors at the end. The
// "NEW" comments mark entries added after the original launcher
// release; "kept" marks survivors of the early pruning pass.

const ColorPreset g_colorPresets[kNumColorPresets] = {
    { L"Dark Red",     RGB(0x8B, 0x00, 0x00) },   //  0 — kept
    { L"Crimson",      RGB(0xC8, 0x3C, 0x50) },   //  1 — NEW: warm rose-red, brighter than Dark Red
    { L"Bright Red",   RGB(0xC8, 0x10, 0x20) },   //  2 — kept
    { L"Amber",        RGB(0xFF, 0xA5, 0x32) },   //  3 — NEW: warm orange between reds and golds
    { L"Gold",         RGB(0xE8, 0xC2, 0x5E) },   //  4 — kept (was idx 5)
    { L"Bright Gold",  RGB(0xFF, 0xD7, 0x00) },   //  5 — kept (was idx 6)
    { L"Pale Gold",    RGB(0xF8, 0xE6, 0xA0) },   //  6 — kept (was idx 7)
    { L"Silver",       RGB(0xC8, 0xC8, 0xD2) },   //  7 — NEW: cool metallic neutral
    { L"Dark Green",   RGB(0x00, 0x6B, 0x1C) },   //  8 — kept
    { L"Frost Teal",   RGB(0x50, 0xC8, 0xC8) },   //  9 — NEW: cool aqua, "ice" magic feel
    { L"Sapphire",     RGB(0x50, 0x82, 0xDC) },   // 10 — NEW: jewel blue
    { L"Royal Purple", RGB(0xA0, 0x5A, 0xD2) },   // 11 — NEW: mystic accent
};

void ApplyColorChange() {
    // Default state: original launcher gold (no preset chosen, or
    // saved idx is out of bounds — guards against a config that
    // points at a removed-since color slot).
    if (g_cfg.fontColorIdx < 0
        || g_cfg.fontColorIdx >= kNumColorPresets) {
        Tok::Gold       = GP(0xC8, 0xA8, 0x4B);
        Tok::GoldBright = GP(0xFF, 0xD7, 0x00);
    } else {
        const ColorPreset& cp = g_colorPresets[g_cfg.fontColorIdx];
        Gdiplus::Color c(255, GetRValue(cp.rgb), GetGValue(cp.rgb),
                              GetBValue(cp.rgb));
        Tok::Gold = c;
        // GoldBright = base + 0x30 per channel, clamped to 255.
        // Used for hover/selected highlights; lifting a fixed amount
        // above the base picks up brightness on the dark-color presets
        // without inventing a separate palette.
        BYTE r = (BYTE)min(255, GetRValue(cp.rgb) + 0x30);
        BYTE g = (BYTE)min(255, GetGValue(cp.rgb) + 0x30);
        BYTE b = (BYTE)min(255, GetBValue(cp.rgb) + 0x30);
        Tok::GoldBright = Gdiplus::Color(255, r, g, b);
    }
    if (g_hwMain) {
        RedrawWindow(g_hwMain, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
}
