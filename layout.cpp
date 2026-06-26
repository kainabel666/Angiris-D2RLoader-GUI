// ═══════════════════════════════════════════════════════════════════════
//  layout.cpp — see layout.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "layout.h"
#include "core.h"   // AppDir, ReadTextFile, JsonStr, JsonInt, JsonBool
// Phase 7d additions — the Layout / Refresh* / Compute* functions
// extracted from Angiris.cpp reach into many other modules:
#include "scaling.h"             // S, SF, U, g_dpiScale, SPosL
#include "colors.h"              // Tok::* (not directly used by layout — included for parity)
#include "assets.h"              // MeasureFrameInset (frame_main.png inset measurement)
#include "fonts.h"               // g_seedLabelLogicalW dependency (BodySeedComboRect)
#include "ui_state.h"            // g_hw* HWNDs + the rect globals Layout writes
#include "config.h"              // g_cfg.d2rPath (RefreshMods)
#include "mod_scan.h"            // g_mods, g_selMod, FindMods (RefreshMods)
#include "mod_config.h"          // g_modSettings, LoadModSettings (RefreshMods)
#include "launch_flags.h"        // ModSettings struct (default-construct in RefreshMods)
#include "mod_list.h"            // ML_REFRESH

// Whole-document state. Default-constructed = no overrides applied,
// every accessor returns the caller's default.
LayoutOverrides g_layout;

// LO::ROW_H storage. Header declares extern; this is the definition.
// Initialized to the launcher's default; wWinMain overrides at startup
// via `LO::ROW_H = LayoutModRowHeight(LO::ROW_H);` if user_layout.json
// changes it.
namespace LO { int ROW_H = 96; }

// ── Helpers (file-local) ─────────────────────────────────────────────

// Pull a nested JSON object's body out of a parent JSON document.
// Given json = `{"foo":{"a":1,"b":2},"bar":3}` and key = `foo`,
// returns `{"a":1,"b":2}`. Crude but adequate — we don't need a
// real parser since the JSON we're reading is hand-written, small,
// and the JsonStr/Int/Bool helpers in core already operate via
// substring search. Returns empty string when the key is missing
// or the value isn't an object.
//
// Brace-balanced extraction: tracks `{` and `}` depth so a value
// containing further braces parses correctly. Handles quoted
// strings so a `}` inside `"..."` doesn't end the object early.
static wstring ExtractNestedObject(const wstring& json, const wchar_t* key) {
    wstring needle = wstring(L"\"") + key + L"\"";
    size_t k = json.find(needle);
    if (k == wstring::npos) return L"";
    size_t colon = json.find(L':', k + needle.size());
    if (colon == wstring::npos) return L"";
    // Skip whitespace to find the opening brace
    size_t open = colon + 1;
    while (open < json.size()
           && (json[open] == L' ' || json[open] == L'\t'
            || json[open] == L'\r' || json[open] == L'\n'))
        ++open;
    if (open >= json.size() || json[open] != L'{') return L"";

    // Walk to the matching close brace, ignoring braces inside quotes.
    int depth = 0;
    bool inStr = false;
    for (size_t i = open; i < json.size(); ++i) {
        wchar_t c = json[i];
        if (inStr) {
            if (c == L'\\' && i + 1 < json.size()) { ++i; continue; }
            if (c == L'"') inStr = false;
            continue;
        }
        if (c == L'"') { inStr = true; continue; }
        if (c == L'{') ++depth;
        else if (c == L'}') {
            --depth;
            if (depth == 0) {
                // Include both the open and close braces in the result
                // so the returned blob is itself a valid JSON object
                // body, parseable by the same helpers (JsonStr, etc.)
                // that JsonStr expects to receive a full JSON value.
                return json.substr(open, i - open + 1);
            }
        }
    }
    return L"";
}

// JsonInt / JsonBool / JsonStr in core return 0 / false / empty on
// missing keys — they don't distinguish "present with that value"
// from "absent." For overrides we DO need that distinction, so
// these wrappers test for the key's presence first and only call
// the core helper when the key is actually there.

static bool HasJsonKey(const wstring& json, const wchar_t* key) {
    wstring needle = wstring(L"\"") + key + L"\"";
    size_t k = json.find(needle);
    if (k == wstring::npos) return false;
    // Make sure what follows looks like a key (colon, optionally
    // after whitespace) rather than this being a value-string that
    // happens to contain the key text.
    size_t colon = json.find(L':', k + needle.size());
    if (colon == wstring::npos) return false;
    // Reject the case where there's a non-whitespace, non-colon char
    // between the close-quote and the colon (would mean it's a value,
    // not a key).
    for (size_t i = k + needle.size(); i < colon; ++i) {
        if (json[i] != L' ' && json[i] != L'\t'
         && json[i] != L'\r' && json[i] != L'\n') return false;
    }
    return true;
}

static std::optional<int> ReadJsonInt(const wstring& json, const wchar_t* key) {
    if (!HasJsonKey(json, key)) return std::nullopt;
    return JsonInt(json, key, 0);
}

static std::optional<bool> ReadJsonBool(const wstring& json, const wchar_t* key) {
    if (!HasJsonKey(json, key)) return std::nullopt;
    return JsonBool(json, key, false);
}

// ── Loader ───────────────────────────────────────────────────────────

void LoadLayoutOverrides() {
    wstring path = AppDir() + L"\\assets\\user_layout.json";
    wstring json = ReadTextFile(path);
    if (json.empty()) return;        // file missing or unreadable

    // Top-level scalar overrides
    g_layout.modRowHeight      = ReadJsonInt (json, L"mod_row_height");
    g_layout.showModdingExpand = ReadJsonBool(json, L"show_modding_expand");

    // Nested "version_label" object
    wstring verObj = ExtractNestedObject(json, L"version_label");
    if (!verObj.empty()) {
        g_layout.versionLabelX = ReadJsonInt(verObj, L"x");
        g_layout.versionLabelY = ReadJsonInt(verObj, L"y");
    }

    // Nested "nav_buttons" object — iterate known button IDs and
    // pull each one's nested override (also an object). We don't
    // iterate the JSON's keys themselves; instead we walk a fixed
    // set of known IDs and ask for each one. That way unknown IDs
    // in the JSON get silently ignored (no spurious entries in
    // g_layout.navButtons) and the launcher's compiled-in id list
    // is the source of truth.
    wstring navObj = ExtractNestedObject(json, L"nav_buttons");
    if (!navObj.empty()) {
        const wchar_t* kIds[] = {
            L"mods", L"options", L"logs", L"help", L"about", L"exit",
        };
        for (const wchar_t* id : kIds) {
            wstring btnObj = ExtractNestedObject(navObj, id);
            if (btnObj.empty()) continue;
            NavButtonOverride ov;
            ov.visible = ReadJsonBool(btnObj, L"visible");
            ov.enabled = ReadJsonBool(btnObj, L"enabled");
            // Only store if at least one field was overridden, to
            // keep g_layout.navButtons sparse.
            if (ov.visible.has_value() || ov.enabled.has_value()) {
                g_layout.navButtons[id] = ov;
            }
        }
    }
}

