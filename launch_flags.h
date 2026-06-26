// ═══════════════════════════════════════════════════════════════════════
//  launch_flags.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Per-mod launch flag system. The user toggles a fixed set of CLI
//  flags (Use Txts / Window / No Sound / etc.) in a 2x3 grid on the
//  Modding column. Each flag's state is stored in a ModSettings
//  struct, persisted per-mod by mod_config.
//
//  The FLAGS[] table binds each flag to a ModSettings member pointer,
//  its CLI argument, display label, description, and a locked-on
//  marker. BuildLaunchArgs walks the table in order to assemble the
//  -mod / -txt / -w / ... string for the loader.
//
//  Depends on:
//    mod_scan — for g_mods / g_selMod (BuildLaunchArgs reads the
//               selected mod's folder for the -mod arg)
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// Per-mod flag state. Persisted per-mod to <mod>\Launcher Files\
// launcher_mod_cfg.json by mod_config. Switching the selected mod
// triggers a Load that swaps these values out wholesale.
struct ModSettings {
    bool    noSound   = false;
    bool    windowed  = false;
    bool    useTxt    = true;     // locked on, see FLAGS table
    bool    skipIntro = false;
    bool    respec    = false;
    bool    resetMaps = false;
    bool    useSeed   = false;    // seed checkbox
    wstring seedArg;              // value the user typed/picked
};
extern ModSettings g_modSettings;

// One row of the FLAGS table. member binds to a ModSettings field
// pointer-to-member; arg is the CLI string ("-ns" etc.); isLocked
// flags fields that must stay true regardless of user input.
struct FlagDef {
    bool ModSettings::* member;
    const wchar_t* name;        // "No Sound"
    const wchar_t* arg;         // "-ns"
    const wchar_t* desc;        // "Disables all audio"
    bool isLocked;              // true → always on, cannot be toggled
};

// Number of flag rows. Defined here as a compile-time constant so
// callers can declare matching sized arrays and so the extern
// declaration of FLAGS below has a known size — which lets the
// existing  `for (const auto& f : FLAGS)`  and
// `sizeof(FLAGS)/sizeof(FLAGS[0])`  call sites work unchanged.
constexpr int kNumFlags = 6;

// Six entries. Iteration order controls both the visual grid layout
// (col = i%2, row = i/2) AND the order in which flags appear in the
// CLI string. Defined in launch_flags.cpp.
extern const FlagDef FLAGS[kNumFlags];

// Force any flag marked isLocked back to its required state. Called
// by LoadModSettings so an older saved config (which might have
// -txt off) gets corrected on load.
void EnforceLockedFlags();

// Build the CLI argument string for the currently selected mod
// (reads g_mods[g_selMod].folder). Returns "-mod NAME -txt -w …"
// in fixed arg order (which is independent of grid layout — see
// the .cpp for the exact sequence).
wstring BuildLaunchArgs();
