// ═══════════════════════════════════════════════════════════════════════
//  config.cpp — see config.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "config.h"
#include "core.h"   // AppDir, ReadTextFile, WriteTextFile, JsonStr/Bool/Int/Double, g_dpiScale

// Single shared instance. Definition lives here; declaration is in
// config.h so every other module can read through &g_cfg.
LauncherCfg g_cfg;

// Path helper — file-internal, not exposed in the header (no other
// module needs to know where the config file lives).
static wstring CfgPath() { return AppDir() + L"\\launcher_config.json"; }

void LoadCfg() {
    wstring j = ReadTextFile(CfgPath());
    g_cfg.d2rPath       = JsonStr(j, L"d2r_path");
    g_cfg.lastMod       = JsonStr(j, L"last_mod");
    g_cfg.toolsDir      = JsonStr(j, L"tools_dir");
    g_cfg.toolExcel     = JsonStr(j, L"tool_excel");
    g_cfg.toolStrings   = JsonStr(j, L"tool_strings");
    g_cfg.toolSprite    = JsonStr(j, L"tool_sprite");
    g_cfg.toolModels    = JsonStr(j, L"tool_models");
    g_cfg.toolTextures  = JsonStr(j, L"tool_textures");
    g_cfg.toolParticles = JsonStr(j, L"tool_particles");
    g_cfg.launchBehavior = JsonInt(j, L"launch_behavior", LB_MINIMIZE);
    if (g_cfg.launchBehavior < 0 || g_cfg.launchBehavior > LB_CLOSE)
        g_cfg.launchBehavior = LB_MINIMIZE;
    g_cfg.backupCount         = JsonInt (j, L"backup_count", 1);
    g_cfg.backupSaves         = JsonBool(j, L"backup_saves", true);
    g_cfg.backupSavesPrompted = JsonBool(j, L"backup_saves_prompted", false);
    if (g_cfg.backupCount < 0)  g_cfg.backupCount = 0;
    if (g_cfg.backupCount > 10) g_cfg.backupCount = 10;

    // UI scale. The toolbar cycling button exposes a fixed preset list
    // (see g_scalePresets); a value not in that list silently snaps to
    // the closest one so a typo in the config can't strand the user
    // with a window too small to click. wWinMain does an additional
    // narrow-to-active-DPI-subset snap after computing g_dpiScale.
    g_cfg.uiScale = JsonDouble(j, L"ui_scale", 1.00);
    {
        constexpr double presets[] = { 0.75, 0.85, 1.00, 1.15, 1.275 };
        double bestDist = 1e9;
        double best = 1.00;
        for (double p : presets) {
            double d = (p > g_cfg.uiScale) ? (p - g_cfg.uiScale) : (g_cfg.uiScale - p);
            if (d < bestDist) { bestDist = d; best = p; }
        }
        g_cfg.uiScale = best;
    }

    // Font / color selections.
    g_cfg.fontName     = JsonStr(j, L"font_name");
    g_cfg.fontColorIdx = JsonInt(j, L"font_color", -1);
    if (g_cfg.fontColorIdx < -1 || g_cfg.fontColorIdx >= 8)
        g_cfg.fontColorIdx = -1;

    // Launcher self-update: tag the user told us to skip. Empty means
    // they haven't skipped anything yet.
    g_cfg.skippedLauncherVersion = JsonStr(j, L"skipped_launcher_version");

    // Last-known system DPI scale (1.0 / 1.5 / etc.). Compared against
    // the live g_dpiScale by wWinMain after it computes it — when the
    // user changes Windows scaling between sessions, uiScale snaps back
    // to 1.0 so the launcher reopens at a known-good size.
    g_cfg.lastDpiScale = JsonDouble(j, L"last_dpi_scale", 1.0);
}

void SaveCfg() {
    wstring j;
    j += L"{\n";
    j += L"  \"d2r_path\":             \"" + EscapeJson(g_cfg.d2rPath)       + L"\",\n";
    j += L"  \"last_mod\":             \"" + EscapeJson(g_cfg.lastMod)       + L"\",\n";
    j += L"  \"tools_dir\":            \"" + EscapeJson(g_cfg.toolsDir)      + L"\",\n";
    j += L"  \"tool_excel\":           \"" + EscapeJson(g_cfg.toolExcel)     + L"\",\n";
    j += L"  \"tool_strings\":         \"" + EscapeJson(g_cfg.toolStrings)   + L"\",\n";
    j += L"  \"tool_sprite\":          \"" + EscapeJson(g_cfg.toolSprite)    + L"\",\n";
    j += L"  \"tool_models\":          \"" + EscapeJson(g_cfg.toolModels)    + L"\",\n";
    j += L"  \"tool_textures\":        \"" + EscapeJson(g_cfg.toolTextures)  + L"\",\n";
    j += L"  \"tool_particles\":       \"" + EscapeJson(g_cfg.toolParticles) + L"\",\n";
    wchar_t buf[16];
    swprintf(buf, 16, L"%d", g_cfg.launchBehavior);
    j += wstring(L"  \"launch_behavior\":      ") + buf + L",\n";
    swprintf(buf, 16, L"%d", g_cfg.backupCount);
    j += wstring(L"  \"backup_count\":         ") + buf + L",\n";
    j += wstring(L"  \"backup_saves\":         ") + (g_cfg.backupSaves ? L"true" : L"false") + L",\n";
    j += wstring(L"  \"backup_saves_prompted\":") + (g_cfg.backupSavesPrompted ? L" true" : L" false") + L",\n";
    // ui_scale: persisted as the actual decimal value (0.85, 1.00, etc.).
    swprintf(buf, 16, L"%.3f", g_cfg.uiScale);
    j += wstring(L"  \"ui_scale\":             ") + buf + L",\n";
    // font_name: persisted as the chosen face name (filename without
    // extension, as enumerated from assets/fonts/). Empty = default.
    j += L"  \"font_name\":            \"" + EscapeJson(g_cfg.fontName) + L"\",\n";
    // font_color: index into g_colorPresets[], -1 = default Gold.
    swprintf(buf, 16, L"%d", g_cfg.fontColorIdx);
    j += wstring(L"  \"font_color\":           ") + buf + L",\n";
    // skipped_launcher_version: tag the user clicked Skip on, so the
    // self-update dialog doesn't re-prompt until a newer release
    // comes along.
    j += L"  \"skipped_launcher_version\": \""
       + EscapeJson(g_cfg.skippedLauncherVersion) + L"\",\n";
    // last_dpi_scale: live g_dpiScale at save time. Used to detect a
    // between-sessions DPI change on next LoadCfg.
    swprintf(buf, 16, L"%.3f", g_dpiScale);
    j += wstring(L"  \"last_dpi_scale\":       ") + buf + L"\n";
    j += L"}";
    WriteTextFile(CfgPath(), j);
}
