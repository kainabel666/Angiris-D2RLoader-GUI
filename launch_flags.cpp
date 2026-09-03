// ═══════════════════════════════════════════════════════════════════════
//  launch_flags.cpp — see launch_flags.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "launch_flags.h"
#include "mod_scan.h"   // g_mods, g_selMod (for BuildLaunchArgs)

// Single shared instance — definition here, declaration in
// launch_flags.h so paint code and dialogs read the same fields.
ModSettings g_modSettings;

// v1.7: "Use Txts" is NO LONGER locked. It used to be forced on
// because D2RLoader.exe required -txt to launch a mod at all; that
// requirement is gone, so the flag is a normal toggle now (still
// defaulting to on — see ModSettings::useTxt). No flag is currently
// locked, but the isLocked mechanism is kept for future use.
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
    { &ModSettings::useTxt,    L"Use Txts",   L"-txt",             L"Use raw .txt data",         false },
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

// ═══════════════════════════════════════════════════════════════════════
//  TOML LAUNCH CONFIG (D2RLoader 1.1.0)
// ═══════════════════════════════════════════════════════════════════════
//
// 1.1.0 wants launch config in D2RLoader.toml rather than on the command
// line: `default_mod` selects the mod, `launch_arguments` supplies extra
// game args (and explicitly forbids -mod). Writing there instead of
// passing argv gives one source of truth, so the toml, our preview
// string and what the game actually receives can't disagree.
//
// The catch is that launch_arguments is user-editable and may hold flags
// we know nothing about. Replacing it wholesale would silently destroy
// those, so we MERGE: strip only the tokens this launcher owns, keep
// everything else, then append our current flag set.

// Flags we own — anything here is ours to rewrite on every launch.
// Kept separate from FLAGS[] because it also covers the value-taking
// args (-seed) and -mod, which isn't a grid flag at all.
static bool IsOwnedFlag(const wstring& tok) {
    for (const auto& f : FLAGS) {
        if (tok == f.arg) return true;
    }
    return tok == L"-seed" || tok == L"-mod";
}

// Args that consume the following token as their value.
static bool FlagTakesValue(const wstring& tok) {
    return tok == L"-seed" || tok == L"-mod";
}

// Explicit rather than iswspace() so this does not depend on <cwctype>
// being pulled in transitively. Command lines only ever separate on
// spaces and tabs anyway.
static bool IsArgSpace(wchar_t c) {
    return c == L' ' || c == L'\t';
}

wstring StripOwnedLaunchArgs(const wstring& existing) {
    wstring out;
    size_t i = 0;
    while (i < existing.size()) {
        while (i < existing.size() && IsArgSpace(existing[i])) ++i;
        if (i >= existing.size()) break;
        size_t start = i;
        while (i < existing.size() && !IsArgSpace(existing[i])) ++i;
        wstring tok = existing.substr(start, i - start);
        if (IsOwnedFlag(tok)) {
            // Drop it, plus its value if it takes one.
            if (FlagTakesValue(tok)) {
                while (i < existing.size() && IsArgSpace(existing[i])) ++i;
                while (i < existing.size() && !IsArgSpace(existing[i])) ++i;
            }
            continue;
        }
        if (!out.empty()) out += L' ';
        out += tok;
    }
    return out;
}

// The value to write to launch_arguments: the user's unknown flags
// first, then ours in the fixed order. -mod is deliberately absent —
// it goes to default_mod, and the toml forbids it here.
wstring BuildTomlLaunchArguments(const wstring& existing) {
    wstring out = StripOwnedLaunchArgs(existing);
    auto add = [&](const wstring& t) {
        if (!out.empty()) out += L' ';
        out += t;
    };
    // NOTE: respec / resetMaps / skipIntro are deliberately absent.
    // D2RLoader 1.1.0 has dedicated toml keys for all three
    // (enable_respec, always_generate_new_maps, skip_title_screen) and
    // the launcher writes those directly — see the Play handler. Adding
    // the command-line equivalents here as well would set the same
    // behaviour twice through two different mechanisms.
    if (g_modSettings.useTxt)    add(L"-txt");
    if (g_modSettings.windowed)  add(L"-w");
    if (g_modSettings.noSound)   add(L"-ns");
    // -seed stays last, per the per-mod seed feature spec.
    if (g_modSettings.useSeed && !g_modSettings.seedArg.empty()) {
        add(L"-seed");
        add(g_modSettings.seedArg);
    }
    return out;
}