// ── Accessors ────────────────────────────────────────────────────────

int  LayoutModRowHeight     (int  d) { return g_layout.modRowHeight     .value_or(d); }
int  LayoutVersionLabelX    (int  d) { return g_layout.versionLabelX    .value_or(d); }
int  LayoutVersionLabelY    (int  d) { return g_layout.versionLabelY    .value_or(d); }
bool LayoutShowModdingExpand(bool d) { return g_layout.showModdingExpand.value_or(d); }

bool LayoutNavButtonVisible(const wchar_t* id, bool d) {
    auto it = g_layout.navButtons.find(id);
    if (it == g_layout.navButtons.end()) return d;
    return it->second.visible.value_or(d);
}

bool LayoutNavButtonEnabled(const wchar_t* id, bool d) {
    auto it = g_layout.navButtons.find(id);
    if (it == g_layout.navButtons.end()) return d;
    return it->second.enabled.value_or(d);
}


// ═══════════════════════════════════════════════════════════════════════
//  PHASE 7d EXTRACTIONS — layout computation + orchestration
// ═══════════════════════════════════════════════════════════════════════
//
//  ComputeBodyLayout + ComputeLeftPanelGeom + Body*Rect: pure functions
//  that derive geometry from the window size and the frame_main /
//  frame_panel_right assets. No globals read, no state mutated.
//
//  RefreshModDescriptionLinks: show/hide the three per-mod link buttons
//  based on whether the currently-selected mod's modinfo.json provided
//  the corresponding fields. Layout must run after this for the
//  buttons to actually move into their painted slots.
//
//  Layout: the main child-positioning function. Reads the LO::*
//  constants + layout overrides + measured asset insets, writes
//  every g_hw* control's WindowPos, and stores the dropdown / slider
//  rects used by paint and hit-test paths.
//
//  RepositionForExpansion: triggered when the bottom panel toggles
//  open/closed. Resizes the window's outer height, then re-runs
//  Layout in the new size.
//
//  RefreshMods: rescan g_mods, restore last-selected mod, reload
//  per-mod settings, refresh link visibility, re-run Layout, repaint.
//

// ── Right-column body geometry ──────────────────────────────────────────
//
// Declared here (before Layout) because Layout uses it to position the
// right-column Win32 button children. The painters that consume the same
// geometry live further down with the BODY GEOMETRY + PAINT section.

// (BodyLayout struct moved to layout.h)

// (BODY_FLAG_GRID_COLS/ROWS moved to layout.h)

BodyLayout ComputeBodyLayout(int W, int H) {
    using namespace LO;
    BodyLayout B = {};

    // Frame-aware insets from frame_main.png.
    // fi.right under-measures (sparse vertical filigree falls below the
    // density threshold), so fall back to fi.bottom — the border is roughly
    // uniform thickness. No padding: the right panel sits FLUSH against the
    // inner edge of frame_main's right filigree, per design.
    FrameInset fi = MeasureFrameInset(L"frame_main.png");
    int insetR = (fi.right >= 8) ? fi.right : fi.bottom;

    // The right column is FRAMED by frame_panel_right.png. Its dimensions
    // and the three internal panel regions (MOD DESCRIPTION / LAUNCH OPTIONS
    // / PLAY) are pulled from the asset's measured divider positions, all
    // at the asset's native pixel scale (no stretching).
    //
    // Position: right edge flush with the inside of frame_main's right
    // filigree; top edge below the title-bar buttons (which sit at
    // TB_BTN_INSET_T .. TB_BTN_INSET_T+TB_BTN_H) plus a small gap.
    Gdiplus::Bitmap* panel = AssetImage(L"frame_panel_right.png");
    int panelW = panel ? (int)panel->GetWidth()  : RIGHT_COL_W;
    int panelH = panel ? (int)panel->GetHeight() : 915;
    PanelRegions pr = MeasurePanelRegions(L"frame_panel_right.png");

    B.rightW = panelW;
    B.rightX = W - panelW - insetR;
    B.rightY = 36 + 42 + 12 - 10;       // 80; below title bar buttons + 12 gap, then up 10 so frame_panel_right's top sits just inside the frame_main filigree instead of overlapping it
    B.rightH = panelH;

    // Translate the asset's internal y-coordinates to window-space.
    // Fallback to even thirds if the measurement failed.
    int topY0, topY1, midY0, midY1, botY0, botY1;
    if (pr.valid) {
        topY0 = B.rightY + pr.topPanelY0;
        topY1 = B.rightY + pr.topPanelY1;
        midY0 = B.rightY + pr.midPanelY0;
        midY1 = B.rightY + pr.midPanelY1;
        botY0 = B.rightY + pr.botPanelY0;
        botY1 = B.rightY + pr.botPanelY1;
    } else {
        int third = panelH / 3;
        topY0 = B.rightY;            topY1 = B.rightY + third;
        midY0 = topY1;               midY1 = topY1 + third;
        botY0 = midY1;               botY1 = B.rightY + panelH;
    }
    int side = pr.valid ? pr.sideMargin : 12;

    // MOD DESCRIPTION panel geometry (top region of the asset)
    B.descX = B.rightX + side + 4;
    B.descY = topY0;
    B.descW = panelW - (side + 4) * 2;
    B.descH = topY1 - topY0;
    int headerH = 36;
    // Three stacked Discord/Docs/Website buttons. Must match Layout()'s
    // placeAtSlot lambda: LINK_H=54, LINK_GAP=6, three buttons, two gaps.
    // Old value (28*3 + 16 = 100) was inherited from a previous smaller
    // link-button design and is what caused the bottom button to bleed
    // through into the LAUNCH OPTIONS panel below.
    int linkH   = 54 * 3 + 6 * 2;
    B.descUpdateBarH = 28;
    // Anchor the link-button stack near the BOTTOM of the description
    // panel so the gap between the WEBSITE button and the panel frame is
    // tight — visually the stack should sit at the bottom of the region
    // (about 8 px of breathing room from the frame edge).
    B.descLinkY = B.descY + B.descH - linkH - 8;
    // Update bar (the "[Details]" strip that appears when a mod has an
    // update available) floats just ABOVE the link stack when active.
    // It's hidden when no update — so it doesn't take up visible space
    // by default and won't push the buttons up.
    B.descUpdateBarY = B.descLinkY - B.descUpdateBarH - 4;
    B.descLinkH = linkH;
    B.descBodyY = B.descY + headerH + 4;
    B.descBodyH = B.descUpdateBarY - B.descBodyY - 8;

    // LAUNCH OPTIONS panel geometry (middle region of the asset)
    B.loX = B.rightX + side + 4;
    B.loY = midY0;
    B.loW = panelW - (side + 4) * 2;
    B.loH = midY1 - midY0;
    B.loFlagsY    = B.loY + headerH + 14;     // +14 nudge for breathing room below header
    B.loFlagColW  = (B.loW - 24) / BODY_FLAG_GRID_COLS;
    B.loFlagRowH  = 40;                       // tightened from 48 — claws back vertical room for the seed row + taller cmd box

    // Seed row sits below the flag grid, above the cmd preview. The
    // checkbox toggles whether -seed is appended; the dropdown picks
    // which value from assets/seeds.json gets used.
    int flagsBlockH = BODY_FLAG_GRID_ROWS * B.loFlagRowH;     // 120 logical
    B.loSeedY = B.loFlagsY + flagsBlockH + 14;
    B.loSeedH = 32;

    // Cmd preview sits below the seed row, near the bottom of the LO
    // panel. Y was loFlagsY + flagsBlockH(@48) + 14 in the previous
    // layout (no seed row). New Y = loFlagsY + flagsBlockH(@40) + 14
    // + seedH(32) + 17 = previous Y + 25 — was previously +35 to match
    // the original "drop the launch-args textbox 35 px" spec, then
    // tightened by 10 px so the cmd preview sits closer to the seed
    // row without crowding the PLAY button. Height starts at 44 and
    // may grow at paint time when the args wrap to 3 lines.
    B.loCmdPreviewH = 44;
    B.loCmdPreviewY = B.loSeedY + B.loSeedH + 17;

    // PLAY button geometry (bottom region of the asset). The asset gives
    // PLAY its own framed slot; we just center the button inside it.
    B.loLaunchH = botY1 - botY0 - 16;
    B.loLaunchY = botY0 + 8;

    return B;
}


