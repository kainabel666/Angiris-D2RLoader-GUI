// ═══════════════════════════════════════════════════════════════════════
//  loader_options_modal.cpp — Basic Options modal
// ═══════════════════════════════════════════════════════════════════════
//
//  Themed popup that exposes the six Basic settings D2RLoader reads at
//  launch. Rows are a mix of two-state toggles (booleans) and int
//  dropdowns. Each change writes immediately to D2RLoader.toml through
//  the SaveTomlBool / SaveTomlInt helpers exported by Angiris.cpp.
//
//  Paint approach mirrors the plugin manager popup — stone bg + ornate
//  frame + owner-drawn Close button — but the row painting is inline
//  here (no listbox) because six fixed rows don't warrant a scrollable
//  control.

#include "loader_options_modal.h"
#include "core.h"          // g_hInst, g_dpiScale
#include "config.h"        // g_cfg (for LoaderTomlPath via extern below)
#include "scaling.h"       // S(), SF()
#include "colors.h"        // Tok::Gold, Tok::BgPanel, etc.
#include "fonts.h"         // g_fBtn, g_fNavSm, g_fModName
#include "assets.h"        // AssetImage, DrawButton9Slice
#include "buttons.h"       // MkStdBtn, PaintOwnerDrawButton, ButtonKind
#include "ui_state.h"      // g_loaderOpts
#include "mod_scan.h"      // v1.6.2: g_mods / g_selMod (override-toml detection)

#include <cstdlib>         // _wtoi

// Toml write helpers live in Angiris.cpp (which owns g_cfg). Declaring
// them extern here keeps this file free of a shared header.
extern void SaveTomlBool(const wchar_t* section, const wchar_t* key, bool v);
extern void SaveTomlInt (const wchar_t* section, const wchar_t* key, int  v);
extern void SaveTomlStr (const wchar_t* section, const wchar_t* key,
                         const wchar_t* v);

using namespace Gdiplus;

// ─────────────────────────────────────────────────────────────────────
//  Layout constants (logical pixels)
// ─────────────────────────────────────────────────────────────────────

constexpr int BO_W                 = 440;   // 400 → 440: string dropdowns need a wider value box
constexpr int BO_TITLE_H           = 32;    // was 40
constexpr int BO_TITLE_TOP_PAD     = 8;     // was 12
constexpr int BO_TITLE_BOT_PAD     = 4;     // was 8
constexpr int BO_ROW_H             = 32;
constexpr int BO_ROW_H_TALL        = 44;    // was 48 — rows with `helper` text
constexpr int BO_ROW_LABEL_INSET_L = 20;
constexpr int BO_ROW_VALUE_INSET_R = 20;    // was 40 — controls sit closer to right edge
constexpr int BO_ROW_VALUE_BOX_W   = 70;
// StrDropdown rows show a short token ("Default", "enUS", "3.2") rather
// than a number, so their chrome is wider. The long human-readable name
// lives in the popup menu, not the box — that keeps the box from having
// to fit "Spanish (Latin America)".
constexpr int BO_ROW_VALUE_BOX_W_STR = 96;
constexpr int BO_VALUE_BOX_H       = 28;    // fixed; vertically centered
constexpr int BO_SLIDER_W          = 53;
constexpr int BO_SLIDER_H          = 23;
constexpr int BO_ROW_TO_BTN_GAP    = 12;    // was 20
constexpr int BO_BTN_W             = 140;
constexpr int BO_BTN_H             = 50;
constexpr int BO_BTN_BOTTOM_PAD    = 12;    // was 16

// Item dimensions inside the int-dropdown popup menu (logical pixels).
constexpr int BO_MENU_ITEM_W = 80;
constexpr int BO_MENU_ITEM_H = 28;
// String dropdowns carry full language names, so their menu is wider.
constexpr int BO_MENU_STR_W  = 232;

// Was 16, which silently truncated: ShowLoaderOptionsModal clamps
// rowCount to this and rows past the cap simply never render. D2RLoader
// 1.1.0 pushes Basic to 20 rows and Developer to 18, so this must stay
// comfortably ahead of both row tables.
constexpr int BO_MAX_ROWS = 24;

// ── Scrolling ────────────────────────────────────────────────────────
// The 1.1.0 row tables overflow any sane window height (Basic alone is
// ~938 logical px), so the row band between the title and the button
// row becomes a scrollable viewport with a themed gutter down its right
// edge. Same asset family and geometry rules as the mod list's
// scrollbar (see mod_list.cpp) so the two read as the same control.
constexpr int BO_SB_W         = 30;   // gutter width (asset native)
constexpr int BO_SB_PAD_R     = 4;    // gap from the modal's inner edge
constexpr int BO_SB_ROW_GAP   = 8;    // gap between rows and the gutter
constexpr int BO_SB_THUMB_W   = 15;   // thumb asset native width
constexpr int BO_SB_MIN_THUMB = 40;   // floor for the proportional thumb
constexpr int BO_SB_THUMB_CAP = 16;   // vertical 3-slice cap
constexpr int BO_SB_UP_H_FB   = 35;   // arrow-cap height fallbacks
constexpr int BO_SB_DOWN_H_FB = 32;
// Fraction of the monitor work area the modal may occupy before it
// starts scrolling instead of growing.
constexpr int BO_MAX_H_PERCENT = 88;
// Absolute ceiling in LOGICAL px. The work-area percentage alone isn't
// enough: on a tall monitor 88% is far more than the row tables need,
// so Basic simply grew to its full ~938 logical height and never
// scrolled. This keeps the modal a sane shape regardless of how much
// desktop is available — the smaller of the two limits wins.
constexpr int BO_MAX_H_LOGICAL = 620;
// Height of the mod-override notice strip under the title, when shown.
// Sits OUTSIDE the scrolling viewport so it stays visible while the
// user scrolls the rows it's warning about.
constexpr int BO_OVERRIDE_H = 26;

// ─────────────────────────────────────────────────────────────────────
//  Row descriptor
// ─────────────────────────────────────────────────────────────────────

enum class BoKind { Toggle, IntDropdown, IntTextBox, StrDropdown };

struct BoRow {
    BoKind         kind;
    const wchar_t* label;
    bool*          boolTarget;        // Toggle only
    int*           intTarget;         // IntDropdown / IntTextBox only
    int            minValue;          // Int* only, inclusive
    int            maxValue;          // Int* only, inclusive
    const wchar_t* tomlSection;
    const wchar_t* tomlKey;
    // Cascade: -1 = independent. Otherwise = index of a Toggle master
    // row in this same list; when the master's boolTarget is false, this
    // row is greyed out and clicks are ignored.
    int            cascadedFrom;
    // Visual: true = label starts a bit further to the right, so nested
    // groups (like the log-detail toggles under Enable Logging) read as
    // subordinate.
    bool           indent;
    // Optional helper text drawn under the label in dim gold. Rows with
    // a helper are rendered taller (BO_ROW_H_TALL) so the two-line block
    // has breathing room.
    const wchar_t* helper;

    // ── StrDropdown only ─────────────────────────────────────────────
    // These trail the struct so every pre-existing row initializer
    // (which stops at `helper`) still compiles — omitted trailing
    // aggregate members are value-initialized to nullptr / 0.
    //
    // strValues[] are the literal strings written to the toml.
    // strShort[] are what the value box shows (must stay short).
    // strLong[]  are what the popup menu shows; nullptr falls back to
    //            strShort. All three arrays are strCount long.
    wstring*              strTarget;
    const wchar_t* const* strValues;
    const wchar_t* const* strShort;
    const wchar_t* const* strLong;
    int                   strCount;
};

// ── StrDropdown value tables ─────────────────────────────────────────
// Ruleset pickers: D2RLoader 1.1.0 lets the Aura Enchanted and Bind
// Demon curse rules follow either the 3.1 or 3.2 patch behaviour.
static const wchar_t* const kRulesetValues[] = { L"3.1", L"3.2" };
static const wchar_t* const kRulesetShort [] = { L"3.1", L"3.2" };
static const wchar_t* const kAuraLong     [] = {
    L"3.1  —  Might / Holy Fire / Blessed Aim / Holy Freeze / Conviction / Fanaticism / Holy Shock",
    L"3.2  —  Concentration / Vigor / Thorns / Holy Freeze / Fanaticism",
};
static const wchar_t* const kCurseLong    [] = {
    L"3.1  —  MonUMod 34: 75% chance to cast Amplify Damage",
    L"3.2  —  MonUMod 35: 5% chance to cast Amplify Damage",
};

// Locale pickers. Empty string = "use the game's normal language",
// which is the toml's documented default for both text and audio.
static const wchar_t* const kLocaleValues[] = {
    L"",    L"enUS", L"deDE", L"esES", L"frFR", L"itIT", L"koKR",
    L"plPL", L"ruRU", L"zhCN", L"zhTW", L"esMX", L"jaJP", L"ptBR",
};
static const wchar_t* const kLocaleShort[] = {
    L"Default", L"enUS", L"deDE", L"esES", L"frFR", L"itIT", L"koKR",
    L"plPL", L"ruRU", L"zhCN", L"zhTW", L"esMX", L"jaJP", L"ptBR",
};
static const wchar_t* const kLocaleLong[] = {
    L"Game Default",
    L"enUS  —  English",
    L"deDE  —  German",
    L"esES  —  Spanish (Spain)",
    L"frFR  —  French",
    L"itIT  —  Italian",
    L"koKR  —  Korean",
    L"plPL  —  Polish",
    L"ruRU  —  Russian",
    L"zhCN  —  Chinese (Simplified)",
    L"zhTW  —  Chinese (Traditional)",
    L"esMX  —  Spanish (Latin America)",
    L"jaJP  —  Japanese",
    L"ptBR  —  Portuguese (Brazil)",
};
constexpr int kLocaleCount = (int)(sizeof(kLocaleValues) / sizeof(kLocaleValues[0]));

// Index of the row's current toml value within strValues, or -1 if the
// file holds something we don't recognise (hand-edited). -1 is not an
// error: the box falls back to showing the raw string and no menu item
// is checked, so an unknown value survives untouched unless the user
// actively picks a new one.
static int StrRowIndex(const BoRow& r) {
    if (!r.strTarget || !r.strValues) return -1;
    for (int i = 0; i < r.strCount; ++i) {
        if (*r.strTarget == r.strValues[i]) return i;
    }
    return -1;
}

