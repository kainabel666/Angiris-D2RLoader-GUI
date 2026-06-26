// ═══════════════════════════════════════════════════════════════════════
//  layout.h
// ═══════════════════════════════════════════════════════════════════════
//
//  User-facing UI customization layer. Modders drop an optional file
//  at assets\user_layout.json next to the .exe; this module reads it
//  at startup and exposes accessor functions that the rest of the
//  launcher uses to look up overridable values.
//
//  Design:
//
//    1. Defaults are authoritative. The launcher ships with hardcoded
//       values that work. The JSON only carries DELTAS — every field
//       is std::optional, and missing-key means "use the default."
//       A modder who only wants to hide the expand arrow writes three
//       lines, not 200.
//
//    2. Read once at startup, never reloaded. Hot-reloading mid-paint
//       opens an enormous worm-can (which paint pass sees which value,
//       what happens when reflow timing collides with a click, etc.)
//       so this is deliberately frozen for the session.
//
//    3. Hidden vs disabled are different. Hidden = not drawn, not
//       clickable, the layout REFLOWS so other buttons shift up.
//       Disabled = drawn greyed out, not clickable, layout unchanged.
//       Both are independent fields per button.
//
//    4. Accessors take a default and return the override-or-default.
//       Calling code stays close to its original form — wrap a call
//       in the accessor instead of changing the surrounding logic.
//
//  Sample assets\user_layout.json:
//
//    {
//      "mod_row_height": 80,
//      "version_label": { "x": 24, "y": 480 },
//      "show_modding_expand": false,
//      "nav_buttons": {
//        "mods":    { "visible": true,  "enabled": true  },
//        "logs":    { "visible": false                   },
//        "about":   { "visible": true,  "enabled": false },
//        "exit":    { "visible": true                    }
//      }
//    }
//
//  Depends on core (ReadTextFile, JsonStr/Int/Bool, AppDir).
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"
#include <optional>

// Per-button override fields. Missing field = inherit default.
struct NavButtonOverride {
    std::optional<bool> visible;
    std::optional<bool> enabled;
};

// Whole-document state. One global instance lives in layout.cpp,
// populated by LoadLayoutOverrides() and read via the accessor
// functions below. Direct access via g_layout works too but should
// be limited to wWinMain bootstrap.
struct LayoutOverrides {
    std::optional<int>  modRowHeight;
    std::optional<int>  versionLabelX;
    std::optional<int>  versionLabelY;
    std::optional<bool> showModdingExpand;

    // Per-button overrides keyed by id string ("mods", "options",
    // "logs", "help", "about", "exit"). Missing key = no override
    // for that button; default-constructed NavButtonOverride means
    // the key was present but both fields were missing.
    std::map<wstring, NavButtonOverride> navButtons;
};

extern LayoutOverrides g_layout;

// ── Lifecycle ────────────────────────────────────────────────────────

// Read <appdir>\assets\user_layout.json if present and populate
// g_layout. Missing file → no-op (g_layout stays default-constructed
// with all fields std::nullopt). Malformed JSON → silently skip the
// bad fields; whatever parsed cleanly applies. Called once from
// wWinMain BEFORE any paint or layout code runs.
void LoadLayoutOverrides();

// ── Accessors (override OR default) ──────────────────────────────────
//
// Every accessor takes the launcher's hardcoded default and returns
// either the user's override (if present) or the default. Read-only
// — overrides are write-once at startup.

int  LayoutModRowHeight       (int  defaultPx);
int  LayoutVersionLabelX      (int  defaultX);
int  LayoutVersionLabelY      (int  defaultY);
bool LayoutShowModdingExpand  (bool defaultVisible);

// Per-button accessors. `id` is the lowercase button id ("mods",
// "options", "logs", "help", "about", "exit"). Defaults are
// `visible=true, enabled=true` — i.e. no override means the button
// is fully active.
bool LayoutNavButtonVisible(const wchar_t* id, bool defaultVisible = true);
bool LayoutNavButtonEnabled(const wchar_t* id, bool defaultEnabled = true);

