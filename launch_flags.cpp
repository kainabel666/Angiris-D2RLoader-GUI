// ═══════════════════════════════════════════════════════════════════════
//  launch_flags.cpp — see launch_flags.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "launch_flags.h"
#include "mod_scan.h"   // g_mods, g_selMod (for BuildLaunchArgs)

// Single shared instance — definition here, declaration in
// launch_flags.h so paint code and dialogs read the same fields.
ModSettings g_modSettings;

// "Use Txts" is locked on for now: the current D2RLoader.exe
// requires it. Toggle this flag back to false once the loader
// supports running without -txt.
//
// Order matters: this controls BOTH the visual layout of the 2x3
// grid AND the order in which flags appear in the launch-args
// string. The grid fills left-to-right, top-to-bottom
// (col = i%2, row = i/2):
//
//   [0: Use Txts  ] [1: Respec     ]   <- top row
//   [2: Window    ] [3: Reset Maps ]   <- middle row
//   [4: No Sound  ] [5: Skip Intro ]   <- bottom row
//
// BuildLaunchArgs walks this array in order, so the cmd preview reads:
//   -mod MODNAME -txt -enablerespec -w -resetofflinemaps -ns -skiplogovideo -seed VALUE
// (-seed is appended last by BuildLaunchArgs from g_modSettings.seedArg
//  when non-empty; the seed UI lives in its own row below the flag
//  grid.)
const FlagDef FLAGS[kNumFlags] = {
    { &ModSettings::useTxt,    L"Use Txts",   L"-txt",             L"Use raw .txt data",         true  },
    { &ModSettings::respec,    L"Respec",     L"-enablerespec",    L"Allow free skill respec",   false },
    { &ModSettings::windowed,  L"Window",     L"-w",               L"Run in a window",           false },
    { &ModSettings::resetMaps, L"Reset Maps", L"-resetofflinemaps",L"Re-roll all map seeds",     false },
    { &ModSettings::noSound,   L"No Sound",   L"-ns",              L"Disables all audio",        false },
    { &ModSettings::skipIntro, L"Skip Intro", L"-skiplogovideo",   L"Skip the intro videos",     false },
};

void EnforceLockedFlags() {
    for (const auto& f : FLAGS) {
        if (f.isLocked) g_modSettings.*(f.member) = true;
    }
}

wstring BuildLaunchArgs() {
    wstring args;
    if (g_selMod >= 0 && g_selMod < (int)g_mods.size())
        args = L"-mod " + g_mods[g_selMod].folder;
    // Arg order is fixed and independent of the checkbox grid layout:
    //   -mod NAME -txt -w -ns -enablerespec -resetofflinemaps -skiplogovideo -seed VALUE
    // -seed sits LAST per the per-mod seed feature spec.
    if (g_modSettings.useTxt)    args += L" -txt";
    if (g_modSettings.windowed)  args += L" -w";
    if (g_modSettings.noSound)   args += L" -ns";
    if (g_modSettings.respec)    args += L" -enablerespec";
    if (g_modSettings.resetMaps) args += L" -resetofflinemaps";
    if (g_modSettings.skipIntro) args += L" -skiplogovideo";
    if (g_modSettings.useSeed && !g_modSettings.seedArg.empty())
        args += L" -seed " + g_modSettings.seedArg;
    return args;
}