// Geometry of the center mod-list panel (frame_panel_left.png). Drawn at
// NATIVE 1:1 (660×955) anchored at bodyTop, so its top edge sits just inside
// the frame_main top filigree. The asset is taller than frame_panel_right
// (which now starts lower at B.rightY), so the two panels are intentionally
// out of vertical sync. mainTop / dividerY map the asset's native landmarks
// (top-border interior at y=69, internal divider at y=804) — no scaling.
// (LeftPanelGeom struct moved to layout.h)
LeftPanelGeom ComputeLeftPanelGeom(const BodyLayout& B) {
    (void)B;                                       // no longer derived from right panel
    using namespace LO;
    FrameInset fi = MeasureFrameInset(L"frame_main.png");
    int bodyTop = fi.top + 8;                      // matches Layout()'s insetT
    int centerX = (fi.left + 12) + LEFT_RAIL_W;    // matches Layout()/PaintBody insetL
    LeftPanelGeom g;
    g.x = centerX + PANEL_LEFT_X_NUDGE;
    g.y = bodyTop + PANEL_LEFT_Y_NUDGE;
    g.w = 660;                                     // native asset width
    g.h = 955;                                     // native asset height
    g.mainTop  = g.y +  69;                        // native main-region interior top
    g.dividerY = g.y + 804;                        // native internal divider
    g.bottom   = g.y + g.h;
    return g;
}


// Single flag's pixel rect (used both in painting and hit-testing).
RECT BodyFlagRect(const BodyLayout& B, int flagIdx) {
    int col = flagIdx % BODY_FLAG_GRID_COLS;
    int row = flagIdx / BODY_FLAG_GRID_COLS;
    int x = B.loX + 8 + col * B.loFlagColW;
    int y = B.loFlagsY + row * B.loFlagRowH;
    return RECT{ x, y, x + B.loFlagColW - 4, y + B.loFlagRowH };
}


RECT BodySeedCheckRect(const BodyLayout& B) {
    int x = B.loX + 8;
    int y = B.loSeedY + (B.loSeedH - SEED_CB_H) / 2;
    return { x, y, x + SEED_CB_W, y + SEED_CB_H };
}

// Combined (input + arrow) chrome rect — for paint backdrop. The combo
// is anchored 30 logical px to the RIGHT OF THE RENDERED "Seed" LABEL
// (not the checkbox itself). Label X = checkbox.right + 8 (original
// post-checkbox padding). Label width is measured at font-creation
// time via g_seedLabelLogicalW so the gap stays a true 30 px regardless
// of which font family the user has selected.
RECT BodySeedComboRect(const BodyLayout& B) {
    RECT cb = BodySeedCheckRect(B);
    int labelX     = cb.right + 8;
    int labelEnd   = labelX + g_seedLabelLogicalW;
    int x          = labelEnd + 30;
    int y = B.loSeedY + (B.loSeedH - SEED_COMBO_H) / 2;
    return { x, y, x + SEED_COMBO_W, y + SEED_COMBO_H };
}

// Text input area — everything inside the combo EXCEPT the arrow slice.
RECT BodySeedInputRect(const BodyLayout& B) {
    RECT c = BodySeedComboRect(B);
    return { c.left, c.top, c.right - SEED_ARROW_W, c.bottom };
}

// Arrow button — the right SEED_ARROW_W slice of the combo.
RECT BodySeedArrowRect(const BodyLayout& B) {
    RECT c = BodySeedComboRect(B);
    return { c.right - SEED_ARROW_W, c.top, c.right, c.bottom };
}