// Basic Options — 20 rows covering [d2rcore.*], the user-facing
// [d2rloader] keys, [d2rloader.backups] and the two extension switches
// from [d2rloader.advanced].
//
// Deliberately absent: default_mod (the Play handler writes it from the
// mod picker, so a second control here would be a competing source of
// truth), skip_title_screen (forced off by EnforceLoaderTomlOwnership
// every run, so a control would be a lie) and launch_arguments (the
// Play handler merges the flag grid into it — see
// BuildTomlLaunchArguments).
//
// Stash Tabs is a text box, not a dropdown: 1.1.0 ships 100 by default
// and a 100-item TrackPopupMenu is unusable.
static BoRow g_boRowsBasic[] = {
    // ── [d2rcore.game_rules] ──
    { BoKind::StrDropdown, L"Aura Enchanted",
      nullptr, nullptr, 0, 0,
      L"d2rcore.game_rules", L"aura_enchanted_selection", -1, false,
      L"Which patch's aura pool Bind Demon pets draw from",
      &g_loaderOpts.auraEnchantedSelection,
      kRulesetValues, kRulesetShort, kAuraLong, 2 },
    { BoKind::StrDropdown, L"Bind Demon Curse",
      nullptr, nullptr, 0, 0,
      L"d2rcore.game_rules", L"bind_demon_curse_selection", -1, false,
      L"Amplify Damage proc rate on converted Cursed demons",
      &g_loaderOpts.bindDemonCurseSelection,
      kRulesetValues, kRulesetShort, kCurseLong, 2 },

    // ── [d2rcore.items] ──
    { BoKind::Toggle,      L"Show Sockets",
      &g_loaderOpts.showGroundSockets, nullptr, 0, 0,
      L"d2rcore.items",  L"show_ground_sockets",  -1, false, nullptr },
    { BoKind::Toggle,      L"Show Item Level",
      &g_loaderOpts.displayItemLevels, nullptr, 0, 0,
      L"d2rcore.items",  L"display_item_levels",  -1, false, nullptr },
    { BoKind::Toggle,      L"Show Stat Ranges",
      &g_loaderOpts.itemStatRanges,    nullptr, 0, 0,
      L"d2rcore.items",  L"item_stat_ranges",     -1, false,
      L"Hold Ctrl or right trigger to view" },
    { BoKind::Toggle,      L"Show Max Sockets",
      &g_loaderOpts.maximumSockets,    nullptr, 0, 0,
      L"d2rcore.items",  L"maximum_sockets",      -1, false, nullptr },

    // ── [d2rcore.player] ──
    { BoKind::Toggle,      L"Respec Skill/Stats",
      &g_loaderOpts.enableRespec,      nullptr, 0, 0,
      L"d2rcore.player", L"enable_respec",        -1, false, nullptr },
    { BoKind::Toggle,      L"RotW Renderer Key",
      &g_loaderOpts.alwaysEnableRotwLegacyKeybind, nullptr, 0, 0,
      L"d2rcore.player", L"always_enable_rotw_legacy_graphics_keybind",
      -1, false, L"Keep Toggle Renderer bound on RotW characters" },

    // ── [d2rcore.stash] ──
    { BoKind::IntTextBox,  L"Stash Tabs",
      nullptr, &g_loaderOpts.addSharedTabs,     0, 100,
      L"d2rcore.stash",  L"add_shared_tabs",      -1, false,
      L"Extra shared tabs. Default = 100" },
    { BoKind::IntTextBox,  L"Material Limit",
      nullptr, &g_loaderOpts.setMaterialsLimit, 0, 255,
      L"d2rcore.stash",  L"set_materials_limit",  -1, false,
      L"Default = 99, Max = 255" },

    // ── [d2rloader] ──
    { BoKind::Toggle,      L"Show TCP/IP Button",
      &g_loaderOpts.showTcpipButton,   nullptr, 0, 0,
      L"d2rloader",      L"show_tcpip_button",    -1, false, nullptr },
    { BoKind::Toggle,      L"Check For Updates",
      &g_loaderOpts.checkForUpdates,   nullptr, 0, 0,
      L"d2rloader",      L"check_for_updates",    -1, false,
      L"D2RLoader's own update check" },
    { BoKind::Toggle,      L"New Maps Each Load",
      &g_loaderOpts.alwaysGenerateNewMaps, nullptr, 0, 0,
      L"d2rloader",      L"always_generate_new_maps", -1, false,
      L"Off keeps each character's map layout" },
    { BoKind::StrDropdown, L"Text Language",
      nullptr, nullptr, 0, 0,
      L"d2rloader",      L"text_locale",          -1, false, nullptr,
      &g_loaderOpts.textLocale,
      kLocaleValues, kLocaleShort, kLocaleLong, kLocaleCount },
    { BoKind::StrDropdown, L"Audio Language",
      nullptr, nullptr, 0, 0,
      L"d2rloader",      L"audio_locale",         -1, false, nullptr,
      &g_loaderOpts.audioLocale,
      kLocaleValues, kLocaleShort, kLocaleLong, kLocaleCount },

    // ── [d2rloader.backups] — index 15 is the cascade master ──
    { BoKind::Toggle,      L"Character Backups",
      &g_loaderOpts.backupsEnabled,    nullptr, 0, 0,
      L"d2rloader.backups", L"enabled",           -1, false,
      L"Backs up before the first save each session" },
    { BoKind::IntTextBox,  L"Backups Kept",
      nullptr, &g_loaderOpts.retainedSessions,  1, 100,
      L"d2rloader.backups", L"retained_sessions", 15, true,
      L"Per character. Range 1-100" },
    { BoKind::Toggle,      L"Back Up Shared Stash",
      &g_loaderOpts.backupSharedStashes, nullptr, 0, 0,
      L"d2rloader.backups", L"shared_stashes",    15, true, nullptr },

    // ── [d2rloader.advanced] — the two extension gates ──
    { BoKind::Toggle,      L"Allow Global Plugins",
      &g_loaderOpts.allowGlobalExtensions, nullptr, 0, 0,
      L"d2rloader.advanced", L"allow_global_extensions", -1, false,
      L"Off stops <game>\\d2rloader plugins loading" },
    { BoKind::Toggle,      L"Allow Mod Plugins",
      &g_loaderOpts.allowModExtensions, nullptr, 0, 0,
      L"d2rloader.advanced", L"allow_mod_extensions",    -1, false,
      L"Off stops the mod's own plugins loading" },
};

// Developer Options — 3 top-level toggles then 14 cascaded log-detail
// toggles that grey out when Enable Logging (index 3, the master) is off.
//
// write_crash_dumps lives in [d2rloader.advanced] rather than
// [d2rloader.developer], but it's a diagnostic the toml itself says to
// enable only when reporting a crash, so it sits here with the other
// developer switches instead of in Basic. Moving it is a one-line
// change if that split reads wrong.
static BoRow g_boRowsDev[] = {
    { BoKind::Toggle, L"Enable Console",
      &g_loaderOpts.enableConsole,    nullptr, 0, 0,
      L"d2rloader.developer", L"enable_console",     -1, false,
      L"Ctrl + ` in game" },
    { BoKind::Toggle, L"Assert Dialog Message",
      &g_loaderOpts.assertDialogMode, nullptr, 0, 0,
      L"d2rloader.developer", L"assert_dialog_mode", -1, false, nullptr },
    { BoKind::Toggle, L"Crash Dumps",
      &g_loaderOpts.writeCrashDumps,  nullptr, 0, 0,
      L"d2rloader.advanced",  L"write_crash_dumps",  -1, false,
      L"Only for reproducible crash reports" },
    { BoKind::Toggle, L"Enable Logging",
      &g_loaderOpts.logsEnabled,      nullptr, 0, 0,
      L"d2rloader.developer.logs", L"enabled",       -1, false, nullptr },
    { BoKind::Toggle, L"Blizzard Diagnostics",
      &g_loaderOpts.logNativeBlizzard, nullptr, 0, 0,
      L"d2rloader.developer.logs", L"native_blizzard", 3, true, nullptr },
    { BoKind::Toggle, L"JSON Resources",
      &g_loaderOpts.logJsonResources, nullptr, 0, 0,
      L"d2rloader.developer.logs", L"json_resources", 3, true,  nullptr },
    { BoKind::Toggle, L"Widget Panel Creation",
      &g_loaderOpts.logWidgetPanels,  nullptr, 0, 0,
      L"d2rloader.developer.logs", L"widget_panels",  3, true,  nullptr },
    { BoKind::Toggle, L"Excel File Loaded",
      &g_loaderOpts.logExcelFiles,    nullptr, 0, 0,
      L"d2rloader.developer.logs", L"excel_files",    3, true,  nullptr },
    { BoKind::Toggle, L"BIN Validation",
      &g_loaderOpts.logBinValidation, nullptr, 0, 0,
      L"d2rloader.developer.logs", L"bin_validation", 3, true,  nullptr },
    { BoKind::Toggle, L"True and Open Type Fonts",
      &g_loaderOpts.logFonts,         nullptr, 0, 0,
      L"d2rloader.developer.logs", L"fonts",          3, true,  nullptr },
    { BoKind::Toggle, L"UI Sprites Creation",
      &g_loaderOpts.logSprites,       nullptr, 0, 0,
      L"d2rloader.developer.logs", L"sprites",        3, true,  nullptr },
    { BoKind::Toggle, L"Chat Messages",
      &g_loaderOpts.logChatMessages,  nullptr, 0, 0,
      L"d2rloader.developer.logs", L"chat_messages",  3, true,  nullptr },
    { BoKind::Toggle, L"Models Creation",
      &g_loaderOpts.logModels,        nullptr, 0, 0,
      L"d2rloader.developer.logs", L"models",         3, true,  nullptr },
    { BoKind::Toggle, L"Extension Details",
      &g_loaderOpts.logExtensionDetails, nullptr, 0, 0,
      L"d2rloader.developer.logs", L"extension_details", 3, true, nullptr },
    { BoKind::Toggle, L"Character Environment",
      &g_loaderOpts.logCharacterEnv,  nullptr, 0, 0,
      L"d2rloader.developer.logs", L"character_environment", 3, true, nullptr },
    { BoKind::Toggle, L"Archive Mounts",
      &g_loaderOpts.logArchiveMounts, nullptr, 0, 0,
      L"d2rloader.developer.logs", L"archive_mounts", 3, true,  nullptr },
    { BoKind::Toggle, L"CASC Fetch Decisions",
      &g_loaderOpts.logCascFetchDecisions, nullptr, 0, 0,
      L"d2rloader.developer.logs", L"casc_fetch_decisions", 3, true,
      L"Very noisy" },
    { BoKind::Toggle, L"CASC File Loading",
      &g_loaderOpts.logCascFiles,     nullptr, 0, 0,
      L"d2rloader.developer.logs", L"casc_files",     3, true,
      L"Very noisy" },
};

