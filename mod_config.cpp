// ═══════════════════════════════════════════════════════════════════════
//  mod_config.cpp — see mod_config.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "mod_config.h"
#include "core.h"           // ReadTextFile, WriteTextFile, JsonBool, JsonStr, EscapeJson
#include "launch_flags.h"   // ModSettings, g_modSettings, EnforceLockedFlags

// Path helper — file-internal. The mod_cfg lives inside the mod's
// own Launcher Files subfolder so it travels with the mod when
// users back up / share their mod directories.
static wstring ModCfgPath(const ModInfo& mod) {
    return mod.launcherDir + L"\\launcher_mod_cfg.json";
}

void LoadModSettings(const ModInfo& mod) {
    wstring j = ReadTextFile(ModCfgPath(mod));
    if (j.empty()) {
        g_modSettings = ModSettings{};   // defaults
        EnforceLockedFlags();
        return;
    }
    g_modSettings.noSound     = JsonBool(j, L"no_sound",     false);
    g_modSettings.windowed    = JsonBool(j, L"windowed",     false);
    g_modSettings.useTxt      = JsonBool(j, L"use_txt",      true);
    g_modSettings.skipIntro   = JsonBool(j, L"skip_intro",   false);
    g_modSettings.respec      = JsonBool(j, L"respec",       false);
    g_modSettings.resetMaps   = JsonBool(j, L"reset_maps",   false);
    g_modSettings.useSeed     = JsonBool(j, L"use_seed",     false);
    g_modSettings.seedArg     = JsonStr (j, L"seed_arg");
    EnforceLockedFlags();
}

void SaveModSettings(const ModInfo& mod) {
    // Lazily create the Launcher Files subfolder
    CreateDirectoryW(mod.launcherDir.c_str(), nullptr);

    auto B = [](bool v) { return v ? L"true" : L"false"; };
    wstring j;
    j += L"{\n";
    j += wstring(L"  \"no_sound\":     ") + B(g_modSettings.noSound)   + L",\n";
    j += wstring(L"  \"windowed\":     ") + B(g_modSettings.windowed)  + L",\n";
    j += wstring(L"  \"use_txt\":      ") + B(g_modSettings.useTxt)    + L",\n";
    j += wstring(L"  \"skip_intro\":   ") + B(g_modSettings.skipIntro) + L",\n";
    j += wstring(L"  \"respec\":       ") + B(g_modSettings.respec)    + L",\n";
    j += wstring(L"  \"reset_maps\":   ") + B(g_modSettings.resetMaps) + L",\n";
    j += wstring(L"  \"use_seed\":     ") + B(g_modSettings.useSeed)   + L",\n";
    j += wstring(L"  \"seed_arg\":     \"") + EscapeJson(g_modSettings.seedArg) + L"\"\n";
    j += L"}";
    WriteTextFile(ModCfgPath(mod), j);
}