// ═══════════════════════════════════════════════════════════════════════
//  LO :: Launcher Layout constants
// ═══════════════════════════════════════════════════════════════════════
//
//  Hardcoded layout values consumed by paint code, Layout(), and the
//  mod list. Promoted from Angiris.cpp to a header so the mod_list,
//  hover_tip, and (future) dialog modules can read them directly
//  rather than reaching back through a callback API.
//
//  Most members are constexpr — values that the launcher's release
//  build treats as immutable. ROW_H is the lone exception: it's
//  reassigned in wWinMain to honor the user_layout.json override,
//  which means it cannot be constexpr. The mutable ROW_H lives in
//  layout.cpp (extern here); every other LO:: member is a header-only
//  constexpr.
//
namespace LO {
    // Window dimensions (collapsed). Expanded state adds EXPAND_H to the height.
    // Collapsed = 1536×1024 (matches frame_main.png native size). When the
    // bottom expansion panel opens, the window grows by EXPAND_H to make
    // room for frame_expand.png (future batch). The artist's composite
    // target of 1536×1324 represents collapsed + EXPAND_H combined.
    constexpr int WIN_W       = 1536;
    constexpr int WIN_H       = 1024;
    constexpr int EXPAND_H    =  300;   // bottom expansion panel extra height

    // Body column widths (new layout). LEFT_RAIL_W is sized to hold the
    // 322px logo image with margin AND the 310px nav button assets; the
    // wider 344 leaves ~17px of pad on each side of the button. RIGHT_COL_W
    // matches the native width of frame_panel_right.png so it drops in at
    // 1:1 pixel scale.
    constexpr int LEFT_RAIL_W   = 344;
    constexpr int RIGHT_COL_W   = 489;   // matches frame_panel_right.png native width
    constexpr int COL_PAD       =  16;

    // Center mod-list panel frame (frame_panel_left.png; native 660×955, top
    // border ~y62, internal divider ~y804). Drawn at NATIVE 1:1 — no stretch —
    // anchored at (centerX + X_NUDGE, bodyTop + Y_NUDGE). PAD insets the
    // title/list/refresh from the side borders. The shared top centerpiece
    // (ornament_gem.png, via PaintTopOrnament) is drawn AFTER this panel so it
    // layers on top of any overlap with the panel's upper filigree band.
    constexpr int PANEL_LEFT_X_NUDGE = 0;
    constexpr int PANEL_LEFT_Y_NUDGE = -12;    // overlaps the frame_main top filigree band by 4 px
    constexpr int PANEL_LEFT_PAD     = 28;

    // Mod list row sizing. Was constexpr; relaxed to plain `int` (defined
    // in layout.cpp, extern here) so wWinMain can apply the user_layout.json
    // override at startup via LayoutModRowHeight(). All read sites compute
    // at runtime anyway — none required constexpr.
    extern int ROW_H;

    // Button overflow pad. Owner-drawn buttons can scale up on hover
    // (typically 4-8% via state transforms), and the scaled-up pixels
    // would otherwise be clipped at the HWND boundary. Every button's
    // HWND is sized with this much invisible padding on each side; the
    // visible art still draws at its native dimensions inside, but
    // the hover-scaled edges have room to render. Also makes the
    // click hit-zone slightly more forgiving.
    constexpr int BTN_OVERFLOW_PAD = 12;

    // Top centerpiece gem ornament (ornament_gem.png — authored 255×108 with
    // the sapphire dead-centre). Drawn centered horizontally over the top
    // filigree band. ORNAMENT_TOP_Y is the gap from the window's top edge:
    // 0 keeps the crest flush with the top, positive values nudge the whole
    // ornament downward. (We can't draw above y=0, so the crest can't be
    // raised past flush.)
    constexpr int ORNAMENT_TOP_Y = 0;

    // Frame corner accents (corner_tl / _tr / _bl / _br .png). Each bracket's
    // dense elbow seats at the inner edge of frame_main.png's border filigree
    // (the content-opening corner). INSET is an extra nudge from that edge:
    // positive pushes further into the content, negative pulls back toward /
    // onto the filigree. 0 = flush against the inner filigree edge.
    constexpr int CORNER_ACCENT_INSET_X = 0;
    constexpr int CORNER_ACCENT_INSET_Y = 0;