// ─────────────────────────────────────────────────────────────────────
//  State (file-static)
// ─────────────────────────────────────────────────────────────────────

static HWND g_boHwnd     = nullptr;
static HWND g_boCloseBtn = nullptr;
static bool g_boClassReg = false;

// ── Scroll state (physical pixels) ───────────────────────────────────
// g_boScrollY is the offset applied to every row rect. g_boContentH is
// the summed height of all rows; when it exceeds the viewport the
// gutter appears and g_boScrollable goes true. Reset on WM_DESTROY.
static int  g_boScrollY    = 0;
static int  g_boContentH   = 0;
static bool g_boScrollable = false;
// Thumb drag: physical mouse-Y minus thumb top at grab time.
static bool g_boSbDragging = false;
static int  g_boSbGrabDY   = 0;

// One EDIT HWND per row (only populated for IntTextBox kind). Created
// with the modal, destroyed with it; hosted directly on the modal so
// keyboard focus lands there when the user tabs or clicks in.
static HWND g_boRowEdits[BO_MAX_ROWS] = { nullptr };

// EDIT control IDs. Base 100 leaves 1 for the Close button and lets
// EN_KILLFOCUS route the row index back to us via LOWORD(wparam).
constexpr int BO_EDIT_ID_BASE = 100;

// Which row list is bound to the currently-open modal. Swapped in by
// ShowBasicOptionsModal / ShowDeveloperOptionsModal before the pump
// starts; cleared on WM_DESTROY. Paint + hit-test code reads these.
static const BoRow*  g_activeRows     = nullptr;
static int           g_activeRowCount = 0;
static const wchar_t* g_activeTitle   = L"";
// v1.6.2: set when the active mod ships its own D2RLoader.toml. Only
// Basic Options cares — see BoDetectModOverride.
static bool    g_boModOverride     = false;
static wstring g_boModOverrideName;

// Popup-menu state for the active int-dropdown click. Only the row
// index (into the active row list) needs to survive across
// TrackPopupMenu since the WM_DRAWITEM / WM_MEASUREITEM callbacks fire
// during the modal blocking call. -1 = no menu open.
static int  g_boOpenMenuRow = -1;

// True if row `i` in the active list is currently inert because its
// master toggle (BoRow::cascadedFrom) is off. Disabled rows paint dim
// and ignore clicks.
static bool RowIsDisabled(int i) {
    if (!g_activeRows) return false;
    if (i < 0 || i >= g_activeRowCount) return false;
    const BoRow& r = g_activeRows[i];
    if (r.cascadedFrom < 0) return false;
    if (r.cascadedFrom >= g_activeRowCount) return false;
    const BoRow& master = g_activeRows[r.cascadedFrom];
    return !(master.boolTarget && *master.boolTarget);
}

// Invalidate just the affected rows so WM_PAINT's HDC clip rect limits
// pixel updates to the touched area. Defined after RowPhysRect (below)
// because it depends on it.
static void InvalidateRowRange(HWND hw, int firstRow, int lastRow);

// ─────────────────────────────────────────────────────────────────────
//  Row-Y computation
// ─────────────────────────────────────────────────────────────────────

// Returns the logical height of row `i`. Rows with `helper` text are
// taller so the two-line label/helper block has breathing room.
static int RowLogicalHeight(int i) {
    if (!g_activeRows) return BO_ROW_H;
    if (i < 0 || i >= g_activeRowCount) return BO_ROW_H;
    return g_activeRows[i].helper ? BO_ROW_H_TALL : BO_ROW_H;
}

// Returns the logical Y coordinate of the top edge of row `i`.
// Iterates prior rows (each may have a different height) rather than
// multiplying by BO_ROW_H, so tall rows shift subsequent rows down.
static int RowLogicalTop(int i) {
    int y = BO_TITLE_TOP_PAD + BO_TITLE_H + BO_TITLE_BOT_PAD;
    for (int j = 0; j < i; ++j) y += RowLogicalHeight(j);
    return y;
}

// The scrollable band: everything between the title strip and the
// button row. Rows are clipped to this and the gutter spans it.
static RECT BoViewportRect(int physW, int physH) {
    int top = (int)((BO_TITLE_TOP_PAD + BO_TITLE_H + BO_TITLE_BOT_PAD)
                    * g_dpiScale);
    // The override notice is pinned chrome, not a row, so it eats into
    // the scrollable band rather than scrolling with it.
    if (g_boModOverride) top += (int)(BO_OVERRIDE_H * g_dpiScale);
    int bot = physH - (int)((BO_ROW_TO_BTN_GAP + BO_BTN_H
                             + BO_BTN_BOTTOM_PAD) * g_dpiScale);
    if (bot < top) bot = top;
    return { 0, top, physW, bot };
}

// Where the override notice sits: directly under the title, directly
// above the scroll viewport.
static RECT BoOverrideRect(int physW, int physH) {
    (void)physH;
    int top = (int)((BO_TITLE_TOP_PAD + BO_TITLE_H + BO_TITLE_BOT_PAD)
                    * g_dpiScale);
    return { 0, top, physW, top + (int)(BO_OVERRIDE_H * g_dpiScale) };
}

// Returns the physical (dpi-scaled) rect for row `i`, in modal client
// coordinates. Rows span the full width minus the panel padding, less
// the scrollbar gutter when one is showing. The scroll offset is baked
// in here so every consumer — paint, hit-test, menu anchoring and EDIT
// placement — automatically agrees on where a row currently sits.
static RECT RowPhysRect(int i, int physW) {
    int y  = (int)(RowLogicalTop(i)    * g_dpiScale) - g_boScrollY;
    int h  = (int)(RowLogicalHeight(i) * g_dpiScale);
    int lx = (int)(BO_ROW_LABEL_INSET_L * g_dpiScale);
    int rx = physW - (int)(BO_ROW_LABEL_INSET_L * g_dpiScale);
    if (g_boScrollable) {
        // Native px — the bar itself is native (see BoScrollbarGeom).
        rx -= (BO_SB_W + BO_SB_PAD_R + BO_SB_ROW_GAP);
    }
    return { lx, y, rx, y + h };
}

// Value-box rect inside a row — the bronze chrome that holds the value
// (dropdown chevron or editable EDIT), fixed height so tall rows don't
// stretch it. StrDropdown rows get a wider box; pass the row so the
// width matches. nullptr = the standard numeric width, which is what
// the slider geometry wants.
static RECT ValueBoxPhysRect(const RECT& row, const BoRow* r = nullptr) {
    int logicalW = (r && r->kind == BoKind::StrDropdown)
                   ? BO_ROW_VALUE_BOX_W_STR : BO_ROW_VALUE_BOX_W;
    int boxW   = (int)(logicalW * g_dpiScale);
    int boxH   = (int)(BO_VALUE_BOX_H     * g_dpiScale);
    int insetR = (int)(BO_ROW_VALUE_INSET_R * g_dpiScale);
    int bx = row.right - boxW - insetR;
    int by = row.top + ((row.bottom - row.top) - boxH) / 2;
    return { bx, by, bx + boxW, by + boxH };
}

// Definition for the forward-declared helper above. For a toggle
// click, invalidate just that row; if the toggle is a cascade master,
// the caller passes a wider range so dependent rows repaint dim/live.
static void InvalidateRowRange(HWND hw, int firstRow, int lastRow) {
    if (!g_activeRows) return;
    if (firstRow < 0) firstRow = 0;
    if (lastRow >= g_activeRowCount) lastRow = g_activeRowCount - 1;
    if (firstRow > lastRow) return;
    RECT clientRc; GetClientRect(hw, &clientRc);
    RECT first = RowPhysRect(firstRow, clientRc.right);
    RECT last  = RowPhysRect(lastRow,  clientRc.right);
    RECT rc = { first.left, first.top, last.right, last.bottom };
    // Row rects carry the scroll offset, so a row scrolled out of view
    // would otherwise invalidate into the title strip or the button
    // band. Clip to the viewport; an empty intersection means the row
    // isn't on screen and needs no repaint at all.
    RECT vp = BoViewportRect(clientRc.right, clientRc.bottom);
    RECT clipped;
    if (!IntersectRect(&clipped, &rc, &vp)) return;
    InvalidateRect(hw, &clipped, FALSE);
}

static RECT SliderPhysRect(const RECT& row) {
    RECT vb = ValueBoxPhysRect(row);
    int sw = (int)(BO_SLIDER_W * g_dpiScale);
    int sh = (int)(BO_SLIDER_H * g_dpiScale);
    int sx = vb.left + ((vb.right - vb.left) - sw) / 2;
    int sy = row.top + ((row.bottom - row.top) - sh) / 2;
    return { sx, sy, sx + sw, sy + sh };
}

// ─────────────────────────────────────────────────────────────────────
//  Scrollbar — geometry, clamp, and the EDIT-follow sync
// ─────────────────────────────────────────────────────────────────────

struct BoSbGeom {
    bool present    = false;
    bool scrollable = false;
    RECT area  = {}, up = {}, down = {}, track = {}, thumb = {};
    int  maxScroll = 0;
    int  trackTop  = 0, trackH = 0;
};