// Show/hide the per-mod link buttons in the Mod Description panel based on
// which fields the selected mod's modinfo.json populates.
void RefreshModDescriptionLinks() {
    bool hasMod = (g_selMod >= 0 && g_selMod < (int)g_mods.size());
    auto show = [](HWND h, bool visible) {
        if (h) ShowWindow(h, visible ? SW_SHOW : SW_HIDE);
    };
    if (!hasMod) {
        show(g_hwModDiscord, false);
        show(g_hwModDocs,    false);
        show(g_hwModWebsite, false);
        return;
    }
    const ModInfo& mod = g_mods[g_selMod];
    show(g_hwModDiscord, !mod.discordUrl.empty());
    show(g_hwModDocs,    !mod.docsUrl.empty());
    show(g_hwModWebsite, !mod.websiteUrl.empty());
}


void Layout(int W, int H) {
    using namespace LO;

    // Callers (WM_SIZE, post-CreateControls init, etc.) pass PHYSICAL client
    // dimensions from GetClientRect or WM_SIZE's lparam. Convert to logical
    // at the top so the rest of Layout reads in a single coordinate system
    // (the same one all LO::* constants live in). SetWindowPos calls use
    // SPosL, which applies S() at the Win32 boundary.
    W = U(W);
    H = U(H);

    // Frame-aware insets: measure the actual filigree thickness from the
    // asset, then inset the whole body so content sits inside the frame.
    // A small extra breathing-room pad is added beyond the raw filigree.
    FrameInset fi = MeasureFrameInset(L"frame_main.png");
    const int padX = 12, padY = 8;
    int insetL = fi.left   + padX;
    // fi.right under-measures (sparse vertical filigree); fall back to
    // fi.bottom. No padX on this side — right panel flush with the inner
    // edge of frame_main's right filigree (matches ComputeBodyLayout()).
    int insetR = (fi.right >= 8) ? fi.right : fi.bottom;
    int insetT = fi.top    + padY;
    int insetB = fi.bottom + padY;

    // ── Body region ─────────────────────────────────────────────────────
    // The body region is constrained to where frame_main.png actually
    // covers — its native height (1024) — regardless of expansion state.
    // The expansion panel is drawn BELOW the main frame, not by shrinking
    // the body. Bottom-anchored content stays in place when the panel
    // opens; the new panel just appears underneath.
    int frameNativeH = 1024;
    if (Gdiplus::Bitmap* fm = AssetImage(L"frame_main.png"))
        frameNativeH = (int)fm->GetHeight();
    int bodyH = frameNativeH;

    int leftX     = insetL;
    int rightX    = W - RIGHT_COL_W - insetR;
    int centerX   = insetL + LEFT_RAIL_W;
    int centerW   = rightX - centerX;
    int bodyTop   = insetT;
    int bodyBot   = bodyH - insetB;
    (void)leftX; (void)bodyTop;

    // Body/panel geometry. Computed up front so the center column (MODS
    // title, Refresh, list) can be positioned relative to frame_panel_left:
    // the MODS title sits under the panel's top divider, the list fills the
    // main region below that, and the Nexus/Update row drops into the footer
    // below the panel's internal divider.
    BodyLayout B = ComputeBodyLayout(W, H);
    LeftPanelGeom lg = ComputeLeftPanelGeom(B);
    int modsTitleY = lg.mainTop + 4;         // below frame_panel_left's top divider

    // Refresh button — top-right of the center column, vertically centered
    // on the MODS title line. Sized to the btn_refresh.png asset's actual
    // native dimensions (queried at runtime, with a 138×52 fallback) so the
    // art isn't squished. No overflow pad: hover-grow has been disabled on
    // this kind, so the HWND rect equals the art rect.
    if (g_hwRefresh) {
        int W_REF = 138, H_REF = 52;
        if (Gdiplus::Bitmap* bm = AssetImage(L"btn_refresh.png")) {
            W_REF = (int)bm->GetWidth();
            H_REF = (int)bm->GetHeight();
        }
        int rx = lg.x + lg.w - LO::PANEL_LEFT_PAD - W_REF;
        int ry = modsTitleY + 22 - H_REF / 2;   // centered on title line
        SPosL(g_hwRefresh, nullptr, rx, ry, W_REF, H_REF, SWP_NOZORDER);
    }

    // ── Left rail (logo + nav stack + loader options) ───────────────────
    // Nav buttons stacked below the logo, sized to btn_nav.png native
    // dimensions (310×76). Centered horizontally within the rail. Each
    // HWND is enlarged by 2*BTN_OVERFLOW_PAD in both dimensions so the
    // hover scale-up has somewhere to render; the visible art stays
    // anchored at its intended pixel position.
    {
        constexpr int NAV_BTN_W   = 279;    // ~90% of 310 — slimmer so the gem caps clear the filigree
        constexpr int NAV_BTN_H   =  68;    // ~90% of 76, preserves aspect
        constexpr int NAV_BTN_GAP =   6;
        const int P = BTN_OVERFLOW_PAD;
        constexpr int NAV_X_NUDGE = 10;     // nudged 10 px right of pure-center
        int navX = insetL + (LEFT_RAIL_W - NAV_BTN_W) / 2 + NAV_X_NUDGE;
        // Logo is 203 px tall, sits 16 px below the frame top. The 24 px
        // (originally) was the gap to the MODS button — bumped to 30 to
        // make a clean band below the logo for the painted "vX.Y"
        // version label (see PaintBody, which positions the label
        // relative to the logo bottom). Without the bump the version
        // text sat inside the MODS button rect and got obscured by the
        // button HWND's paint.
        int navY = insetT + 16 + 203 + 30;

        // Table-driven nav button layout. Each entry binds the button's
        // user_layout.json id ("mods", "options", etc.) to its HWND.
        // Iteration applies visibility (ShowWindow + skip-position +
        // skip-navY-advance for reflow) and enabled state from the
        // layout-overrides accessors. The list order here defines
        // the visual stacking order top-to-bottom; hiding a button
        // makes the ones below it shift up to fill the gap.
        struct NavBtn { const wchar_t* id; HWND hw; };
        const NavBtn nav[] = {
            { L"mods",    g_hwNavMods    },
            { L"options", g_hwNavOptions },
            { L"logs",    g_hwNavLogs    },
            { L"help",    g_hwNavHelp    },
            { L"about",   g_hwNavAbout   },
            { L"exit",    g_hwNavExit    },
        };
        for (const auto& b : nav) {
            if (!b.hw) continue;
            bool visible = LayoutNavButtonVisible(b.id, true);
            bool enabled = LayoutNavButtonEnabled(b.id, true);

            ShowWindow(b.hw, visible ? SW_SHOW : SW_HIDE);
            EnableWindow(b.hw, enabled ? TRUE : FALSE);

            if (!visible) continue;     // skip position AND skip navY
                                        // advance → reflow

            SPosL(b.hw, nullptr,
                         navX - P, navY - P,
                         NAV_BTN_W + 2 * P, NAV_BTN_H + 2 * P,
                         SWP_NOZORDER);
            navY += NAV_BTN_H + NAV_BTN_GAP;
        }
    }

    // Loader Options section anchored to the bottom of the left rail.
    // Stack (top → bottom):
    //   LOADER OPTIONS header
    //   Loader Dir path bar + "..." (ellipse) button
    //   Stash Tabs dropdown
    //   Plugins button  (replaces the former Dmg Display dropdown)
    //
    // The Plugins button anchors the bottom of the section. Stash Tabs
    // and the path bar shift upward relative to the prior layout to
    // make room — net height is roughly the same since Plugins (54 tall)
    // is taller than the dropdown it replaced (28 tall).
    {
        constexpr int ELLIPSE_W = 37;     // aspect-correct for 36 tall
        constexpr int ELLIPSE_H = 36;
        constexpr int BAR_H     = 44;   // path bar height (taller so wrapped paths breathe)
        constexpr int LOADER_X_NUDGE = 28;   // 10 px right of the centered-in-rail position
        constexpr int ROW_H_DD  = 28;     // dropdown row height (Stash Tabs)
        constexpr int PLUGINS_W = 254;    // btn_nexus_update.png native width
        constexpr int PLUGINS_H = 54;     // btn_nexus_update.png native height
        constexpr int ROW_GAP   = 6;      // gap between Stash row and Plugins button
        constexpr int BAR_GAP   = 8;      // gap between path bar and Stash
        // dirW is constrained by the 300-px bg_loader_options.png frame
        // (which has ~12 px of bronze border on each side). The path bar
        // plus 6-px gap plus 37-px ellipse must fit inside the bronze.
        int navW = LEFT_RAIL_W - COL_PAD * 2 - LOADER_X_NUDGE;
        int dirW = 233;
        int loaderX = insetL + COL_PAD + LOADER_X_NUDGE;

        // Anchor: Plugins button bottom drives the whole block. The
        // -25 lifts the entire section 25 px above its natural bottom
        // baseline (matches the prior layout's vertical positioning).
        int sectionBot = bodyBot - 8 - 6 - 25;
        int pluginsBot = sectionBot;
        int pluginsTop = pluginsBot - PLUGINS_H;
        int stashBot   = pluginsTop - ROW_GAP;
        int stashTop   = stashBot - ROW_H_DD;
        int barBot     = stashTop - BAR_GAP;
        int barTop     = barBot - BAR_H;

        // Loader Dir path bar
        g_loaderDirRect = { loaderX, barTop, loaderX + dirW, barBot };

        // Ellipse button vertically centered on the path bar.
        int ellipseY = barTop + (BAR_H - ELLIPSE_H) / 2;
        if (g_hwLoaderDirBtn)
            SPosL(g_hwLoaderDirBtn, nullptr,
                         loaderX + dirW + 6, ellipseY,
                         ELLIPSE_W, ELLIPSE_H, SWP_NOZORDER);

        // Stash Tabs row rect — paint code reads this to draw the dropdown.
        g_stashDropdownRect = { loaderX, stashTop, loaderX + navW, stashBot };

        // Plugins button — centered horizontally in the Loader Options
        // column. Native 254×54 art; no overflow pad (the Plugins kind
        // has no hover-grow, so the HWND fits the art exactly).
        if (g_hwLoaderPlugins) {
            int pluginsX = loaderX + (navW - PLUGINS_W) / 2;
            SPosL(g_hwLoaderPlugins, nullptr,
                         pluginsX, pluginsTop, PLUGINS_W, PLUGINS_H,
                         SWP_NOZORDER);
        }
    }

    // ── Mod list (center column) ────────────────────────────────────────
    // Sized to exactly 5 visible rows; any extra mods are reachable via the
    // scrollbar. Top sits below the MODS title (and clears the frame's top
    // border); inset from the side borders by PANEL_LEFT_PAD. With the
    // frame's content-derived height, the bottom lands ~8 px above the
    // internal divider, and Nexus/Update sit in the footer just below it.
    int listTop = max(modsTitleY + 44 + 8, lg.mainTop + 6);
    // 5 rows + 4 inter-row gaps + 4 px tail. The +4 is sub-pixel safety:
    // at higher g_scale values (e.g. 4K @ 150%), the last row's bottom
    // edge lands a fraction of a pixel inside the list bitmap (5*96*1.275 =
    // 642.6 in a 643 px tall bitmap). Without the cushion, GDI+ can drop
    // the bottom row entirely during the initial paint and only redraw it
    // when a hover invalidates the row directly.
    int listBot = listTop + 5 * LO::ROW_H + 4 * 6 + 4;
    if (g_hwList) {
        SPosL(g_hwList, nullptr,
                     lg.x + LO::PANEL_LEFT_PAD, listTop,
                     lg.w - LO::PANEL_LEFT_PAD * 2, listBot - listTop,
                     SWP_NOZORDER);
    }

    // ── Center-column toolbar (On Launch / Scale / Font+Colour) ────────
    // Three-column block. The TITLE TIER is the topmost row of visible
    // content — ON LAUNCH header in the left column, Scale label+textbox
    // in the middle column, Font label+dropdown in the right column,
    // all sharing the same top Y so the three columns read as a row.
    //
    // Beneath the title tier:
    //   Left column   → OL textbox  (28 px below the title)
    //                 → OL slider   (38 px below the OL textbox)
    //   Middle column → Scale slider (36 px below the Scale textbox)
    //   Right column  → Colour dropdown (38 px below the Font dropdown)
    //
    // The OL slider's bottom anchors the block bottom; Scale slider and
    // Colour both end well above that, so the OL column is the tallest.
    //
    // Colour's RIGHT edge is aligned to Font's right edge (rather than
    // its left edge), so the dropdown chevrons sit at the same X. The
    // COLOR_W < FONT_W width difference (181 vs 241 logical) is absorbed
    // on Colour's LEFT — its column starts 60 px right of Font's left.
    //
    // Anchor: OL slider's BOTTOM lands 3 logical px above the expand
    // arrow's top (bodyBot − 59). With the +12 OL shift, the OL slider's
    // BOTTOM extends past where it used to be — past the arrow's top
    // (bodyBot − 56) by 9 px in Y. Different X (OL is under the left
    // column, arrow at window center), so no visual conflict.
    constexpr int OL_LABEL_W    = 150;    // "ON LAUNCH" header column width
    constexpr int OL_VALUE_W    =  90;    // textbox width (left-aligned in column)
    constexpr int TB_H          =  32;
    constexpr int HEADER_H      =  24;
    constexpr int SLIDER_W      =  53;
    constexpr int SLIDER_H      =  23;
    constexpr int SLIDER_GAP    =   4;
    constexpr int ROW_GAP       =   6;
    constexpr int COL_GAP       =  16;

    // Total toolbar row width = sum of column widths + 2 inter-col gaps.
    // The right column's width is the wider of FONT_W / COLOR_W since
    // Font and Colour share the column (Colour is shifted right within
    // it so its chevron aligns with Font's — see the right-column block
    // below). tbX0 centers the row in the center column.
    int rightColW = (TBL::FONT_W > TBL::COLOR_W) ? TBL::FONT_W : TBL::COLOR_W;
    int totalRowW = OL_LABEL_W + COL_GAP + TBL::SCALE_W + COL_GAP + rightColW;
    int tbX0      = centerX + (centerW - totalRowW) / 2;

    // Anchor: ON LAUNCH title's NEW Y = OLD title's Y + 12 (the down-12
    // shift). With the previous anchor at olSliderBottom = bodyBot − 59,
    // the previous title Y was bodyBot − 148. So the new title Y is
    // bodyBot − 136. Everything else flows from there.
    //
    //   title (= Scale textbox = Font dropdown)         Y = bodyBot − 136
    //   OL textbox                                       Y = title + 28
    //   OL slider                                        Y = title + 66
    //   OL slider bottom                                 Y = title + 89  = bodyBot − 47
    //
    // OL slider bottom now sits 9 px BELOW the arrow's top (bodyBot − 56),
    // but the OL slider is under the LEFT column and the arrow is at the
    // window center — different X, no visual conflict.
    int titleTierY = bodyBot - 136;

    int olHeaderY  = titleTierY;
    int olTextboxY = olHeaderY  + HEADER_H + SLIDER_GAP;
    int olSliderY  = olTextboxY + TB_H + ROW_GAP;

    // ── Left column: On Launch ─────────────────────────────────────────
    // Shifted 15 px right of the column origin so the OL controls don't
    // sit hard against the center-column inset — gives the "ON LAUNCH"
    // header some breathing room from the panel's left chrome.
    {
        int x = tbX0 + 15;
        g_onLaunchHeaderRect = { x, olHeaderY, x + OL_LABEL_W,
                                 olHeaderY + HEADER_H };
        g_onLaunchRect = { x, olTextboxY, x + OL_VALUE_W,
                           olTextboxY + TB_H };
        int olSliderX = x + (OL_VALUE_W - SLIDER_W) / 2;
        g_onLaunchSliderRect = { olSliderX, olSliderY,
                                 olSliderX + SLIDER_W, olSliderY + SLIDER_H };
    }

    // ── Middle column: Scale ───────────────────────────────────────────
    {
        int x = tbX0 + OL_LABEL_W + COL_GAP;
        int textboxY = titleTierY;
        int sliderY  = textboxY + TB_H + SLIDER_GAP;

        g_scaleDropdownRect = { x, textboxY, x + TBL::SCALE_W,
                                textboxY + TB_H };
        // Slider nudged 10 px right of its centered-under-textbox
        // position. The textbox itself stays put — only the slider
        // shifts so it sits visually under the Scale's value tier
        // (the "85%" text area) rather than under the column's center.
        int scaleSliderX = x + (TBL::SCALE_W - SLIDER_W) / 2 + 10;
        g_scaleSliderRect = { scaleSliderX, sliderY,
                              scaleSliderX + SLIDER_W, sliderY + SLIDER_H };
    }

    // ── Right column: Font + Colour stacked ────────────────────────────
    {
        int x = tbX0 + OL_LABEL_W + COL_GAP + TBL::SCALE_W + COL_GAP;
        int fontY    = titleTierY;
        int colourY  = fontY + TB_H + ROW_GAP;
        // Font occupies the full right-column width (FONT_W). Colour is
        // shifted right so its RIGHT edge matches Font's right edge —
        // this aligns the chevrons at the same X.
        int colourX  = x + (TBL::FONT_W - TBL::COLOR_W);

        g_fontDropdownRect  = { x, fontY, x + TBL::FONT_W,
                                fontY + TB_H };
        g_colorDropdownRect = { colourX, colourY,
                                colourX + TBL::COLOR_W, colourY + TB_H };
    }

    // Nexus Mod Directory + Update Selected Mod row.
    // Both buttons share the btn_nexus_update_*.png asset family at
    // native 254×54. Distributed across the center column with the
    // remaining horizontal space split as left margin, gap, right margin.
    // Sits in the strip BETWEEN the mod list and the toolbar — pushed
    // down from "listBot + 12" so the toolbar can claim the strip just
    // above the expand arrow (per the swap requested in Stage 6).
    constexpr int NU_W = 254;
    constexpr int NU_H = 54;
    const int NU_P = BTN_OVERFLOW_PAD;
    // Place Nexus/Update midway between the mod list and the toolbar so
    // the strip reads as evenly weighted. Falls back to a 12-px gap
    // below the list if the toolbar lands too close (small-scale edge).
    // titleTierY is the new "toolbar top" — where ON LAUNCH title / Scale
    // textbox / Font dropdown all sit (previously named tbY before the
    // 3-column refactor).
    int btnY = (listBot + (titleTierY - NU_H)) / 2;
    if (btnY < listBot + 12) btnY = listBot + 12;
    int gap  = max(8, (centerW - NU_W * 2) / 3);
    int btnXLeft  = centerX + (centerW - NU_W * 2 - gap) / 2;
    int btnXRight = btnXLeft + NU_W + gap;
    if (g_hwBrowseMods)
        SPosL(g_hwBrowseMods, nullptr,
                     btnXLeft - NU_P, btnY - NU_P,
                     NU_W + 2 * NU_P, NU_H + 2 * NU_P, SWP_NOZORDER);
    if (g_hwUpdateMod)
        SPosL(g_hwUpdateMod, nullptr,
                     btnXRight - NU_P, btnY - NU_P,
                     NU_W + 2 * NU_P, NU_H + 2 * NU_P, SWP_NOZORDER);

    // ── Right column: Mod Description (top half) + Launch Options ───────
    // (B / lg computed at the top of Layout.)

    // Mod Description link buttons — three square 85×85 buttons in a
    // single horizontal row at the bottom of the Mod Description panel.
    // Bottom Y matches where the OLD vertical 3-slot stack's slot-2
    // button bottomed out (LINK_H=54 + 2*(LINK_H+LINK_GAP) above
    // descLinkY = +174 logical), so the rest of the panel layout is
    // unaffected by the shape change.
    //
    // X placement uses 5 evenly-spaced anchors (indices 0..4) across
    // the available width. The number of VISIBLE buttons drives which
    // anchors get used:
    //     1 visible →  [2]            (center)
    //     2 visible →  [1, 3]
    //     3 visible →  [0, 2, 4]      (outer + center)
    // Visibility is set by RefreshModDescriptionLinks based on whether
    // each URL is present in the selected mod's modinfo.json. Order
    // within the row is fixed: Docs, Discord, Website (left to right
    // when all three are visible).
    //
    // No BTN_OVERFLOW_PAD here. These buttons have no hover-grow (just
    // click-shrink), so the HWND can be exactly the 85×85 visible art
    // size; the same exception the paint code handles for Refresh /
    // Ellipse / Arrow applies (added for the ModLink* kinds).
    constexpr int LINK_SQ = 85;
    int linkX = B.descX + 12;
    int linkW = B.descW - 24;
    int rowBottom = B.descLinkY + 174;   // = old slot-2 bottom (LINK_H=54, GAP=6)
    int rowY      = rowBottom - LINK_SQ;
    auto anchorX = [&](int i) {
        // 0..4 evenly spaced across (linkW - LINK_SQ); anchor 0 sits flush
        // left, anchor 4 sits flush right, anchor 2 dead center.
        return linkX + (i * (linkW - LINK_SQ)) / 4;
    };
    HWND linkBtns[3] = { g_hwModDocs, g_hwModDiscord, g_hwModWebsite };
    int  visibleIdx[3];
    int  nv = 0;
    for (int i = 0; i < 3; ++i)
        if (linkBtns[i] && IsWindowVisible(linkBtns[i])) visibleIdx[nv++] = i;
    // Anchor-index lookup per visible count.
    static const int ANCHORS[4][3] = {
        { -1, -1, -1 },     // 0 visible — nothing to do
        {  2, -1, -1 },     // 1 visible
        {  1,  3, -1 },     // 2 visible
        {  0,  2,  4 },     // 3 visible
    };
    for (int i = 0; i < nv; ++i) {
        int ax = anchorX(ANCHORS[nv][i]);
        SPosL(linkBtns[visibleIdx[i]], nullptr,
              ax, rowY, LINK_SQ, LINK_SQ, SWP_NOZORDER);
    }

    // PLAY button sized to btn_play_*.png native dimensions (422×102),
    // centered within the panel's PLAY slot (B.loLaunch[Y|H] defines the
    // slot from the panel asset's third interior region).
    if (g_hwLaunch) {
        constexpr int PLAY_W = 422, PLAY_H = 102;
        const int P = BTN_OVERFLOW_PAD;
        int slotX = B.rightX;
        int slotW = B.rightW;
        int slotY = B.loLaunchY;
        int slotH = B.loLaunchH;
        int playX = slotX + (slotW - PLAY_W) / 2;
        int playY = slotY + (slotH - PLAY_H) / 2;
        SPosL(g_hwLaunch, nullptr,
                     playX - P, playY - P,
                     PLAY_W + 2 * P, PLAY_H + 2 * P, SWP_NOZORDER);
    }

    // ── Bottom expansion arrow toggle ───────────────────────────────────
    // Sized to btn_expand_arrow_*.png native (68×50), centered horizontally
    // in the open strip between the mod list bottom and the Nexus/Update
    // button row. Stays put when the panel expands.
    //
    // Visible state honors LayoutShowModdingExpand(true) — when modders
    // set show_modding_expand: false in user_layout.json, the toggle is
    // hidden via ShowWindow(SW_HIDE) and the modding section can never
    // open. The bottom expansion panel below this block also reads
    // g_bottomExpanded, which is forced false in wWinMain when the
    // override hides the toggle, so the panel never opens either.
    if (g_hwExpandToggle) {
        bool showToggle = LayoutShowModdingExpand(true);
        ShowWindow(g_hwExpandToggle, showToggle ? SW_SHOW : SW_HIDE);
        if (showToggle) {
            constexpr int ARROW_W = 68, ARROW_H = 50;
            // No overflow pad: HWND sized exactly to the visible art (the
            // chevron has no hover-scale, so it doesn't need headroom).
            int stripTop = bodyBot - 124 + 4 + 60;       // moved 60px down (was 90)
            int stripBot = bodyBot - 58 - 4 + 60;
            int ax = (W - ARROW_W) / 2;
            int ay = stripTop + (stripBot - stripTop - ARROW_H) / 2;
            SPosL(g_hwExpandToggle, nullptr,
                         ax, ay, ARROW_W, ARROW_H, SWP_NOZORDER);
        }
    }

    // ── Bottom expansion panel (only laid out when visible) ─────────────
    if (g_bottomExpanded) {
        // Inset content by frame_expand.png's measured filigree, so the
        // button rows sit inside the bronze border. Falls back to a small
        // default inset if the asset is missing.
        FrameInset fe = MeasureFrameInset(L"frame_expand.png");
        int feL = (fe.left  > 0 ? fe.left  : 16) + 8;
        int feR = (fe.right > 0 ? fe.right : 16) + 8;
        int feT = (fe.top   > 0 ? fe.top   : 22) + 4;

        int panelTop = bodyH + feT;         // below the frame's top filigree
        int panelH   = EXPAND_H;
        int colGap   = EXP_COL_GAP;          // internal Tools-column gap (no divider)
        int secGap   = EXP_SEC_GAP;          // section gap (holds a divider)
        const int P  = BTN_OVERFLOW_PAD;

        // Button dimensions: narrowed from nav width (see LO::EXP_BTN_W) so
        // each button sits inside its section with padding, and the section
        // gaps stay wide enough that the dividers clear the button HWNDs'
        // overflow pads. Height is shorter (58 vs 76); 9-slice rendering
        // handles the mismatch so the gem corners stay pixel-perfect.
        int sec1W = EXP_BTN_W;
        int sec3W = EXP_BTN_W;
        // Section 2 (Tools, 2-column) is twice as wide plus the internal gap.
        int sec2W = EXP_BTN_W * 2 + colGap;
        // Distribute leftover space symmetrically as left/right edge padding.
        int framedW  = W - feL - feR;
        int totalBtnW = sec1W + sec2W + sec3W;
        int slack    = framedW - totalBtnW - secGap * 2;
        int leftPad  = slack / 2;
        int sec1X = feL + leftPad;
        int sec2X = sec1X + sec1W + secGap;
        int sec3X = sec2X + sec2W + secGap;

        // Divider centers (mirror PaintBottomPanel). The single-column
        // sections are centered within the space between the panel edge and
        // the nearest divider — not left/right-anchored at sec1X/sec3X — so
        // References and Downloads sit centered in their columns.
        int div1X = sec1X + sec1W + secGap / 2;
        int div2X = sec2X + sec2W + secGap / 2;
        int refBtnX = (feL + div1X - EXP_BTN_W) / 2;
        int dlBtnX  = (div2X + (W - feR) - EXP_BTN_W) / 2;

        int hdrH    = EXP_HDR_H;            // sub-section header band (tightened)
        int rowH    = EXP_BTN_H;
        int rowGap  = EXP_ROW_GAP;          // breathing room between rows (tightened)
        int firstRowY = panelTop + hdrH + 8;

        // Section 1: REFERENCES (3 stacked, single column — centered in column)
        for (int i = 0; i < 3; ++i) {
            if (!g_hwBottomRefs[i]) continue;
            int ry = firstRowY + i * (rowH + rowGap);
            SPosL(g_hwBottomRefs[i], nullptr,
                         refBtnX - P, ry - P,
                         EXP_BTN_W + 2 * P, EXP_BTN_H + 2 * P, SWP_NOZORDER);
        }

        // Section 2: TOOLS AND PROGRAMS (2 columns × 3 rows)
        //   col A: Edit TXT (0), Edit Sprite (1), Edit JSON (2)
        //   col B: Edit Particles (5), Edit Textures (4), Edit Models (3)
        int colAX = sec2X;
        int colBX = sec2X + EXP_BTN_W + colGap;
        int colAOrder[3] = { 0, 1, 2 };
        int colBOrder[3] = { 5, 4, 3 };
        for (int r = 0; r < 3; ++r) {
            int ry = firstRowY + r * (rowH + rowGap);
            if (g_hwBottomTools[colAOrder[r]])
                SPosL(g_hwBottomTools[colAOrder[r]], nullptr,
                             colAX - P, ry - P,
                             EXP_BTN_W + 2 * P, EXP_BTN_H + 2 * P, SWP_NOZORDER);
            if (g_hwBottomTools[colBOrder[r]])
                SPosL(g_hwBottomTools[colBOrder[r]], nullptr,
                             colBX - P, ry - P,
                             EXP_BTN_W + 2 * P, EXP_BTN_H + 2 * P, SWP_NOZORDER);
        }

        // Section 3: DOWNLOADS (3 stacked, single column — centered in column)
        for (int i = 0; i < 3; ++i) {
            if (!g_hwBottomDls[i]) continue;
            int ry = firstRowY + i * (rowH + rowGap);
            SPosL(g_hwBottomDls[i], nullptr,
                         dlBtnX - P, ry - P,
                         EXP_BTN_W + 2 * P, EXP_BTN_H + 2 * P, SWP_NOZORDER);
        }
    }

    // NOTE: Layout() only positions children — it does NOT invalidate.
    // Callers invalidate the precise region they changed (see WM_SIZE,
    // ML_NOTIFY_SELECT, and the expand-toggle handler).
}