    // Bottom expand-panel vertical dividers (left/right_expand_divider.png,
    // each 27×248 — a mirrored pair). Centered in the two gaps that flank the
    // central Tools column, top-aligned to the section content. TOP_PAD nudges
    // them up/down from the header line (negative = up; current −6 tucks the
    // top under the panel filigree); X_NUDGE shifts both dividers laterally
    // to line up with verticals elsewhere on the main window; FALLBACK_H is
    // the height of the programmatic rule when the PNG is missing.
    constexpr int EXP_DIVIDER_TOP_PAD    = -6;
    constexpr int EXP_DIVIDER_X_NUDGE    =  0;
    constexpr int EXP_DIVIDER_FALLBACK_H = 248;

    // Bottom expand-panel button grid (shared by Layout positioning and
    // PaintBottomPanel chrome). EXP_BTN_W is narrowed from the 310px nav
    // width so each button sits inside its section with padding instead of
    // bleeding into the dividers. EXP_SEC_GAP is the wide gap between
    // sections that holds a divider — sized so the gap between adjacent
    // button HWNDs (EXP_SEC_GAP − 2·BTN_OVERFLOW_PAD) clears the 27px divider
    // with headroom on both sides. EXP_COL_GAP is the tight internal gap
    // between the two Tools columns (no divider there).
    constexpr int EXP_BTN_W   = 280;
    constexpr int EXP_BTN_H   =  58;
    constexpr int EXP_SEC_GAP =  64;
    constexpr int EXP_COL_GAP =  24;
    // Section-title band height and inter-row gap. Tightened (from 44/12) so
    // the three button rows clear the panel's bottom filigree, and matched to
    // the smaller g_fExpHdr title font.
    constexpr int EXP_HDR_H   =  30;
    constexpr int EXP_ROW_GAP =   8;
}

// ═══════════════════════════════════════════════════════════════════════
//  BODY LAYOUT — geometry for the right panel + per-control rects
// ═══════════════════════════════════════════════════════════════════════
//
//  Phase 7c promoted these from Angiris.cpp file-static so paint_main.cpp
//  can see them. Definitions still live in Angiris.cpp (Phase 7d moves
//  them here for real); these are just forward decls + the struct
//  layouts they need.

// Right column overall + MOD DESCRIPTION + LAUNCH OPTIONS panel geometry.
// Computed from the frame asset's measured inset + panel divider rows by
// ComputeBodyLayout. Consumed by both paint code (paint_main.cpp) and
// hit-test code (MainProc in Angiris.cpp).
struct BodyLayout {
    // Right column overall
    int rightX, rightY, rightW, rightH;

    // MOD DESCRIPTION panel
    int descX, descY, descW, descH;
    int descBodyY, descBodyH;       // text region under the header
    int descLinkY, descLinkH;       // Discord/Docs/Website stacked region
    int descUpdateBarY;             // 24px strip at the bottom for the update bar
    int descUpdateBarH;

    // LAUNCH OPTIONS panel
    int loX, loY, loW, loH;
    int loFlagsY;                   // top of the 6-flag grid
    int loFlagColW;                 // width of one flag column
    int loFlagRowH;                 // height of one flag row
    int loSeedY;                    // top of the seed row (checkbox + dropdown)
    int loSeedH;                    // seed row height
    int loCmdPreviewY;              // cmd preview text bar (top)
    int loCmdPreviewH;              // base height; paint may grow it dynamically
    int loLaunchY;                  // PLAY button top
    int loLaunchH;
};

constexpr int BODY_FLAG_GRID_COLS = 2;
constexpr int BODY_FLAG_GRID_ROWS = 3;       // 6 flags = 2 cols × 3 rows

// Left panel (mod list + nav) framed geometry pulled from
// frame_panel_left.png at native scale.
struct LeftPanelGeom { int x, y, w, h; int mainTop, dividerY, bottom; };

// Pure functions of W/H (or B). Definitions in Angiris.cpp for Phase 7c;
// Phase 7d moves them into layout.cpp.
BodyLayout    ComputeBodyLayout(int W, int H);
LeftPanelGeom ComputeLeftPanelGeom(const BodyLayout& B);