// All rects in physical client coordinates, derived from the live
// client rect + g_boContentH so paint and mouse handling can't drift
// apart. g_boScrollY is read only to position the thumb; the caller is
// responsible for having clamped it.
static BoSbGeom BoScrollbarGeom(int physW, int physH) {
    BoSbGeom s;
    if (!g_boScrollable || physW <= 0 || physH <= 0) return s;
    RECT vp = BoViewportRect(physW, physH);
    int vpH = vp.bottom - vp.top;
    if (vpH <= 0) return s;

    s.present   = true;
    s.maxScroll = max(0, g_boContentH - vpH);

    // NOTE: every scrollbar dimension below is in NATIVE asset pixels,
    // NOT scaled by g_dpiScale. The arrow caps and the thumb are drawn
    // at their native size (DrawImage with GetWidth/GetHeight), so
    // scaling the gutter while the art stays native pushed the caps out
    // of line with the track. mod_list.cpp uses the same convention.
    int upH   = BO_SB_UP_H_FB;
    int downH = BO_SB_DOWN_H_FB;
    if (Gdiplus::Bitmap* a = AssetImage(L"scroll_up.png"))   upH   = (int)a->GetHeight();
    if (Gdiplus::Bitmap* a = AssetImage(L"scroll_down.png")) downH = (int)a->GetHeight();

    s.area.right  = physW - (int)(BO_ROW_LABEL_INSET_L * g_dpiScale) - BO_SB_PAD_R;
    s.area.left   = s.area.right - BO_SB_W;
    s.area.top    = vp.top;
    s.area.bottom = vp.bottom;

    s.up   = { s.area.left, vp.top,          s.area.right, vp.top + upH };
    s.down = { s.area.left, vp.bottom - downH, s.area.right, vp.bottom };

    s.trackTop = vp.top + upH;
    int trackBot = vp.bottom - downH;
    s.trackH = max(0, trackBot - s.trackTop);
    s.track  = { s.area.left, s.trackTop, s.area.right, trackBot };

    s.scrollable = (s.maxScroll > 0) && (s.trackH > BO_SB_MIN_THUMB);
    int thumbH;
    if (!s.scrollable) {
        thumbH = s.trackH;
    } else {
        thumbH = (int)((long long)s.trackH * vpH / max(1, g_boContentH));
        thumbH = max(BO_SB_MIN_THUMB, min(thumbH, s.trackH));
    }
    int thumbTop = s.trackTop;
    if (s.scrollable) {
        int travel = s.trackH - thumbH;
        if (travel > 0) {
            thumbTop = s.trackTop
                     + (int)((long long)g_boScrollY * travel / s.maxScroll);
            thumbTop = max(s.trackTop, min(thumbTop, s.trackTop + travel));
        }
    }
    int thumbX = s.area.left + (BO_SB_W - BO_SB_THUMB_W) / 2;
    s.thumb = { thumbX, thumbTop,
                thumbX + BO_SB_THUMB_W, thumbTop + thumbH };
    return s;
}

static void BoClampScroll(int physW, int physH) {
    RECT vp = BoViewportRect(physW, physH);
    // RECT fields are LONG, so (vp.bottom - vp.top) is a long and
    // max(0, int - long) has no deducible common type under GCC 15.
    // Narrow to int first, the same way BoScrollbarGeom does.
    int vpH = (int)(vp.bottom - vp.top);
    int maxScroll = max(0, g_boContentH - vpH);
    if (g_boScrollY < 0)         g_boScrollY = 0;
    if (g_boScrollY > maxScroll) g_boScrollY = maxScroll;
}

