// ═══════════════════════════════════════════════════════════════════════
//  scaling.cpp — see scaling.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "scaling.h"
#include "core.h"     // g_dpiScale
#include "config.h"   // g_cfg.uiScale

// Storage for the extern declarations in scaling.h. Initial values
// are set so a paint that runs BEFORE wWinMain's scale init still
// produces visible output (it just won't honor the user's saved
// uiScale until LoadCfg + the user-scale assignment have run).
double g_userScale = 0.85;
double g_scale     = 0.85;

const ScalePreset g_scalePresets[kNumScalePresets] = {
    { L"75%",  0.75  },
    { L"85%",  0.85  },
    { L"100%", 1.00  },
    { L"115%", 1.15  },
    { L"127%", 1.275 },
};

void ActiveScalePresets(int& a, int& b, int& c) {
    if (g_dpiScale >= 1.25) { a = 0; b = 1; c = 2; }   // 75 / 85 / 100
    else                    { a = 2; b = 3; c = 4; }   // 100 / 115 / 127
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
