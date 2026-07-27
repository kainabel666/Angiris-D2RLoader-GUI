// ═══════════════════════════════════════════════════════════════════════
//  scaling.cpp — see scaling.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "scaling.h"
#include "core.h"     // g_dpiScale
#include "config.h"   // g_cfg.uiScale
#include "layout.h"   // LO::WIN_W / WIN_H — the window's logical design size

// Storage for the extern declarations in scaling.h. Initial values
// are set so a paint that runs BEFORE wWinMain's scale init still
// produces visible output (it just won't honor the user's saved
// uiScale until LoadCfg + the user-scale assignment have run).
double g_userScale = 0.85;
double g_scale     = 0.85;

// Largest userScale whose window still fits the current monitor. 1.0
// until UpdateScreenHeadroom runs (early paints are conservative).
double g_screenHeadroom = 1.0;

const ScalePreset g_scalePresets[kNumScalePresets] = {
    { L"50%",  0.50  },
    { L"65%",  0.65  },
    { L"75%",  0.75  },
    { L"85%",  0.85  },
    { L"100%", 1.00  },
    { L"115%", 1.15  },
    { L"127%", 1.275 },
};

void UpdateScreenHeadroom(int workW, int workH) {
    if (workW <= 0 || workH <= 0 || g_dpiScale <= 0.0) {
        g_screenHeadroom = 1.0;
        return;
    }
    // The window is LO::WIN_W x LO::WIN_H LOGICAL pixels, drawn at
    // (userScale * dpiScale). It fits when both axes fit, so the largest
    // usable userScale is the smaller of the two ratios.
    double hw = (double)workW / ((double)LO::WIN_W * g_dpiScale);
    double hh = (double)workH / ((double)LO::WIN_H * g_dpiScale);
    g_screenHeadroom = (hw < hh) ? hw : hh;
}

void ActiveScalePresets(int& a, int& b, int& c) {
    // DPI picks the band; headroom only vetoes it.
    //
    // These are two different questions and they must not be conflated.
    // The Windows DPI setting is the user's stated PREFERENCE for how big
    // things should be — someone on 150% has asked for 1.5x. Headroom is
    // only a FEASIBILITY limit: whether the screen can physically show it.
    //
    // Selecting purely on headroom (as this briefly did) ignores the
    // preference: a 4K display at 150% has headroom 1.38, so it would be
    // offered 100/115/127 and open at a 2937x1958 window — it fits, but
    // it's the opposite of the "make things bigger" the user asked for.
    //
    // So: start from the DPI band, then shift DOWN only while the band's
    // largest stop cannot fit. That keeps 4K@150% on 75/85/100 while still
    // rescuing 1080p@150%, where even 75% overflows and the band has to
    // slide to 50/65/75.
    //
    // `start` is the index of the band's smallest stop; the table is
    //   0:50  1:65  2:75  3:85  4:100  5:115  6:127
    int start;
    if      (g_dpiScale >= 1.50) start = 2;   //  75 /  85 / 100
    else if (g_dpiScale >= 1.25) start = 3;   //  85 / 100 / 115
    else                         start = 4;   // 100 / 115 / 127

    while (start > 0 && g_scalePresets[start + 2].mul > g_screenHeadroom) {
        --start;
    }

    a = start;
    b = start + 1;
    c = start + 2;
}

int ScaleToggleState() {
    int a, b, c;
    ActiveScalePresets(a, b, c);
    int presetIdx[3] = { a, b, c };
    for (int i = 0; i < 3; ++i) {
        if (g_scalePresets[presetIdx[i]].mul == g_cfg.uiScale) return i;
    }
    return 0;
}