// The IntTextBox EDITs are real child windows, so unlike the painted
// rows they don't move when the scroll offset changes — they have to be
// repositioned explicitly. A row scrolled out of the viewport gets its
// EDIT hidden outright: clipping alone would leave it able to take
// clicks and keyboard focus in the title or button band.
static void BoSyncEditPositions(HWND hw) {
    if (!g_activeRows) return;
    RECT rc; GetClientRect(hw, &rc);
    RECT vp = BoViewportRect(rc.right, rc.bottom);
    for (int i = 0; i < g_activeRowCount && i < BO_MAX_ROWS; ++i) {
        HWND ed = g_boRowEdits[i];
        if (!ed) continue;
        RECT row = RowPhysRect(i, rc.right);
        RECT vb  = ValueBoxPhysRect(row, &g_activeRows[i]);
        int inset = (int)(4 * g_dpiScale);
        bool visible = (row.top >= vp.top) && (row.bottom <= vp.bottom);
        if (visible) {
            // SWP_NOCOPYBITS matters here: the default move BLITS the
            // control's existing pixels to the new position and only
            // repaints what it must. For antialiased text on a scrolled
            // control that leaves the old glyphs smeared under the new
            // ones — the "blurred / warped" numbers. Forcing a clean
            // erase + full repaint costs nothing at this size.
            SetWindowPos(ed, nullptr,
                         vb.left + inset, vb.top + inset,
                         (vb.right - vb.left) - 2 * inset,
                         (vb.bottom - vb.top) - 2 * inset,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
            ShowWindow(ed, SW_SHOW);
            InvalidateRect(ed, nullptr, TRUE);
            UpdateWindow(ed);
        } else {
            // Drop focus before hiding, or the EDIT keeps the caret and
            // swallows keystrokes while invisible.
            if (GetFocus() == ed) SetFocus(hw);
            ShowWindow(ed, SW_HIDE);
        }
    }
}

static void BoScrollBy(HWND hw, int deltaPhys) {
    if (!g_boScrollable) return;
    RECT rc; GetClientRect(hw, &rc);
    int before = g_boScrollY;
    g_boScrollY += deltaPhys;
    BoClampScroll(rc.right, rc.bottom);
    if (g_boScrollY == before) return;
    BoSyncEditPositions(hw);
    RECT vp = BoViewportRect(rc.right, rc.bottom);
    InvalidateRect(hw, &vp, FALSE);
}

// Vertical 3-slice for the thumb — keep `cap` px of art at each end and
// stretch the middle, so the grip's finished ends stay sharp.
static void BoDrawThumb(Graphics& g, Gdiplus::Bitmap* b,
                        int x, int y, int w, int h, int cap) {
    if (!b) return;
    int sw = (int)b->GetWidth(), sh = (int)b->GetHeight();
    if (h >= sh && h > cap * 2 && sh > cap * 2) {
        g.DrawImage(b, Rect(x, y, w, cap), 0, 0, sw, cap, UnitPixel);
        g.DrawImage(b, Rect(x, y + cap, w, h - cap * 2),
                    0, cap, sw, sh - cap * 2, UnitPixel);
        g.DrawImage(b, Rect(x, y + h - cap, w, cap),
                    0, sh - cap, sw, cap, UnitPixel);
    } else {
        g.DrawImage(b, Rect(x, y, w, h), 0, 0, sw, sh, UnitPixel);
    }
}

static void BoPaintScrollbar(Graphics& g, int physW, int physH) {
    BoSbGeom s = BoScrollbarGeom(physW, physH);
    if (!s.present) return;

    auto RW = [](const RECT& r) { return (int)(r.right - r.left); };
    auto RH = [](const RECT& r) { return (int)(r.bottom - r.top); };

    if (Gdiplus::Bitmap* tk = AssetImage(L"scrollbar_track.png")) {
        g.DrawImage(tk, Rect((INT)s.area.left, (INT)s.area.top,
                             (INT)RW(s.area), (INT)RH(s.area)),
                    0, 0, (INT)tk->GetWidth(), (INT)tk->GetHeight(), UnitPixel);
    } else {
        SolidBrush groove(Color(150, 0x10, 0x0A, 0x06));
        g.FillRectangle(&groove, (INT)s.area.left, (INT)s.area.top,
                        (INT)RW(s.area), (INT)RH(s.area));
        Pen edge(Tok::BronzeDim, 1.0f);
        g.DrawRectangle(&edge, (INT)s.area.left, (INT)s.area.top,
                        (INT)(RW(s.area) - 1), (INT)(RH(s.area) - 1));
    }

    if (Gdiplus::Bitmap* th = AssetImage(L"scroll.png")) {
        BoDrawThumb(g, th, (INT)s.thumb.left, (INT)s.thumb.top,
                    (INT)RW(s.thumb), (INT)RH(s.thumb),
                    BO_SB_THUMB_CAP);
    } else {
        SolidBrush grip(Tok::BronzeBright);
        g.FillRectangle(&grip, (INT)s.thumb.left, (INT)s.thumb.top,
                        (INT)RW(s.thumb), (INT)RH(s.thumb));
        Pen rim(Tok::Gold, 1.0f);
        g.DrawRectangle(&rim, (INT)s.thumb.left, (INT)s.thumb.top,
                        (INT)(RW(s.thumb) - 1), (INT)(RH(s.thumb) - 1));
    }

    if (Gdiplus::Bitmap* up = AssetImage(L"scroll_up.png"))
        g.DrawImage(up, (INT)s.up.left, (INT)s.up.top,
                    (INT)up->GetWidth(), (INT)up->GetHeight());
    if (Gdiplus::Bitmap* dn = AssetImage(L"scroll_down.png"))
        g.DrawImage(dn, (INT)s.down.left, (INT)s.down.top,
                    (INT)dn->GetWidth(), (INT)dn->GetHeight());

    if (g_boSbDragging) {
        SolidBrush glow(Color(30, 0xFF, 0xE0, 0xA0));
        g.FillRectangle(&glow, (INT)s.thumb.left, (INT)s.thumb.top,
                        (INT)RW(s.thumb), (INT)RH(s.thumb));
    }
}

// ─────────────────────────────────────────────────────────────────────
//  Paint
// ─────────────────────────────────────────────────────────────────────

// Paint one row's label + control. Runs inside the modal's WM_PAINT
// after the stone + frame + title backdrop has already been laid down.
// `disabled` = true dims the label and paints a semi-transparent stone
// overlay over the toggle/dropdown so the row reads as inert.
static void PaintRow(Graphics& g, const RECT& row, const BoRow& r,
                     bool disabled) {
    SolidBrush labelBr(disabled ? Tok::BronzeDim : Tok::TextParchment);
    SolidBrush valueBr(disabled ? Tok::BronzeDim : Tok::Gold);
    SolidBrush helperBr(disabled ? Tok::BronzeDim : Tok::TextParchment);

    // Label + optional helper text. Two-line rows split the label area
    // vertically: label on top, helper on the bottom in dim gold.
    StringFormat sfLbl;
    sfLbl.SetAlignment(StringAlignmentNear);
    sfLbl.SetLineAlignment(StringAlignmentCenter);
    sfLbl.SetFormatFlags(sfLbl.GetFormatFlags() | StringFormatFlagsNoWrap);

    RECT vb = ValueBoxPhysRect(row, &r);
    int  labelXBase = row.left + (int)(8 * g_dpiScale);
    int  labelX = labelXBase + (r.indent ? (int)(24 * g_dpiScale) : 0);
    int  labelW = vb.left - (int)(6 * g_dpiScale) - labelX;
    if (labelW < 0) labelW = 0;

    int rowH  = row.bottom - row.top;
    int lblH  = r.helper ? rowH / 2 : rowH;
    int lblY  = row.top;
    int helpY = row.top + lblH;
    int helpH = rowH - lblH;

    if (r.label) {
        // Label: prefer g_fModName (Exocet 18px) so labels read at a
        // comfortable size relative to the value controls. Falls back to
        // g_fBtn (13px) if the larger font failed to load.
        Gdiplus::Font* labelFont = g_fModName ? g_fModName : g_fBtn;
        if (labelFont) {
            g.DrawString(r.label, -1, labelFont,
                         RectF((REAL)labelX, (REAL)lblY,
                               (REAL)labelW, (REAL)lblH),
                         &sfLbl, &labelBr);
        }
    }
    if (r.helper) {
        // Helper: small (11px), dim-gold. Sub-label size keeps the note
        // clearly secondary to the label above.
        Gdiplus::Font* helperFont = g_fSubLbl ? g_fSubLbl : g_fBtn;
        if (helperFont) {
            g.DrawString(r.helper, -1, helperFont,
                         RectF((REAL)labelX, (REAL)helpY,
                               (REAL)labelW, (REAL)helpH),
                         &sfLbl, &helperBr);
        }
    }

    if (r.kind == BoKind::Toggle) {
        // Same asset family as the main window's Show Sockets toggle
        // (btn_toggle1 = false / btn_toggle3 = true). Fallback: pill
        // with a marker at the active end.
        bool on = r.boolTarget && *r.boolTarget;
        RECT sr = SliderPhysRect(row);
        const wchar_t* assetName = on ? L"btn_toggle3.png" : L"btn_toggle1.png";
        if (Gdiplus::Bitmap* asset = AssetImage(assetName)) {
            InterpolationMode prev = g.GetInterpolationMode();
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.DrawImage(asset, (INT)sr.left, (INT)sr.top,
                        (INT)(sr.right - sr.left),
                        (INT)(sr.bottom - sr.top));
            g.SetInterpolationMode(prev);
        } else {
            int trackY = sr.top + (sr.bottom - sr.top) / 2 - 1;
            Pen track(Tok::BronzeBright, 2.0f);
            g.DrawLine(&track,
                       sr.left + (int)(4 * g_dpiScale), trackY,
                       sr.right - (int)(4 * g_dpiScale), trackY);
            int markerW = (sr.bottom - sr.top) - (int)(4 * g_dpiScale);
            int slot0X  = sr.left + (int)(4 * g_dpiScale);
            int slot2X  = sr.right - (int)(4 * g_dpiScale) - markerW;
            int markerX = on ? slot2X : slot0X;
            SolidBrush markerFill(Tok::GoldBright);
            g.FillEllipse(&markerFill, markerX,
                          sr.top + (int)(2 * g_dpiScale),
                          markerW, markerW);
        }
        if (disabled) {
            SolidBrush dim(Color(140, 28, 24, 20));
            g.FillRectangle(&dim, (INT)sr.left, (INT)sr.top,
                            (INT)(sr.right - sr.left),
                            (INT)(sr.bottom - sr.top));
        }
        return;
    }

    // ── IntDropdown / IntTextBox chrome ─────────────────────────────────
    // Both use text_box.png as the bronze chrome. Dropdown paints the
    // value + chevron on top; TextBox skips both — a themed EDIT child
    // sits inside the chrome and paints its own contents.
    if (Gdiplus::Bitmap* tb = AssetImage(L"text_box.png")) {
        InterpolationMode prev = g.GetInterpolationMode();
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(tb, (INT)vb.left, (INT)vb.top,
                    (INT)(vb.right - vb.left),
                    (INT)(vb.bottom - vb.top));
        g.SetInterpolationMode(prev);
    } else {
        Pen border(Tok::Bronze, 1.0f);
        g.DrawRectangle(&border,
                        (INT)vb.left, (INT)vb.top,
                        (INT)((vb.right - vb.left) - 1),
                        (INT)((vb.bottom - vb.top) - 1));
    }

    if (r.kind == BoKind::IntTextBox) {
        // The EDIT paints its own value; nothing more to draw here.
        // Cascaded IntTextBox rows still need the dim overlay — the
        // EDIT itself is disabled via EnableWindow, but the chrome
        // around it has to read as inert too.
        if (disabled) {
            SolidBrush dim(Color(140, 28, 24, 20));
            g.FillRectangle(&dim, (INT)vb.left, (INT)vb.top,
                            (INT)(vb.right - vb.left),
                            (INT)(vb.bottom - vb.top));
        }
        return;
    }

    // ── Value text: number for IntDropdown, short token for StrDropdown ──
    wchar_t buf[32];
    const wchar_t* valueText = buf;
    if (r.kind == BoKind::StrDropdown) {
        int si = StrRowIndex(r);
        if (si >= 0 && r.strShort) {
            valueText = r.strShort[si];
        } else if (r.strTarget && !r.strTarget->empty()) {
            // Unrecognised hand-edited value — show it verbatim rather
            // than pretending it's one of ours.
            valueText = r.strTarget->c_str();
        } else {
            valueText = L"Default";
        }
    } else {
        int val = r.intTarget ? *r.intTarget : 0;
        swprintf(buf, 32, L"%d", val);
    }

    StringFormat sfC;
    sfC.SetAlignment(StringAlignmentCenter);
    sfC.SetLineAlignment(StringAlignmentCenter);
    sfC.SetTrimming(StringTrimmingEllipsisCharacter);
    sfC.SetFormatFlags(sfC.GetFormatFlags() | StringFormatFlagsNoWrap);
    int chevronPad = (int)(22 * g_dpiScale);
    if (g_fBtn) {
        g.DrawString(valueText, -1, g_fBtn,
                     RectF((REAL)vb.left, (REAL)vb.top,
                           (REAL)((vb.right - vb.left) - chevronPad),
                           (REAL)(vb.bottom - vb.top)),
                     &sfC, &valueBr);
    }

    if (Gdiplus::Bitmap* ch = AssetImage(L"dropdown_chevron.png")) {
        int chW = (int)ch->GetWidth();
        int chH = (int)ch->GetHeight();
        int targetH = (vb.bottom - vb.top) - (int)(4 * g_dpiScale);
        int targetW = (chH > 0) ? chW * targetH / chH : chW;
        int cx = vb.right - targetW - (int)(4 * g_dpiScale);
        int cy = vb.top + ((vb.bottom - vb.top) - targetH) / 2;
        InterpolationMode prev = g.GetInterpolationMode();
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(ch, cx, cy, targetW, targetH);
        g.SetInterpolationMode(prev);
    } else {
        StringFormat sfR;
        sfR.SetAlignment(StringAlignmentCenter);
        sfR.SetLineAlignment(StringAlignmentCenter);
        if (g_fBtn) {
            g.DrawString(L"\u25BE", -1, g_fBtn,
                         RectF((REAL)(vb.right - (int)(16 * g_dpiScale)),
                               (REAL)vb.top, (REAL)(16 * g_dpiScale),
                               (REAL)(vb.bottom - vb.top)),
                         &sfR, &valueBr);
        }
    }
    if (disabled) {
        SolidBrush dim(Color(140, 28, 24, 20));
        g.FillRectangle(&dim, (INT)vb.left, (INT)vb.top,
                        (INT)(vb.right - vb.left),
                        (INT)(vb.bottom - vb.top));
    }
}

// Paint the popup menu's owner-drawn items when TrackPopupMenu asks
// during g_boOpenMenuRow != -1. Item body = value string centered in
// a stone-toned cell, matching the main window's owner-draw menus.
static void PaintMenuItem(DRAWITEMSTRUCT* d) {
    Graphics g(d->hDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    int rl = (int)d->rcItem.left;
    int rt = (int)d->rcItem.top;
    int rR = (int)d->rcItem.right;
    int rB = (int)d->rcItem.bottom;
    int rw = rR - rl;
    int rh = rB - rt;

    bool selected = (d->itemState & ODS_SELECTED) != 0;
    bool checked  = (d->itemState & ODS_CHECKED)  != 0;

    SolidBrush bg(selected ? Tok::BgPanel2 : Tok::BgPanel);
    g.FillRectangle(&bg, rl, rt, rw, rh);

    Pen sep(Tok::BronzeDim, 1.0f);
    g.DrawLine(&sep, rl, rB - 1, rR, rB - 1);

    if (selected) {
        Pen glow(Tok::Gold, 1.0f);
        g.DrawRectangle(&glow, rl + 1, rt + 1, rw - 3, rh - 3);
    }

    if (checked) {
        int dd = S(6);
        SolidBrush dot(Tok::GoldBright);
        g.FillEllipse(&dot, rl + S(8),
                      rt + (rh - dd) / 2, dd, dd);
    }

    // Menu item ID = index/value + 1 (we shifted by 1 in the insert
    // loop so that TrackPopupMenu can return 0 as "user cancelled").
    // Int rows carry the value itself; string rows carry an index into
    // the row's strLong/strShort tables.
    int id = (int)d->itemID - 1;
    wchar_t buf[16];
    const wchar_t* text = buf;
    bool isStr = false;
    if (g_activeRows && g_boOpenMenuRow >= 0
        && g_boOpenMenuRow < g_activeRowCount) {
        const BoRow& r = g_activeRows[g_boOpenMenuRow];
        if (r.kind == BoKind::StrDropdown) {
            isStr = true;
            if (id >= 0 && id < r.strCount) {
                text = r.strLong ? r.strLong[id]
                     : (r.strShort ? r.strShort[id] : L"");
            } else {
                text = L"";
            }
        }
    }
    if (!isStr) swprintf(buf, 16, L"%d", id);

    StringFormat sfC;
    // Numbers centre nicely; long language names read better flush left
    // past the check gutter.
    sfC.SetAlignment(isStr ? StringAlignmentNear : StringAlignmentCenter);
    sfC.SetLineAlignment(StringAlignmentCenter);
    sfC.SetTrimming(StringTrimmingEllipsisCharacter);
    sfC.SetFormatFlags(sfC.GetFormatFlags() | StringFormatFlagsNoWrap);
    SolidBrush txt(selected ? Tok::GoldBright : Tok::Gold);
    if (g_fBtn) {
        int gut = isStr ? S(22) : 0;
        g.DrawString(text, -1, g_fBtn,
                     RectF((REAL)(rl + gut), (REAL)rt,
                           (REAL)(rw - gut - (isStr ? S(8) : 0)), (REAL)rh),
                     &sfC, &txt);
    }
}

// ─────────────────────────────────────────────────────────────────────
//  Popup menu (int dropdowns)
// ─────────────────────────────────────────────────────────────────────

// Open a themed int-value popup menu anchored to the row's value box.
// Blocks until the user picks a value or dismisses; on pick, updates
// the target int, writes the toml, and invalidates the modal for a
// repaint. No-op if the row is disabled by cascade.
static void OpenIntMenu(int rowIdx) {
    if (!g_activeRows) return;
    if (rowIdx < 0 || rowIdx >= g_activeRowCount) return;
    if (RowIsDisabled(rowIdx)) return;
    const BoRow& r = g_activeRows[rowIdx];
    if (r.kind != BoKind::IntDropdown || !r.intTarget) return;

    g_boOpenMenuRow = rowIdx;

    HMENU menu = CreatePopupMenu();
    int cur = *r.intTarget;
    for (int v = r.minValue; v <= r.maxValue; ++v) {
        MENUITEMINFOW mii = { sizeof(mii) };
        mii.fMask  = MIIM_FTYPE | MIIM_ID | MIIM_STATE;
        mii.fType  = MFT_OWNERDRAW;
        mii.fState = (v == cur) ? MFS_CHECKED : MFS_UNCHECKED;
        mii.wID    = (UINT)(v + 1);   // +1 so 0 can mean "cancelled"
        InsertMenuItemW(menu, (UINT)(v - r.minValue), TRUE, &mii);
    }

    // Anchor at the bottom-left of the row's value box, in physical
    // screen coordinates.
    RECT clientRc; GetClientRect(g_boHwnd, &clientRc);
    RECT row = RowPhysRect(rowIdx, clientRc.right);
    RECT vb  = ValueBoxPhysRect(row, &r);
    POINT pt = { vb.left, vb.bottom };
    ClientToScreen(g_boHwnd, &pt);

    int chosen = TrackPopupMenu(menu,
                                TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN,
                                pt.x, pt.y, 0, g_boHwnd, nullptr);
    DestroyMenu(menu);
    g_boOpenMenuRow = -1;

    if (chosen > 0) {
        int newVal = chosen - 1;
        if (newVal < r.minValue) newVal = r.minValue;
        if (newVal > r.maxValue) newVal = r.maxValue;
        // r is a const reference into g_activeRows — the mutable int
        // target lives outside the row struct, so we're free to write
        // it (and the toml key) directly.
        *r.intTarget = newVal;
        SaveTomlInt(r.tomlSection, r.tomlKey, newVal);
        InvalidateRowRange(g_boHwnd, rowIdx, rowIdx);
    }
}

// Open a themed string-value popup menu anchored to the row's value
// box. Same machinery as OpenIntMenu, but item IDs are indices into the
// row's value table rather than the values themselves.
//
// If the toml holds an unrecognised value, StrRowIndex returns -1 and
// nothing is checked — the user sees the raw value in the box and can
// either leave it alone or overwrite it by picking from the list.
static void OpenStrMenu(int rowIdx) {
    if (!g_activeRows) return;
    if (rowIdx < 0 || rowIdx >= g_activeRowCount) return;
    if (RowIsDisabled(rowIdx)) return;
    const BoRow& r = g_activeRows[rowIdx];
    if (r.kind != BoKind::StrDropdown || !r.strTarget || !r.strValues) return;

    g_boOpenMenuRow = rowIdx;

    HMENU menu = CreatePopupMenu();
    int cur = StrRowIndex(r);
    for (int i = 0; i < r.strCount; ++i) {
        MENUITEMINFOW mii = { sizeof(mii) };
        mii.fMask  = MIIM_FTYPE | MIIM_ID | MIIM_STATE;
        mii.fType  = MFT_OWNERDRAW;
        mii.fState = (i == cur) ? MFS_CHECKED : MFS_UNCHECKED;
        mii.wID    = (UINT)(i + 1);   // +1 so 0 can mean "cancelled"
        InsertMenuItemW(menu, (UINT)i, TRUE, &mii);
    }

    RECT clientRc; GetClientRect(g_boHwnd, &clientRc);
    RECT row = RowPhysRect(rowIdx, clientRc.right);
    RECT vb  = ValueBoxPhysRect(row, &r);
    // Right-align the wide menu to the box's right edge so it doesn't
    // run off the modal — the menu is much wider than the box.
    POINT pt = { vb.right, vb.bottom };
    ClientToScreen(g_boHwnd, &pt);

    int chosen = TrackPopupMenu(menu,
                                TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTALIGN,
                                pt.x, pt.y, 0, g_boHwnd, nullptr);
    DestroyMenu(menu);
    g_boOpenMenuRow = -1;

    if (chosen > 0) {
        int idx = chosen - 1;
        if (idx >= 0 && idx < r.strCount) {
            *r.strTarget = r.strValues[idx];
            SaveTomlStr(r.tomlSection, r.tomlKey, r.strValues[idx]);
            InvalidateRowRange(g_boHwnd, rowIdx, rowIdx);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────
//  WndProc
// ─────────────────────────────────────────────────────────────────────

static LRESULT CALLBACK BasicOptionsProc(HWND hw, UINT msg,
                                         WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        int W = rc.right, H = rc.bottom;

        // Double-buffered so asset blits don't flicker on drag.
        HDC memDC     = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);
        {
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

            // Stone bg — sampled at (40,40) so the texture cadence
            // matches the plugin manager popup + main window.
            if (Gdiplus::Bitmap* stone = AssetImage(L"bg_stone.png")) {
                int sw = (int)stone->GetWidth();
                int sh = (int)stone->GetHeight();
                // The crop starts at (40,40), so only (sw-40, sh-40) of
                // source is actually available. Clamping to sw/sh let
                // the source rect run 40px past the bitmap's edge,
                // which GDI+ renders as a smeared or blank band along
                // the bottom of a tall modal.
                int availW = (sw > 40) ? sw - 40 : sw;
                int availH = (sh > 40) ? sh - 40 : sh;
                int cropW = (availW < W) ? availW : W;
                int cropH = (availH < H) ? availH : H;
                if (cropW < 1) cropW = 1;
                if (cropH < 1) cropH = 1;
                Rect dst(0, 0, W, H);
                g.DrawImage(stone, dst, 40, 40, cropW, cropH, UnitPixel);
            } else {
                SolidBrush bg(Color(28, 24, 20));
                g.FillRectangle(&bg, 0, 0, W, H);
            }

            // Frame chrome — 9-slice frame_modbanner (corner 24).
            if (Gdiplus::Bitmap* frame = AssetImage(L"frame_modbanner.png")) {
                DrawButton9Slice(g, frame, 0, 0, W, H, 24);
            } else {
                Pen fallback(Tok::Bronze, 1.0f);
                g.DrawRectangle(&fallback, 1, 1, W - 3, H - 3);
            }

            // Title strip — centered at the top.
            SolidBrush titleBr(Tok::Gold);
            StringFormat sfT;
            sfT.SetAlignment(StringAlignmentCenter);
            sfT.SetLineAlignment(StringAlignmentCenter);
            Gdiplus::Font* titleFont = g_fModName ? g_fModName : g_fNavSm;
            if (titleFont) {
                g.DrawString(g_activeTitle, -1, titleFont,
                    RectF((REAL)rc.left, (REAL)S(BO_TITLE_TOP_PAD),
                          (REAL)(rc.right - rc.left),
                          (REAL)S(BO_TITLE_H)),
                    &sfT, &titleBr);
            }

            // v1.6.2: pinned notice when the active mod ships its own
            // D2RLoader.toml. The [d2rcore.*] rows below show GLOBAL
            // values the mod may be overriding, so say so rather than
            // letting the modal imply it's showing what's in effect.
            if (g_boModOverride) {
                RECT ob = BoOverrideRect(W, H);
                SolidBrush band(Color(120, 0x28, 0x10, 0x08));
                g.FillRectangle(&band, (INT)ob.left, (INT)ob.top,
                                (INT)(ob.right - ob.left),
                                (INT)(ob.bottom - ob.top));
                Pen obRule(Tok::RedDark, 1.0f);
                g.DrawLine(&obRule, (INT)ob.left, (INT)(ob.bottom - 1),
                           (INT)ob.right, (INT)(ob.bottom - 1));
                Gdiplus::Font* of = g_fNavSm ? g_fNavSm : g_fBtn;
                if (of) {
                    StringFormat sfO;
                    sfO.SetAlignment(StringAlignmentCenter);
                    sfO.SetLineAlignment(StringAlignmentCenter);
                    sfO.SetTrimming(StringTrimmingEllipsisCharacter);
                    sfO.SetFormatFlags(StringFormatFlagsNoWrap);
                    SolidBrush ob2(Tok::RedBright);
                    wstring msg = g_boModOverrideName
                                + L" has its own D2RLoader.toml \u2014 "
                                  L"game settings may be overridden";
                    g.DrawString(msg.c_str(), -1, of,
                        RectF((REAL)(ob.left + S(8)), (REAL)ob.top,
                              (REAL)((ob.right - ob.left) - S(16)),
                              (REAL)(ob.bottom - ob.top)),
                        &sfO, &ob2);
                }
            }

            // Rows — label + control per row. Cascade-dimmed rows
            // paint dim label + overlay-muted control.
            //
            // Clipped to the viewport so a partially-scrolled row can't
            // bleed over the title strip or the button band, and rows
            // fully outside it are skipped entirely.
            RECT vp = BoViewportRect(W, H);
            Gdiplus::Region prevClip;
            g.GetClip(&prevClip);
            g.SetClip(Rect((INT)vp.left, (INT)vp.top,
                           (INT)(vp.right - vp.left),
                           (INT)(vp.bottom - vp.top)), CombineModeIntersect);
            for (int i = 0; i < g_activeRowCount; ++i) {
                RECT row = RowPhysRect(i, W);
                if (row.bottom <= vp.top || row.top >= vp.bottom) continue;
                PaintRow(g, row, g_activeRows[i], RowIsDisabled(i));
            }
            g.SetClip(&prevClip, CombineModeReplace);

            BoPaintScrollbar(g, W, H);
        }

        // BitBlt only the invalidated region — for targeted row
        // updates this keeps the copy small and avoids overpainting
        // pixels outside the paint clip.
        int px = ps.rcPaint.left;
        int py = ps.rcPaint.top;
        int pw = ps.rcPaint.right - ps.rcPaint.left;
        int ph = ps.rcPaint.bottom - ps.rcPaint.top;
        BitBlt(hdc, px, py, pw, ph, memDC, px, py, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT rc; GetClientRect(hw, &rc);

        // Scrollbar gutter claims the click before any row does.
        BoSbGeom s = BoScrollbarGeom(rc.right, rc.bottom);
        if (s.present) {
            if (PtInRect(&s.thumb, pt) && s.scrollable) {
                g_boSbDragging = true;
                g_boSbGrabDY   = pt.y - s.thumb.top;
                SetCapture(hw);
                InvalidateRect(hw, &s.area, FALSE);
                return 0;
            }
            if (PtInRect(&s.up, pt)) {
                BoScrollBy(hw, -(int)(BO_ROW_H * g_dpiScale));
                return 0;
            }
            if (PtInRect(&s.down, pt)) {
                BoScrollBy(hw, +(int)(BO_ROW_H * g_dpiScale));
                return 0;
            }
            if (PtInRect(&s.track, pt)) {
                int vpH = (int)(s.area.bottom - s.area.top);
                int page = max((int)(BO_ROW_H * g_dpiScale),
                               vpH - (int)(BO_ROW_H * g_dpiScale));
                BoScrollBy(hw, (pt.y < s.thumb.top) ? -page : +page);
                return 0;
            }
        }

        // Clicks outside the row viewport (title strip, button band)
        // must not fall through to a row that happens to be scrolled
        // under them.
        RECT vp = BoViewportRect(rc.right, rc.bottom);
        if (pt.y < vp.top || pt.y >= vp.bottom) return 0;

        for (int i = 0; i < g_activeRowCount; ++i) {
            RECT row = RowPhysRect(i, rc.right);
            if (pt.y < row.top || pt.y >= row.bottom) continue;
            if (pt.x < row.left || pt.x >= row.right) continue;

            if (RowIsDisabled(i)) return 0;    // cascade-inert

            const BoRow& r = g_activeRows[i];
            if (r.kind == BoKind::Toggle) {
                // Row-wide hit target — clicking anywhere in the row
                // toggles the boolean. Forgiving on small slider art.
                if (r.boolTarget) {
                    *r.boolTarget = !*r.boolTarget;
                    SaveTomlBool(r.tomlSection, r.tomlKey, *r.boolTarget);
                    // If this row is a cascade master, dependent rows
                    // may need to flip between dim/live — find the
                    // trailing dependent index and invalidate the whole
                    // affected range. Otherwise, just this row.
                    int lastRow = i;
                    for (int j = i + 1; j < g_activeRowCount; ++j) {
                        if (g_activeRows[j].cascadedFrom == i) {
                            lastRow = j;
                            // A greyed-out IntTextBox still has a live
                            // EDIT child underneath, which would happily
                            // take keystrokes the row is supposed to be
                            // refusing. Keep the control's enabled state
                            // in step with the cascade.
                            if (g_activeRows[j].kind == BoKind::IntTextBox
                                && j < BO_MAX_ROWS && g_boRowEdits[j]) {
                                EnableWindow(g_boRowEdits[j], !RowIsDisabled(j));
                            }
                        }
                    }
                    InvalidateRowRange(hw, i, lastRow);
                    UpdateWindow(hw);
                }
            } else if (r.kind == BoKind::IntDropdown
                       || r.kind == BoKind::StrDropdown) {
                // Only the value-box area opens the popup, so clicking
                // the label doesn't spuriously open menus.
                RECT vb = ValueBoxPhysRect(row, &r);
                if (pt.x >= vb.left && pt.x < vb.right
                    && pt.y >= vb.top && pt.y < vb.bottom) {
                    if (r.kind == BoKind::IntDropdown) OpenIntMenu(i);
                    else                               OpenStrMenu(i);
                }
            }
            // IntTextBox: the EDIT child catches its own clicks; a
            // click on the label area does nothing (matches the
            // dropdown case — no spurious focus grab).
            break;
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        // ~3 rows per notch, matching the mod list's feel.
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        BoScrollBy(hw, -(delta / WHEEL_DELTA) * (int)(BO_ROW_H * g_dpiScale) * 3);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!g_boSbDragging) break;
        RECT rc; GetClientRect(hw, &rc);
        BoSbGeom s = BoScrollbarGeom(rc.right, rc.bottom);
        int thumbH = s.thumb.bottom - s.thumb.top;
        int travel = s.trackH - thumbH;
        if (travel > 0 && s.maxScroll > 0) {
            int newTop = GET_Y_LPARAM(lp) - g_boSbGrabDY;
            newTop = max(s.trackTop, min(newTop, s.trackTop + travel));
            g_boScrollY = (int)((long long)(newTop - s.trackTop)
                                * s.maxScroll / travel);
            BoClampScroll(rc.right, rc.bottom);
            BoSyncEditPositions(hw);
            RECT vp = BoViewportRect(rc.right, rc.bottom);
            InvalidateRect(hw, &vp, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_boSbDragging) {
            g_boSbDragging = false;
            ReleaseCapture();
            RECT rc; GetClientRect(hw, &rc);
            BoSbGeom s = BoScrollbarGeom(rc.right, rc.bottom);
            if (s.present) InvalidateRect(hw, &s.area, FALSE);
        }
        return 0;
    }

    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* m = (MEASUREITEMSTRUCT*)lp;
        if (m->CtlType == ODT_MENU && g_boOpenMenuRow >= 0) {
            bool isStr = g_activeRows
                         && g_boOpenMenuRow < g_activeRowCount
                         && g_activeRows[g_boOpenMenuRow].kind
                            == BoKind::StrDropdown;
            m->itemWidth  = S(isStr ? BO_MENU_STR_W : BO_MENU_ITEM_W);
            m->itemHeight = S(BO_MENU_ITEM_H);
            return TRUE;
        }
        return FALSE;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;
        if (d->CtlType == ODT_MENU && g_boOpenMenuRow >= 0) {
            PaintMenuItem(d);
            return TRUE;
        }
        // Close button — MkStdBtn/PaintOwnerDrawButton pipeline.
        if (PaintOwnerDrawButton(d)) return TRUE;
        return 0;
    }

    case WM_COMMAND: {
        WORD id   = LOWORD(wp);
        WORD code = HIWORD(wp);
        // IntTextBox EDIT lost focus (user tabbed/clicked away or the
        // modal closed). Read the string, clamp to the row's range,
        // and write to the toml + intTarget. The EDIT is refreshed
        // with the clamped value so out-of-range typing snaps back.
        if (code == EN_KILLFOCUS
            && id >= BO_EDIT_ID_BASE
            && id <  BO_EDIT_ID_BASE + BO_MAX_ROWS
            && g_activeRows) {
            int rowIdx = id - BO_EDIT_ID_BASE;
            if (rowIdx >= g_activeRowCount) break;
            const BoRow& r = g_activeRows[rowIdx];
            if (r.kind != BoKind::IntTextBox || !r.intTarget) break;
            HWND ed = g_boRowEdits[rowIdx];
            if (!ed) break;
            wchar_t buf[16] = {};
            GetWindowTextW(ed, buf, 16);
            int val = _wtoi(buf);
            if (val < r.minValue) val = r.minValue;
            if (val > r.maxValue) val = r.maxValue;
            *r.intTarget = val;
            SaveTomlInt(r.tomlSection, r.tomlKey, val);
            // Refresh EDIT with the clamped value (silently no-op if
            // val already equals the parsed text).
            wchar_t back[16]; swprintf(back, 16, L"%d", val);
            SetWindowTextW(ed, back);
            return 0;
        }
        if (code == BN_CLICKED && id == 1) {
            DestroyWindow(hw);
            return 0;
        }
        break;
    }

    case WM_CTLCOLOREDIT: {
        // Theme the IntTextBox EDITs — dark bg + gold text on a black
        // brush so the value box reads like a shadowed well matching
        // the rename modal's text field.
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, RGB(0xE0, 0xC0, 0x70));  // warm gold
        SetBkColor(hdc, RGB(0, 0, 0));
        return (LRESULT)GetStockObject(BLACK_BRUSH);
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            DestroyWindow(hw);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hw);
        return 0;

    case WM_DESTROY: {
        HWND parent = GetWindow(hw, GW_OWNER);
        if (parent) {
            EnableWindow(parent, TRUE);
            SetForegroundWindow(parent);
        }
        g_boHwnd     = nullptr;
        g_boCloseBtn = nullptr;
        // EDIT children are destroyed automatically with the parent;
        // just null the array so a subsequent open starts clean.
        for (int i = 0; i < BO_MAX_ROWS; ++i) g_boRowEdits[i] = nullptr;
        g_activeRows     = nullptr;
        g_activeRowCount = 0;
        g_activeTitle    = L"";
        g_boScrollY      = 0;
        g_boContentH     = 0;
        g_boScrollable   = false;
        g_boModOverride  = false;
        g_boModOverrideName.clear();
        if (g_boSbDragging) { g_boSbDragging = false; ReleaseCapture(); }
        g_boSbGrabDY     = 0;
        return 0;
    }
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

// ─────────────────────────────────────────────────────────────────────
//  Shared spawner + public entry points
// ─────────────────────────────────────────────────────────────────────

// D2RLoader 1.1.0 lets a mod ship its own config at
//   <game>\mods\<Mod>\d2rloader\config\D2RLoader.toml
// Per the stock toml's own section comments, [d2rcore.*] settings say
// "Active mods can override these" while [d2rloader] says "Mods cannot
// override these". Every [d2rcore.*] row in Basic Options is therefore
// showing a GLOBAL value that the running mod may be overriding.
//
// We deliberately do NOT parse the override file — reading it would
// mean guessing at merge semantics we haven't confirmed, and showing a
// wrong "effective" value is worse than showing the global one. We just
// detect that it exists and say so, so the user knows the rows below
// may not be what's in effect.
static void BoDetectModOverride() {
    g_boModOverride = false;
    g_boModOverrideName.clear();
    if (g_selMod < 0 || g_selMod >= (int)g_mods.size()) return;
    wstring path = g_mods[g_selMod].dir
                 + L"\\d2rloader\\config\\D2RLoader.toml";
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) return;
    g_boModOverride     = true;
    g_boModOverrideName = g_mods[g_selMod].folder;
}

// Summed height of every row, in logical px — the scrollable content.
static int ComputeRowsHeightLogical(const BoRow* rows, int rowCount) {
    int y = 0;
    for (int j = 0; j < rowCount; ++j) {
        y += rows[j].helper ? BO_ROW_H_TALL : BO_ROW_H;
    }
    return y;
}

// Modal height derived from the row list. This is the height the modal
// WANTS; ShowLoaderOptionsModal caps it against the monitor work area
// and lets the viewport scroll whatever doesn't fit.
static int ComputeModalHeightLogical(const BoRow* rows, int rowCount) {
    int y = BO_TITLE_TOP_PAD + BO_TITLE_H + BO_TITLE_BOT_PAD;
    y += ComputeRowsHeightLogical(rows, rowCount);
    return y + BO_ROW_TO_BTN_GAP + BO_BTN_H + BO_BTN_BOTTOM_PAD;
}

static void ShowLoaderOptionsModal(HWND parent, const wchar_t* title,
                                   const BoRow* rows, int rowCount) {
    if (g_boHwnd) return;   // already open (either flavor)
    if (rowCount > BO_MAX_ROWS) rowCount = BO_MAX_ROWS;

    if (!g_boClassReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = BasicOptionsProc;
        wc.hInstance     = g_hInst;
        wc.lpszClassName = L"AngirisLoaderOptionsModal";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        g_boClassReg = true;
    }

    // Bind the active list before creating the window (WM_PAINT can
    // fire during CreateWindow/ShowWindow, so the paint code needs
    // these already valid).
    g_activeRows     = rows;
    g_activeRowCount = rowCount;
    g_activeTitle    = title;
    for (int i = 0; i < BO_MAX_ROWS; ++i) g_boRowEdits[i] = nullptr;

    RECT pr;
    GetWindowRect(parent, &pr);
    int physW = (int)(BO_W * g_dpiScale);
    int wantH = (int)(ComputeModalHeightLogical(rows, rowCount) * g_dpiScale);

    // Cap against the work area of the monitor the parent is on. The
    // 1.1.0 row tables are far taller than any screen at higher user
    // scales, so anything past the cap scrolls instead of growing the
    // window off the desktop.
    int physH = wantH;
    int maxH  = wantH;
    // Absolute ceiling first, so the modal scrolls even on a monitor
    // with room to spare.
    int hardCap = (int)(BO_MAX_H_LOGICAL * g_dpiScale);
    if (physH > hardCap) physH = hardCap;
    HMONITOR mon = MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (mon && GetMonitorInfoW(mon, &mi)) {
        int workH = mi.rcWork.bottom - mi.rcWork.top;
        maxH = workH * BO_MAX_H_PERCENT / 100;
        if (physH > maxH) physH = maxH;
    }

    // Scroll bookkeeping has to be live before the window exists —
    // WM_PAINT can fire during CreateWindow, and RowPhysRect consults
    // g_boScrollable for the gutter inset.
    g_boScrollY  = 0;
    g_boContentH = (int)(ComputeRowsHeightLogical(rows, rowCount) * g_dpiScale);
    {
        RECT vp = BoViewportRect(physW, physH);
        g_boScrollable = (g_boContentH > (vp.bottom - vp.top));
    }
    g_boSbDragging = false;
    g_boSbGrabDY   = 0;

    int x = pr.left + ((pr.right  - pr.left) - physW) / 2;
    int y = pr.top  + ((pr.bottom - pr.top ) - physH) / 2;
    // Keep the popup fully on-screen once it's tall enough to matter —
    // centring on the parent can push a capped modal off the top edge.
    if (mon && GetMonitorInfoW(mon, &mi)) {
        if (y < mi.rcWork.top) y = mi.rcWork.top;
        if (y + physH > mi.rcWork.bottom) y = mi.rcWork.bottom - physH;
        if (x < mi.rcWork.left) x = mi.rcWork.left;
        if (x + physW > mi.rcWork.right) x = mi.rcWork.right - physW;
    }

    g_boHwnd = CreateWindowExW(
        0,  // owned popup: stays above its owner (launcher) without pinning over other apps     // no DLGMODALFRAME — our own frame_modbanner
                           // is the visible border; the system-drawn
                           // 3D edge from DLGMODALFRAME was showing as
                           // a bright white ring around the popup.
        L"AngirisLoaderOptionsModal",
        title,
        // WS_CLIPCHILDREN: parent paints excluded from where children
        // sit, so the Close button + IntTextBox EDITs don't flicker
        // during targeted row invalidations.
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, physW, physH,
        parent, nullptr, g_hInst, nullptr);
    if (!g_boHwnd) {
        g_activeRows     = nullptr;
        g_activeRowCount = 0;
        g_activeTitle    = L"";
        return;
    }

    // Close button — centered horizontally, bottom-anchored so the
    // button grows upward if we ever bump BO_BTN_H.
    int physBtnW = (int)(BO_BTN_W * g_dpiScale);
    int physBtnH = (int)(BO_BTN_H * g_dpiScale);
    int btnX = (physW - physBtnW) / 2;
    int btnY = physH - (int)(BO_BTN_BOTTOM_PAD * g_dpiScale) - physBtnH;
    g_boCloseBtn = MkStdBtn(g_boHwnd, L"Close", 1,
                            btnX, btnY, physBtnW, physBtnH,
                            true, ButtonKind::Plugins);

    // For each IntTextBox row, spawn a themed EDIT positioned inside
    // the value box. ID = BO_EDIT_ID_BASE + row index so EN_KILLFOCUS
    // routes back to the right row.
    for (int i = 0; i < rowCount; ++i) {
        if (rows[i].kind != BoKind::IntTextBox) continue;
        RECT rowRc = RowPhysRect(i, physW);
        RECT vb    = ValueBoxPhysRect(rowRc, &rows[i]);
        int inset = (int)(4 * g_dpiScale);
        int ex = vb.left + inset;
        int ey = vb.top + inset;
        int ew = (vb.right - vb.left) - 2 * inset;
        int eh = (vb.bottom - vb.top) - 2 * inset;
        int cur = rows[i].intTarget ? *rows[i].intTarget : 0;
        wchar_t buf[16]; swprintf(buf, 16, L"%d", cur);
        g_boRowEdits[i] = CreateWindowExW(0,
            L"EDIT", buf,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_NUMBER,
            ex, ey, ew, eh,
            g_boHwnd, (HMENU)(UINT_PTR)(BO_EDIT_ID_BASE + i),
            g_hInst, nullptr);
        if (g_boRowEdits[i]) {
            // Cap input at the digit count the row's max actually needs
            // (255 → 3, 100 → 3, but keep this derived rather than
            // hardcoded so a future wider range doesn't silently
            // truncate what the user can type).
            int limit = 1;
            for (int m = rows[i].maxValue; m >= 10; m /= 10) ++limit;
            SendMessage(g_boRowEdits[i], EM_SETLIMITTEXT, (WPARAM)limit, 0);
            // A cascaded text box starts disabled if its master is off.
            if (rows[i].cascadedFrom >= 0)
                EnableWindow(g_boRowEdits[i], !RowIsDisabled(i));
        }
    }

    EnableWindow(parent, FALSE);
    ShowWindow(g_boHwnd, SW_SHOW);
    // A scrolled viewport can start with some EDITs off-screen; this
    // hides those before the first frame is shown.
    if (g_boScrollable) BoSyncEditPositions(g_boHwnd);
    UpdateWindow(g_boHwnd);
    SetActiveWindow(g_boHwnd);

    MSG msg;
    while (g_boHwnd) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == 0 || got == -1) {
            if (got == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (g_boHwnd && IsDialogMessageW(g_boHwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void ShowBasicOptionsModal(HWND parent) {
    // Only Basic carries [d2rcore.*] rows, so only Basic can be
    // shadowed by a mod's override toml. Developer Options is entirely
    // [d2rloader.*], which mods cannot override.
    BoDetectModOverride();
    ShowLoaderOptionsModal(parent, L"Basic Options",
        g_boRowsBasic,
        (int)(sizeof(g_boRowsBasic) / sizeof(g_boRowsBasic[0])));
}

void ShowDeveloperOptionsModal(HWND parent) {
    g_boModOverride = false;
    g_boModOverrideName.clear();
    ShowLoaderOptionsModal(parent, L"Developer Options",
        g_boRowsDev,
        (int)(sizeof(g_boRowsDev) / sizeof(g_boRowsDev[0])));
}