// Called when g_bottomExpanded flips — grows or shrinks the window, then
// re-runs Layout() to settle child positions. newH is computed in logical
// pixels (matching LO::) then scaled through S() at the SetWindowPos
// boundary, which is what Win32 expects.
void RepositionForExpansion() {
    if (!g_hwMain) return;
    RECT rc; GetWindowRect(g_hwMain, &rc);
    int newH = g_bottomExpanded ? (LO::WIN_H + LO::EXPAND_H) : LO::WIN_H;
    SetWindowPos(g_hwMain, nullptr, 0, 0,
                 rc.right - rc.left, S(newH),
                 SWP_NOMOVE | SWP_NOZORDER);
}


// ═══════════════════════════════════════════════════════════════════════
//  MAIN WINDOW PROC
// ═══════════════════════════════════════════════════════════════════════

void RefreshMods() {
    g_mods.clear();
    g_mods = FindMods(g_cfg.d2rPath);

    // Restore last-selected mod if it still exists
    g_selMod = -1;
    if (!g_cfg.lastMod.empty()) {
        for (size_t i = 0; i < g_mods.size(); ++i) {
            if (_wcsicmp(g_mods[i].folder.c_str(), g_cfg.lastMod.c_str()) == 0) {
                g_selMod = (int)i;
                break;
            }
        }
    }
    // Load that mod's saved flag state (or defaults if no save exists)
    if (g_selMod >= 0) LoadModSettings(g_mods[g_selMod]);
    else               g_modSettings = ModSettings{};

    // Tell child controls to re-render (re-wired in later commits as
    // the new layout's child windows come online).
    if (g_hwList) {
        SendMessage(g_hwList, WM_USER + 1, 0, 0);   // ML_REFRESH
    }
    RefreshModDescriptionLinks();
    // Re-run Layout so the link buttons that RefreshModDescriptionLinks
    // just toggled ShowWindow on actually land in their painted slots.
    // Without this, on the very first paint after startup the link
    // buttons sit at their creation position (0, 0) because they're
    // visible but Layout's "only-position-visible" loop never had a
    // chance to run between the show and the paint. Mirrors the pattern
    // used in the ML_NOTIFY_SELECT handler in MainProc.
    if (g_hwMain) {
        RECT rc; GetClientRect(g_hwMain, &rc);
        Layout(rc.right, rc.bottom);
    }
    if (g_hwLaunch) EnableWindow(g_hwLaunch, g_selMod >= 0);
    // Repaint the main window so the launch-options checkboxes and cmd
    // preview reflect the freshly-loaded g_modSettings for g_selMod.
    // Without this the first-paint values (defaults / blanks) stick.
    if (g_hwMain) InvalidateRect(g_hwMain, nullptr, FALSE);
}