// Per-control rects derived from BodyLayout. Used by both paint and
// hit-test paths so the visible shape and the click target match.
RECT BodyFlagRect(const BodyLayout& B, int flagIdx);
RECT BodySeedCheckRect(const BodyLayout& B);
RECT BodySeedComboRect(const BodyLayout& B);
RECT BodySeedInputRect(const BodyLayout& B);
RECT BodySeedArrowRect(const BodyLayout& B);

// ═══════════════════════════════════════════════════════════════════════
//  TOOLBAR (Scale / Font / Colour) WIDTHS
// ═══════════════════════════════════════════════════════════════════════
//
// Center-column toolbar widths sized for the all-caps Exocet font at
// SF(13). Each control = label area + 1px gap + value box. Hand-tuned;
// Colour needs the most room (6 chars × ~19 logical px = ~114). FONT
// value box sized for the abbreviated face label ("Cinz-Bol").
namespace TBL {
    constexpr int SCALE_LABEL_W = 100;    // "SCALE"  (5 caps)
    constexpr int SCALE_VALUE_W =  70;    // room for "127%" centered
    constexpr int SCALE_W       = SCALE_LABEL_W + 1 + SCALE_VALUE_W;   // 171

    constexpr int FONT_LABEL_W  =  80;    // "FONT"   (4 caps)
    constexpr int FONT_VALUE_W  = 160;    // room for "Cin-Bol" + chevron
    constexpr int FONT_W        = FONT_LABEL_W + 1 + FONT_VALUE_W;     // 241

    constexpr int COLOR_LABEL_W = 120;    // "COLOUR" (6 caps)
    constexpr int COLOR_VALUE_W =  60;    // small square swatch + chevron
    constexpr int COLOR_W       = COLOR_LABEL_W + 1 + COLOR_VALUE_W;   // 181
}

// Title-bar button geometry (minimize + close at top-right of the
// frame). Promoted from Angiris.cpp file-static so paint_main.cpp can
// position the two buttons during PaintBody.
constexpr int TB_BTN_W       = 43;
constexpr int TB_BTN_H       = 42;
constexpr int TB_BTN_GAP     = 14;     // between minimize and close
constexpr int TB_BTN_INSET_R = 58;     // from right edge of the frame
constexpr int TB_BTN_INSET_T = 30;     // tucked just under the top filigree

// Seed-row sub-control dimensions. Promoted so the Body*Rect helpers
// in layout.cpp can use them without depending on private Angiris.cpp
// constants. SEED_CB_* = checkbox glyph; SEED_COMBO_* = input+arrow
// chrome total; SEED_ARROW_W = arrow button slice on the right.
constexpr int SEED_CB_W    = 27;
constexpr int SEED_CB_H    = 28;
constexpr int SEED_COMBO_W = 160;     // total width of input + arrow
constexpr int SEED_COMBO_H = 26;
constexpr int SEED_ARROW_W = 24;      // arrow button slice on the right edge

// ── Phase 7d additions: layout orchestration ─────────────────────────

// Position every child window in the main window's client area.
// Called from WM_SIZE and after any state change that affects which
// controls are visible (mod selection, bottom-panel expansion, etc.).
// Reads layout overrides from layout.json (Phase 4b) so per-deployment
// tweaks can move buttons without recompiling.
void Layout(int W, int H);

// Re-do layout in response to the bottom panel toggling open/closed.
// Resizes the main window's outer height to accommodate the expansion
// area, then re-runs Layout to reposition everything in the new size.
void RepositionForExpansion();

// Rescan the mod folder, repopulate g_mods, restore last-selected mod,
// load that mod's per-mod settings, refresh the link buttons, re-run
// Layout, and trigger a repaint. Called from the Refresh button, the
// folder watcher (after the user clicks Refresh on stale-list highlight),
// and on startup after the D2R path is resolved.
void RefreshMods();

// Re-evaluate which of the three per-mod link buttons (Discord/Docs/
// Website) should be visible for the currently-selected mod. Reads
// g_mods[g_selMod].discord/docs/website and ShowWindow's accordingly.
// Layout must be called after this for the buttons to actually move
// to their new screen positions.
void RefreshModDescriptionLinks();
