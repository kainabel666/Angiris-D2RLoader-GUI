// ═══════════════════════════════════════════════════════════════════════
//  Angiris — D2R Mod Launcher
// ═══════════════════════════════════════════════════════════════════════
//
//  A standalone launcher for Diablo II: Resurrected mods.
//  Design reference: Angiris_Design_Specification.txt
//
//  All UI is custom-painted via GDI+. Win32 children are used only for
//  the EDIT/BUTTON controls that need keyboard/clipboard support.
//
// ═══════════════════════════════════════════════════════════════════════
//
//  NAMING CONVENTIONS (Win32 + GDI+ idiom)
//  ────────────────────────────────────────────────────────────────────
//  These short names appear constantly throughout the file. They follow
//  the conventions you'll see in MSDN documentation and most Win32 code,
//  so once you know them they read naturally.
//
//  ── Win32 message-procedure parameters ──
//    hw, hwnd        Window handle (HWND). The "h" stands for "handle".
//    msg             Message ID (UINT) — e.g. WM_PAINT, WM_COMMAND.
//    wp, wParam      WPARAM — message-specific data, usually a flag/ID.
//    lp, lParam      LPARAM — message-specific data, usually a pointer
//                    or packed coords (use LOWORD/HIWORD or
//                    GET_X_LPARAM/GET_Y_LPARAM to unpack).
//
//  ── Drawing / paint primitives ──
//    hdc             Device Context handle (HDC) — the "canvas" you draw
//                    onto. Returned by BeginPaint, GetDC, etc.
//    ps              PAINTSTRUCT — info about the current WM_PAINT call
//                    (ps.rcPaint is the dirty rectangle to repaint).
//    rc              RECT — a {left, top, right, bottom} rectangle.
//    g               GDI+ Graphics object — wraps an HDC and provides
//                    the modern drawing API (antialiasing, gradients).
//    sf              StringFormat — text alignment / wrapping options.
//
//  ── Type-prefix conventions ──
//    g_              Global variable. Anything mutable that lives
//                    outside a function uses this prefix.
//    g_hw…           Global HWND. e.g. g_hwMain, g_hwHero.
//    g_ff…           Global FontFamily pointer. e.g. g_ffCinzel.
//    g_f…            Global Font pointer. e.g. g_fHeroName.
//    IDC_…           Control ID constant (sent in WM_COMMAND).
//    WM_…            Standard Windows messages (defined by Windows).
//    MSG_…           Our custom messages, all WM_USER + N values.
//    IDT_…           Timer IDs we pass to SetTimer.
//    LO::            Layout constants (window dimensions, panel sizes).
//    Tok::           Design tokens (colors, mainly).
//    OP / ML / MD…   Per-section namespaces (Options panel, Mod List,
//                    Modding column) — both for constants and helpers.
//
//  ── State pointers in window procedures ──
//    st              Pointer to that window's state struct. Each custom
//                    window (Options panel, Mod List, etc.) has its own
//                    state map keyed by HWND.
//
//  ── Misc shorthand ──
//    W, H            Width / height of the current paint region.
//    cx, cy          Center coordinates of something (e.g. a button).
//    REAL            GDI+ float type (alias for float).
//    GP(r,g,b)       Make an opaque GDI+ Color.
//    GPA(a,r,g,b)    Make a GDI+ Color with explicit alpha.
//
// ═══════════════════════════════════════════════════════════════════════

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <thread>
#include <atomic>
#include <mutex>
#include <winhttp.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winhttp.lib")

using namespace std;
using namespace Gdiplus;

// ═══════════════════════════════════════════════════════════════════════
//  DESIGN TOKENS
//  All colors and spacing live here — extend this block, never inline.
// ═══════════════════════════════════════════════════════════════════════

// GDI+ Color shortcuts: GP = fully opaque, GPA = with explicit alpha
static inline Color GP (int r, int g, int b)         { return Color(255, r, g, b); }
static inline Color GPA(int a, int r, int g, int b)  { return Color(a, r, g, b); }

namespace Tok {
    // Backgrounds
    const Color BgDeep      = GP(0x0A, 0x07, 0x05);
    const Color BgPanel     = GP(0x1A, 0x11, 0x08);
    const Color BgPanel2    = GP(0x24, 0x17, 0x10);

    // Gold accents. NON-const: the toolbar Colour dropdown reassigns
    // these at runtime so the user's pick takes effect across every
    // paint site that already uses Tok::Gold / Tok::GoldBright. The
    // initial values here are the launcher's default look.
    Color Gold        = GP(0xC8, 0xA8, 0x4B);
    Color GoldBright  = GP(0xFF, 0xD7, 0x00);
    const Color GoldDim     = GP(0x8B, 0x69, 0x14);
    const Color GoldDeep    = GP(0x6B, 0x4F, 0x1A);

    // Bronze structural
    const Color Bronze      = GP(0x5C, 0x3A, 0x1E);
    const Color BronzeDim   = GP(0x3A, 0x25, 0x10);
    const Color BronzeBright= GP(0x8B, 0x5A, 0x2B);

    // Launch red
    const Color RedDark     = GP(0x64, 0x00, 0x00);
    const Color RedBright   = GP(0xDC, 0x28, 0x00);
    const Color RedGlow     = GP(0xFF, 0x48, 0x18);

    // Text
    const Color TextParchment = GP(0xD4, 0xB4, 0x83);
    const Color TextDim       = GP(0x78, 0x60, 0x40);
    const Color ParchmentInk  = GP(0x2A, 0x1A, 0x0A);

    // GDI COLORREFs (for native controls / WM_CTLCOLOR)
    const COLORREF crBgDeep    = RGB(0x0A, 0x07, 0x05);
    const COLORREF crBgPanel   = RGB(0x1A, 0x11, 0x08);
    const COLORREF crGold      = RGB(0xC8, 0xA8, 0x4B);
    const COLORREF crGoldBright= RGB(0xFF, 0xD7, 0x00);
    const COLORREF crBronzeDim = RGB(0x3A, 0x25, 0x10);
    const COLORREF crText      = RGB(0xD4, 0xB4, 0x83);
}

// Spacing scale — only these values used
namespace Sp {
    constexpr int s1 =  4;
    constexpr int s2 =  8;
    constexpr int s3 = 12;
    constexpr int s4 = 16;
    constexpr int s5 = 24;
    constexpr int s6 = 32;
}

// ═══════════════════════════════════════════════════════════════════════
//  DPI / USER-SCALE PLUMBING
// ═══════════════════════════════════════════════════════════════════════
//
// Two scale factors compose into a single multiplier that converts logical
// pixels (what every LO:: constant and Layout coordinate is written in) to
// physical pixels (what Win32 and the framebuffer actually use):
//
//   g_userScale   from launcher_config.json. Default 0.85; "small" = 0.70.
//   g_dpiScale    system DPI / 96.0  (e.g. 4K @ 150% Windows scaling = 1.5).
//   g_scale     = g_userScale * g_dpiScale.
//
//   logical  → physical  via S(int) / SF(REAL)
//   physical → logical   via U(int)
//
// Mouse input arrives in physical pixels and must be unscaled before being
// tested against logical rects. Paint code stays in logical pixels: WM_PAINT
// can apply a single g.ScaleTransform at the start of the paint and every
// DrawString / DrawImage / FillRectangle inherits the scale automatically.
// This stage only adds the plumbing — nothing reads g_scale yet, so the
// visible UI is unchanged until later stages wire CreateWindow, fonts, and
// Layout() through S() / SF().

static double g_userScale = 0.85;   // from launcher_config.json
static double g_dpiScale  = 1.0;    // from the system DPI (96 dpi = 1.0)
static double g_scale     = 0.85;   // = g_userScale * g_dpiScale

// Logical → physical.
static inline int  S(int x)    { return (int)lround(x * g_scale); }
static inline REAL SF(REAL x)  { return (REAL)(x * g_scale); }

// Physical → logical (mouse handlers).
static inline int  U(int x)    { return (int)lround(x / g_scale); }

// SetWindowPos that takes LOGICAL coords/sizes and applies S() at the
// Win32 boundary. Use this everywhere in Layout() so the layout math
// stays readable (one unit system: logical pixels) and the scaling is
// applied uniformly. Mouse handlers and Layout never have to know
// about g_scale individually.
static inline BOOL SPosL(HWND hw, HWND hwAfter, int x, int y, int w, int h, UINT flags) {
    return SetWindowPos(hw, hwAfter, S(x), S(y), S(w), S(h), flags);
}

// InvalidateRect with a LOGICAL rectangle. Win32's InvalidateRect takes
// physical pixels (it doesn't know about g_scale), so we scale up the
// rectangle bounds before the call. nullptr passes through to mean
// "invalidate the entire client area".
static inline BOOL InvalidateRectL(HWND hw, const RECT* logical, BOOL erase) {
    if (!logical) return InvalidateRect(hw, nullptr, erase);
    RECT phys = {
        S((int)logical->left),  S((int)logical->top),
        S((int)logical->right), S((int)logical->bottom)
    };
    return InvalidateRect(hw, &phys, erase);
}

// GetClientRect that returns LOGICAL dimensions. Win32's version returns
// physical pixels; this one runs U() on right/bottom so callers can
// operate the rest of their logic in the same logical space as Layout
// and the LO::* constants. left/top are always 0 either way.
static inline void GetClientRectL(HWND hw, RECT* out) {
    GetClientRect(hw, out);
    out->right  = U((int)out->right);
    out->bottom = U((int)out->bottom);
}

// Per-monitor V2 DPI awareness if available, falling back to per-monitor V1,
// then system-aware. Must be called before the first window is created.
static void InitDpiAwareness() {
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        // Win10 v1703+: per-monitor V2 (best non-client scaling)
        typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(HANDLE);
        auto pSetCtx = (PFN_SetProcessDpiAwarenessContext)GetProcAddress(
            hUser32, "SetProcessDpiAwarenessContext");
        if (pSetCtx) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = (HANDLE)-4
            if (pSetCtx((HANDLE)-4)) return;
        }
    }
    // Win8.1+ fallback: per-monitor V1
    HMODULE hShcore = LoadLibraryW(L"Shcore.dll");
    if (hShcore) {
        typedef HRESULT (WINAPI *PFN_SetProcessDpiAwareness)(int);
        auto pSetAware = (PFN_SetProcessDpiAwareness)GetProcAddress(
            hShcore, "SetProcessDpiAwareness");
        if (pSetAware) {
            // PROCESS_PER_MONITOR_DPI_AWARE = 2
            HRESULT hr = pSetAware(2);
            FreeLibrary(hShcore);
            if (SUCCEEDED(hr)) return;
        } else {
            FreeLibrary(hShcore);
        }
    }
    // Vista+ fallback: system-DPI-aware
    SetProcessDPIAware();
}

// Returns DPI / 96.0 for the primary monitor (e.g. 1.0 at 100%, 1.5 at 150%).
// Uses GetDpiForSystem on Win10+ and falls back to GetDeviceCaps.
static double QuerySystemDpiScale() {
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        typedef UINT (WINAPI *PFN_GetDpiForSystem)();
        auto pGetDpi = (PFN_GetDpiForSystem)GetProcAddress(
            hUser32, "GetDpiForSystem");
        if (pGetDpi) {
            UINT dpi = pGetDpi();
            if (dpi > 0) return (double)dpi / 96.0;
        }
    }
    HDC hdc = GetDC(nullptr);
    if (hdc) {
        int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(nullptr, hdc);
        if (dpi > 0) return (double)dpi / 96.0;
    }
    return 1.0;
}

// Layout dimensions
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

    // Mod list row sizing
    constexpr int ROW_H         =  96;

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

// Center-column toolbar (Scale / Font / Colour) widths. Per-control rather
// than uniform — 3×240 = 720 + gaps overflowed the center column at high
// DPI. Each control: label area + 1 px gap + value box. Label and value
// gaps inside each control are tight (1 logical) so the row fits on
// narrow center columns; the inter-control gap (TB_GAP in Layout) sits
// at 9 logical for the same reason.
//
// Label widths sized for the all-caps Exocet font (g_fBtn at SF(13)),
// where each cap measures ~19 logical px in practice. "Colour" needs
// the most room (6 chars × 19 = ~114); earlier values that worked on
// paper clipped one or two chars at the right edge ("FON", "COLOU").
// Font value box is sized for the abbreviated face label ("Cinz-Bol",
// up to 7 chars including dash + chevron + padding).
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

// ═══════════════════════════════════════════════════════════════════════
//  CONTROL IDs
// ═══════════════════════════════════════════════════════════════════════

enum {
    IDC_MOD_LIST       = 100,
    IDC_LAUNCH_BTN     = 101,

    // Mod Description panel link buttons (per-selected-mod)
    IDC_MOD_DISCORD    = 110,
    IDC_MOD_DOCS       = 111,
    IDC_MOD_WEBSITE    = 112,

    // Left rail navigation buttons (open external paths / files)
    IDC_NAV_MODS       = 120,
    IDC_NAV_OPTIONS    = 121,
    IDC_NAV_LOGS       = 122,
    IDC_NAV_HELP       = 123,
    IDC_NAV_ABOUT      = 124,
    IDC_NAV_EXIT       = 125,

    // Loader Directory: read-only path text + Browse (...) button
    IDC_LOADER_DIR_BTN = 130,

    // Mod list refresh button (top-right of list column)
    IDC_REFRESH_BTN    = 140,

    // Browse / Update Selected buttons below the mod list
    IDC_BROWSE_MODS    = 141,
    IDC_UPDATE_MOD     = 142,

    // Bottom expansion panel toggle (arrow button)
    IDC_EXPAND_TOGGLE  = 150,

    // Bottom panel: 6 tool launchers, 3 references, 3 download URLs.
    // Wired into the existing LaunchTool flow (see commits 6+).
    IDC_TOOL_FIRST     = 200,    // 200..205  (6 tools)
    IDC_REF_FIRST      = 210,    // 210..212  (3 references)
    IDC_DL_FIRST       = 220,    // 220..222  (3 download links)

    // Mod list right-click context menu commands. These aren't real
    // child-control IDs — they're TrackPopupMenu command IDs that come
    // back through WM_COMMAND when the user picks a menu item.
    IDM_MOD_OPEN_FOLDER  = 500,
    IDM_MOD_BACKUP_SAVES = 501,
    IDM_MOD_REZIP        = 502,
    IDM_MOD_UNINSTALL    = 503,
    // Restart button shown on the progress dialog when a launcher
    // self-update install reaches stage 4. Clicking it spawns the
    // newly-installed exe and closes the running one.
    IDC_LAUNCHER_RESTART_BTN = 600,
};

// Notification sent up from child controls when any flag changes
constexpr UINT OPT_NOTIFY_CHANGED = WM_USER + 10;

// ═══════════════════════════════════════════════════════════════════════
//  DATA TYPES
// ═══════════════════════════════════════════════════════════════════════

struct ModInfo {
    wstring name;          // display name (from modinfo "name" / "savepath" / folder)
    wstring folder;        // folder name under <D2R>\mods\, used as -mod arg
    wstring dir;           // full path to the mod's root directory
    wstring launcherDir;   // <mod>\Launcher Files
    wstring savePath;      // modinfo "savepath" — folder name under
                           //   %USERPROFILE%\Saved Games\Diablo II
                           //   Resurrected\Mods\ where D2R puts this
                           //   mod's character + stash data. Required
                           //   by Blizzard's modinfo.json schema, so
                           //   always populated. Used by the Backup
                           //   Saves feature.

    // Optional modinfo.json fields
    wstring title;         // overrides "name" as the display label
    wstring description;   // short tagline shown on the mod row
    wstring overview;      // longer description (unused for now)
    wstring version;       // shown in hero meta line
    wstring author;        // shown in hero meta line
    wstring bannerPath;    // resolved banner file path; empty if absent
    wstring docsUrl;       // shows the Documents button when non-empty
    wstring websiteUrl;    // shows the Website button when non-empty
    wstring discordUrl;    // shows the Discord button when non-empty

    // Update-check opt-in. Either field enables checks for this mod.
    // update_github wins if both are set.
    wstring updateGithub;     // "owner/repo" — GitHub releases shortcut
    wstring updateManifest;   // explicit URL to a JSON manifest
};

// Per-mod cached result of the latest update check. Persisted to
// <appdir>\assets\update_cache.json keyed by mod folder name.
struct UpdateInfo {
    bool    available    = false;   // remote version > local version
    bool    fetched      = false;   // true if we have a successful fetch
    bool    timedOut     = false;   // set on a timeout failure
    wstring localVersion;
    wstring remoteVersion;
    wstring changelog;
    wstring downloadUrl;
    wstring sourceUrl;
    wstring releaseDate;
    wstring sha256;                 // optional integrity check
    wstring skippedVersion;         // user clicked "Skip this version"
    time_t  fetchedAt    = 0;
    int     httpStatus   = 0;
};

// What the launcher does after D2R has been confirmed running.
enum LaunchBehavior {
    LB_STAY     = 0,   // stay open, no monitoring
    LB_MINIMIZE = 1,   // minimize on launch, restore + focus on D2R exit
    LB_CLOSE    = 2,   // close launcher after D2R confirmed running
};

struct LauncherCfg {
    wstring d2rPath;
    wstring lastMod;
    wstring toolsDir;       // user's modding tools folder
    wstring toolExcel;      // resolved AFJ Sheet Editor Pro.exe
    wstring toolStrings;    // resolved Code.exe
    wstring toolSprite;     // resolved D2RModding-SpriteEdit-2.0.exe
    wstring toolModels;     // resolved models editor (.exe or shortcut)
    wstring toolTextures;   // resolved textures editor (.exe or shortcut)
    wstring toolParticles;  // resolved particles editor (.exe or shortcut)
    int launchBehavior = LB_MINIMIZE;

    // Mod-update behavior. backupCount = how many old mod folders to keep
    // when updating (0 disables backups). backupSaves = whether to back
    // up the mod's save folder before applying an update; user is asked
    // once, the answer becomes the default. backupSavesPrompted tracks
    // whether we've already asked.
    int  backupCount         = 1;
    bool backupSaves         = true;
    bool backupSavesPrompted = false;

    // UI scale multiplier (independent of system DPI). Persisted as
    // "ui_scale" in launcher_config.json. The final scale factor used by
    // S()/SF()/U() is this value times the system DPI scale; see g_scale
    // at the top of the file. Valid presets are exposed via the toolbar
    // scale dropdown (70/85/100/115/127%).
    double uiScale = 0.85;

    // Preferred display font face name. Persisted as "font_name". Empty
    // string = use the default (Exocet Blizzard Medium for headings,
    // Cinzel for the logo, etc.). The toolbar font dropdown lists every
    // .ttf in assets/fonts/ and stores the user's choice here.
    //
    // NOTE: As of the toolbar's first pass this value is persisted but
    // the actual font swap is deferred — CreateGdipFonts still uses the
    // hard-coded family names. A follow-up will make this runtime-active.
    wstring fontName;

    // Preferred text-color preset index (0..7 — see g_colorPresets[]).
    // Persisted as "font_color". -1 = default (the existing Tok::Gold).
    // Like fontName above, this is captured but not yet applied UI-wide.
    int fontColorIdx = -1;

    // System DPI scale at the last time the config was written. If the
    // current DPI differs from this on load (user changed Windows scaling
    // between sessions), uiScale is reset to 1.0 — otherwise a setting
    // tuned for one DPI environment could leave the launcher unusable on
    // a different one.
    double lastDpiScale = 1.0;

    // Launcher self-update — "skipped" version. When the startup
    // update check finds a latest GitHub release whose tag matches
    // this string, the user-facing dialog is suppressed (the user
    // chose Skip Version on a previous prompt for this same release).
    // Cleared automatically when a newer version than the skipped
    // one becomes available, so a user who skipped v1.2 still gets
    // prompted for v1.3.
    wstring skippedLauncherVersion;
};

// ── Launcher self-update wiring ────────────────────────────────────────
// The launcher checks the GitHub repository for a newer tagged release
// at startup. When a newer release is found and the user hasn't
// "skipped" that exact tag, a themed dialog offers Update / Skip /
// Ignore. The Update path downloads the release zip, renames the
// running .exe to .old, extracts the new files in place, and spawns
// the new process — see the long comment near LauncherUpdateInstallWorker.
constexpr const wchar_t* LAUNCHER_VERSION = L"1.2";
constexpr const wchar_t* LAUNCHER_GITHUB_OWNER = L"kainabel666";
constexpr const wchar_t* LAUNCHER_GITHUB_REPO  = L"Angiris-D2RLoader-GUI";

// ── Launcher self-update state globals ─────────────────────────────────
// Declared up here (rather than next to the rest of the self-update
// implementation further down) because PaintBody references the
// glow flag and the click rect, and PaintBody lives well before the
// self-update block in the file. The function-side code stays where
// it is; only the data lives early.
static wstring      g_launcherUpdateLatestTag;
static wstring      g_launcherUpdateDownloadUrl;
static atomic<bool> g_launcherUpdateCheckRunning{false};

// Set true once the startup worker confirms a newer release exists
// (the "is newer than current" comparison; skipped-version state
// doesn't gate this flag — we want the glowing version label to
// appear even when the user has previously asked to Skip, so the
// label itself can act as an escape hatch to re-check).
static bool         g_launcherUpdateAvailable = false;

// When the user clicks the version label we re-run the check and
// want the dialog to appear even if the user previously chose Skip
// Version on this exact tag. This flag bypasses the skipped-version
// gate for one MSG_LAUNCHER_UPDATE_AVAILABLE delivery.
static bool         g_forceUpdatePrompt = false;

// Logical-coordinate hit rect for the painted version label. Set in
// PaintBody, read by WM_LBUTTONDOWN / WM_SETCURSOR. Zero when no
// version label has been painted yet (very early in startup, before
// the first WM_PAINT).
static RECT         g_versionLabelRect = {0, 0, 0, 0};

// Per-mod launch settings. Persisted to <mod>\Launcher Files\launcher_mod_cfg.json
// so each mod remembers its own flags.
struct ModSettings {
    bool noSound       = false;   // -ns
    bool windowed      = false;   // -w
    bool useTxt        = true;    // -txt    (default on; standard for modded play)
    bool skipIntro     = false;   // -skiplogovideo
    bool respec        = false;   // -enablerespec
    bool resetMaps     = false;   // -resetofflinemaps
    bool useSeed       = false;   // checkbox state — whether -seed gets appended
    wstring seedArg;              // -seed VALUE — the value persists across checkbox toggles so unchecking doesn't lose what was typed
};
static ModSettings g_modSettings;

// Update-check cache, keyed by mod folder. Persisted to
// <appdir>\assets\update_cache.json. Refetches happen at startup and
// on Refresh Mod List click; within the TTL we serve cached results.
static map<wstring, UpdateInfo> g_updateInfo;

// Per-mod playtime tracking. Persisted to <appdir>\assets\playtime.json
// (centralized, not per-mod) so that re-installing a mod doesn't wipe
// its history — the cache survives any change to the mod's own files.
// `seconds` is the running total of D2R-up-and-running time across all
// launches; `lastPlayed` is the unix epoch of the most recent exit.
// Display surface: the hover tooltip on each mod row.
struct PlaytimeRec {
    uint64_t seconds   = 0;
    time_t   lastPlayed = 0;     // 0 == never
};
static map<wstring, PlaytimeRec> g_playtimes;   // key = mod folder

// Tracking state for the current launch. Snapshot at click time so the
// poll handler can attribute play time correctly even if the user
// switches the selected mod mid-game. g_d2rGameStartTick is the
// GetTickCount() value captured the first poll where D2R was seen
// running — that's the proper "game actually playing" start (vs. the
// click tick, which includes loader/spawn overhead).
static DWORD   g_d2rGameStartTick = 0;
static wstring g_d2rGameModFolder;

constexpr int  UPDATE_HTTP_TIMEOUT_FAST  = 5000;
constexpr int  UPDATE_HTTP_TIMEOUT_SLOW  = 10000;     // retry after user prompt
constexpr int  UPDATE_CACHE_TTL_SECONDS  = 2 * 3600;
constexpr UINT MSG_UPDATE_CHECK_DONE     = WM_USER + 20;

// ── Drag-and-drop zip install (V1.1) ─────────────────────────────────────
// User drops one or more .zip files onto the launcher window. A worker
// thread extracts each (via Windows-bundled tar.exe), locates modinfo.json
// inside, derives the mod folder name, and installs into <D2R>\mods\.
// On collision a themed modal dialog asks the user how to resolve.
//
// MSG_ZIP_CONFLICT_DIALOG: worker→main, SendMessage (blocks worker). LPARAM
//   points to a ConflictDialogParam; main thread shows the modal dialog and
//   writes the user's choice back to param->choice before returning. The
//   modal MUST run on the main UI thread (Win32 modals + window-class
//   restrictions), hence the SendMessage round-trip.
// MSG_ZIP_QUEUE_DONE: worker→main, PostMessage. Triggers a final
//   RefreshMods + repaint after all queued zips have been processed.
constexpr UINT MSG_ZIP_CONFLICT_DIALOG   = WM_USER + 30;
constexpr UINT MSG_ZIP_QUEUE_DONE        = WM_USER + 31;
constexpr UINT MSG_ZIP_NEED_PATH         = WM_USER + 32;   // worker→main, blocks; returns 0=cancel, 1=path set
constexpr UINT MSG_ZIP_NO_MODINFO        = WM_USER + 33;   // worker→main, blocks; returns 0
constexpr UINT MSG_ZIP_PROGRESS_SHOW     = WM_USER + 34;   // worker→main (blocking via SendMessage so dialog is up before next stage)
constexpr UINT MSG_ZIP_PROGRESS_UPDATE   = WM_USER + 35;   // worker→main, blocking; LPARAM = ProgressUpdate*
constexpr UINT MSG_ZIP_PROGRESS_HIDE     = WM_USER + 36;   // worker→main, posted

// Launcher self-update — worker thread polls GitHub Releases once at
// startup and posts this message when a newer tagged release exists
// (and the user hasn't asked to skip that tag). MainProc shows the
// themed dialog; payload travels through file-scope globals filled
// by the worker before the post.
constexpr UINT MSG_LAUNCHER_UPDATE_AVAILABLE = WM_USER + 50;

// Sent by the launcher-update install worker to drive the simple
// text-only popup forward through its three statuses. wp = new status:
//   1 = Downloading
//   2 = Updating
//   3 = Complete
// UI thread updates g_lupopupStatus and repaints g_lupopupWnd.
constexpr UINT MSG_LUPOPUP_STATUS            = WM_USER + 51;

struct ConflictDialogParam {
    wstring modName;   // shown in the dialog title/body
    int     choice;    // out: 0=cancel, 1=update, 2=overwrite
};

struct ProgressUpdate {
    int     stage;        // 0..4 — selects progress_bar_<stage>.png
    int     zipIdx;       // 1-based position in queue
    int     zipTotal;     // total queued zips at start of run
    wstring zipName;      // current archive filename (no directory)
    wstring stageLabel;   // "Extracting archive...", etc.
};

// D2R process tracking for the post-launch Minimize/Close behaviors.
// The process handle is kept alive for polling when minimize is chosen.
// ── D2R process tracking ─────────────────────────────────────────────────
// We don't track D2RLoader's handle — it's a shim that injects mod hooks
// into D2R.exe and then exits, often before D2R has finished loading.
// Polling the loader's handle would make us think D2R exited when only
// the loader did, and the launcher would un-minimize back on top of a
// still-loading game. Instead we poll the process table by name; the
// launcher restores only when D2R.exe itself disappears (or never shows
// up at all, after a fail-safe timeout).
//
// State machine:
//   g_d2rTracking == false  → not tracking, no timer running
//   true + g_d2rEverSeen == false  → launch sent, waiting for D2R.exe
//                                    to appear in the process table
//   true + g_d2rEverSeen == true   → D2R.exe is running; waiting for it
//                                    to exit
//
// g_d2rLaunchTick anchors a 60 s fail-safe — if D2R.exe never shows up
// (loader crashed, wrong path, etc.) we give up and restore the launcher
// rather than polling forever.
static bool   g_d2rTracking   = false;
static bool   g_d2rEverSeen   = false;
static DWORD  g_d2rLaunchTick = 0;
// True between the post-launch SetTimer and the FIRST IDT_D2R_POLL fire.
// While set, the timer is on its "wait 10 s before first poll" interval;
// the first fire resets the timer to the normal 1 s cadence.
static bool   g_pollFirstShot = false;

// Returns true if any process whose image name matches one of `names`
// (case-insensitive) is present in the current process snapshot. The
// snapshot is built once and walked once regardless of how many names
// we're checking, which keeps the polling cost flat.
static bool AnyProcessExistsByName(const wchar_t* const* names, size_t count) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe = { sizeof(pe) };
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            for (size_t i = 0; i < count; ++i) {
                if (_wcsicmp(pe.szExeFile, names[i]) == 0) {
                    found = true;
                    break;
                }
            }
            if (found) break;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

// The two process names we treat as "the game is alive". D2RLoader is
// the bootstrap shim that may exit shortly after spawning D2R.exe — but
// some setups keep it alive throughout play, and some leave it active
// briefly between the loader doing its work and D2R.exe being visible.
// Counting either name as "running" handles all of those.
static const wchar_t* const k_d2rProcessNames[] = {
    L"D2R.exe",
    L"D2RLoader.exe",
};
static const size_t k_d2rProcessNameCount =
    sizeof(k_d2rProcessNames) / sizeof(k_d2rProcessNames[0]);
constexpr UINT IDT_D2R_POLL = 9101;         // 1s timer fired on the main HWND
constexpr UINT IDT_HOVER_TIP = 9102;        // 2s timer on the mod list HWND;
                                            //   fires the playtime tooltip
                                            //   if the cursor's been still
                                            //   over a row long enough.
constexpr UINT IDT_CLEANUP_OLD_EXE = 9103;  // 2s timer on the main HWND;
                                            //   deferred retry for deleting
                                            //   Angiris.exe.old when the
                                            //   prior launcher process is
                                            //   still releasing its image
                                            //   file. See CleanupLauncher
                                            //   OldExe + the WM_TIMER
                                            //   handler in MainProc.

// State for the deferred .old cleanup. Populated by CleanupLauncherOldExe
// when the up-front fast-path attempt fails (because the prior launcher
// is still shutting down). The MainProc timer reads g_pendingOldExeDelete
// and retries DeleteFileW every 2s until success, the file disappears,
// or g_cleanupOldExeAttempts hits the cap (~30s total).
static wstring g_pendingOldExeDelete;
static int     g_cleanupOldExeAttempts = 0;

// Discord Rich Presence integration is held back pending the Discord
// application review process — the IPC code lives in a separate module
// (discord_rpc.cpp / discord_rpc.h) which is currently not included or
// built. See discord_rpc.h for re-enable instructions when the time
// comes.

// Decoded banner image cache, keyed by file path. Reloaded only when
// the path changes — repeated paints of the same banner are free.
// (Now used by the mod list rows, which render banners as backgrounds.)
namespace Gdiplus { class Bitmap; }
static Gdiplus::Bitmap* g_bannerCache    = nullptr;
static wstring          g_bannerCacheKey;

// ═══════════════════════════════════════════════════════════════════════
//  GLOBALS
// ═══════════════════════════════════════════════════════════════════════

static HINSTANCE   g_hInst        = nullptr;
static HWND        g_hwMain       = nullptr;
static HWND        g_hwList       = nullptr;     // mod list (custom-painted)
static HWND        g_hwLaunch     = nullptr;     // PLAY button (in right column)

// Mod Description link buttons (per-mod, shown/hidden based on modinfo.json)
static HWND        g_hwModDiscord = nullptr;
static HWND        g_hwModDocs    = nullptr;
static HWND        g_hwModWebsite = nullptr;

// Left rail navigation buttons (open external paths/files)
static HWND        g_hwNavMods    = nullptr;
static HWND        g_hwNavOptions = nullptr;
static HWND        g_hwNavLogs    = nullptr;
static HWND        g_hwNavHelp    = nullptr;
static HWND        g_hwNavAbout   = nullptr;
static HWND        g_hwNavExit    = nullptr;

// Loader Directory row (read-only path + ... browse button)
static RECT        g_loaderDirRect = {};            // paint+hit-test rect for the path bar
static RECT        g_stashDropdownRect = {};         // Stash Tabs row rect (Layout populates)
static RECT        g_dmgDropdownRect   = {};         // DMG Display row rect (Layout populates)

// Center-column toolbar dropdowns (sit between the Nexus/Update button
// row and the expand arrow). All three are painted programmatically and
// hit-tested in WM_LBUTTONDOWN. Layout() populates their rects.
static RECT        g_scaleDropdownRect = {};   // top-row Scale label+value (display only, not clickable)
static RECT        g_scaleSliderRect   = {};   // 3-state toggle slider below Scale — the click target
static RECT        g_onLaunchHeaderRect= {};   // top tier — "ON LAUNCH" header label
static RECT        g_onLaunchRect      = {};   // middle tier — value-only textbox (Min/Close/Stay)
static RECT        g_onLaunchSliderRect= {};   // bottom tier — 3-state toggle slider

// Measured rendered width of the "Seed" label (in LOGICAL units, not
// scaled). Recomputed every time CreateGdipFonts runs so a font swap
// via the toolbar Font dropdown immediately reflows the combo. Default
// covers the case where the global is read before the first measure.
static int         g_seedLabelLogicalW = 44;
static RECT        g_fontDropdownRect  = {};
static RECT        g_colorDropdownRect = {};
static HWND        g_hwLoaderDirBtn = nullptr;     // "..." button

// Mod list adjacent buttons
static HWND        g_hwRefresh    = nullptr;       // top-right "Refresh"
static HWND        g_hwBrowseMods = nullptr;       // bottom-left
static HWND        g_hwUpdateMod  = nullptr;       // bottom-right

// Bottom expansion panel
static HWND        g_hwExpandToggle = nullptr;     // arrow button
static bool        g_bottomExpanded = false;

// Custom title-bar button state (rendered as image assets in PaintBody;
// hit-tested in WM_NCHITTEST and WM_LBUTTONDOWN). -1 = no hover, 0 = min,
// 1 = close. Pressed is true while the mouse is held over a button.
static int         g_tbHover    = -1;
static int         g_tbPressed  = -1;

// Owner-drawn buttons. Every push-button created via MkStdBtn is registered
// here with a "kind" that tells WM_DRAWITEM which asset and state transform
// to use. Until the styled button assets land, every kind falls back to
// the programmatic OPDrawBtnFrame so the launcher looks identical to today.
//
// One asset per kind (no _idle/_hover/_click variants). The grow-on-hover,
// shrink-and-offset-on-click effect is produced by applying a GDI+ transform
// at paint time. Both the asset and its label text scale together so they
// read as a single unified element.
//
// Asset filenames:
//   Nav         → btn_nav.png          (left rail + link buttons)
//   NavSm       → btn_nav.png          (unused — kept for future use; bottom
//                                       expansion panel now uses NexusUpdate)
//   Refresh     → btn_refresh.png      (text baked into art)
//   NexusUpdate → btn_nexus_update.png (Nexus + Update Selected; shared)
//   Play        → btn_play.png         (large)
//   Ellipse     → btn_ellipse.png      ("..." button; glyph baked in)
//   Arrow       → btn_expand_arrow.png (chevron; rotated 180° when expanded)
enum class ButtonKind {
    Nav,
    NavSm,
    Refresh,
    NexusUpdate,
    ModLink,           // legacy — kept so existing call sites compile; not used after the link buttons split
    ModLinkDocs,       // square 85×85 — uses btn_docs.png; falls back to 9-sliced btn_nexus_update + "D"
    ModLinkDiscord,    // square 85×85 — uses btn_discord.png; falls back to 9-sliced btn_nexus_update + "X"
    ModLinkWebsite,    // square 85×85 — uses btn_website.png; falls back to 9-sliced btn_nexus_update + "W"
    Play,
    Ellipse,
    Arrow,
};

// Per-kind visual response to hover / click. Applied as a GDI+ transform
// around the button's center so the asset AND label move/scale together.
struct ButtonStateTransform {
    float scaleHover;     // multiplier when mouse is over (and not pressed)
    float scaleClick;     // multiplier when pressed
    float offsetXClick;   // pixels to shift right when pressed (down-right tactile)
    float offsetYClick;
};

// Per-kind transform table. Tuned so each kind's animation feels right at
// its native size — bigger buttons grow/shrink less to keep movement subtle,
// smaller buttons can use bolder scale factors.
static ButtonStateTransform StateTransformFor(ButtonKind k) {
    switch (k) {
    case ButtonKind::Nav:         return { 1.06f, 0.92f, 2.0f, 2.0f };
    case ButtonKind::NavSm:       return { 1.06f, 0.92f, 2.0f, 2.0f };
    case ButtonKind::Refresh:     return { 1.00f, 0.94f, 1.0f, 1.0f };   // no hover-grow; click-shrink only
    case ButtonKind::NexusUpdate:    return { 1.05f, 0.93f, 1.0f, 1.0f };
    case ButtonKind::ModLink:        return { 1.00f, 0.93f, 1.0f, 1.0f };   // legacy
    case ButtonKind::ModLinkDocs:    return { 1.00f, 0.93f, 1.0f, 1.0f };   // no hover-grow; click-shrink
    case ButtonKind::ModLinkDiscord: return { 1.00f, 0.93f, 1.0f, 1.0f };
    case ButtonKind::ModLinkWebsite: return { 1.00f, 0.93f, 1.0f, 1.0f };
    case ButtonKind::Play:        return { 1.03f, 0.95f, 2.0f, 2.0f };
    case ButtonKind::Ellipse:     return { 1.08f, 0.90f, 1.0f, 1.0f };
    case ButtonKind::Arrow:       return { 1.06f, 0.92f, 0.0f, 2.0f };
    }
    return { 1.0f, 1.0f, 0.0f, 0.0f };
}

// Asset filename for a given kind (single asset per kind — no state suffix).
static const wchar_t* AssetNameFor(ButtonKind k) {
    switch (k) {
    case ButtonKind::Nav:         return L"btn_nav.png";
    case ButtonKind::NavSm:       return L"btn_nav.png";
    case ButtonKind::Refresh:     return L"btn_refresh.png";
    case ButtonKind::NexusUpdate:    return L"btn_nexus_update.png";
    case ButtonKind::ModLink:        return L"btn_nexus_update.png";   // legacy
    case ButtonKind::ModLinkDocs:    return L"btn_docs.png";
    case ButtonKind::ModLinkDiscord: return L"btn_discord.png";
    case ButtonKind::ModLinkWebsite: return L"btn_website.png";
    case ButtonKind::Play:        return L"btn_play.png";
    case ButtonKind::Ellipse:     return L"btn_ellipse.png";
    case ButtonKind::Arrow:       return L"btn_expand_arrow.png";
    }
    return nullptr;
}
struct ButtonState {
    ButtonKind kind     = ButtonKind::Nav;
    bool       hover    = false;     // mouse currently over this button
    bool       tracking = false;     // we've called TrackMouseEvent already
};
static map<HWND, ButtonState> g_btnStates;

// Subclass proc for owner-drawn buttons. Tracks mouse hover state so the
// shared WM_DRAWITEM handler can swap idle↔hover assets cleanly. Without
// this, Win32 button controls swallow mouse messages internally and we'd
// have to poll cursor position on every paint (which doesn't trigger
// repaints on hover-in/hover-out).
static LRESULT CALLBACK BtnHoverSubclass(HWND hw, UINT msg,
                                        WPARAM wp, LPARAM lp,
                                        UINT_PTR /*id*/, DWORD_PTR /*ref*/) {
    // Suppress the system's default WM_ERASEBKGND for owner-draw buttons.
    // Without this, the BUTTON class fills the client area with
    // COLOR_BTNFACE (a light gray, looks near-white over our dark stone)
    // BEFORE WM_DRAWITEM fires, which paints a brief flash whenever a
    // hidden button becomes visible (expansion panel opens, mod-link
    // buttons appear for a freshly selected mod, etc.). WM_DRAWITEM
    // repaints the entire rect anyway, so the erase is redundant.
    if (msg == WM_ERASEBKGND) return 1;

    auto it = g_btnStates.find(hw);
    if (it != g_btnStates.end()) {
        ButtonState& st = it->second;
        // Skip the hover-driven invalidate when the kind has no hover
        // visual change (scaleHover == 1.0). Otherwise WM_MOUSEMOVE /
        // WM_MOUSELEAVE schedule a full WM_PAINT just to redraw an
        // identical frame, and on heavy-asset buttons that paint shows
        // up as visible jitter. Click-driven repaints still happen
        // through the normal BS_OWNERDRAW pathway (Windows sends
        // WM_DRAWITEM with ODS_SELECTED), so the shrink-on-click
        // behavior is unaffected.
        bool hoverHasVisual = (StateTransformFor(st.kind).scaleHover != 1.0f);
        if (msg == WM_MOUSEMOVE) {
            if (!st.hover) {
                st.hover = true;
                if (hoverHasVisual) InvalidateRect(hw, nullptr, FALSE);
            }
            if (!st.tracking) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hw, 0 };
                TrackMouseEvent(&tme);
                st.tracking = true;
            }
        }
        else if (msg == WM_MOUSELEAVE) {
            st.hover = false;
            st.tracking = false;
            if (hoverHasVisual) InvalidateRect(hw, nullptr, FALSE);
        }
        else if (msg == WM_NCDESTROY) {
            g_btnStates.erase(it);
            RemoveWindowSubclass(hw, BtnHoverSubclass, 1);
        }
    }
    return DefSubclassProc(hw, msg, wp, lp);
}

// Register an HWND as an owner-drawn button of the given kind. Called once
// per button at creation time. WM_DRAWITEM dispatches via this map; the
// subclass added here keeps the hover state in sync.
static void RegisterButton(HWND hw, ButtonKind kind) {
    if (!hw) return;
    g_btnStates[hw] = ButtonState{ kind, false, false };
    SetWindowSubclass(hw, BtnHoverSubclass, 1, 0);
}

static HWND        g_hwBottomTools[6]   = {};      // 6 tool launchers
static HWND        g_hwBottomRefs[3]    = {};      // 3 references
static HWND        g_hwBottomDls[3]     = {};      // 3 download links

static bool        g_modsDirty    = false;       // watcher saw changes; manual refresh pending

static ULONG_PTR   g_gdipToken    = 0;
static LauncherCfg g_cfg;
static vector<ModInfo> g_mods;
static int         g_selMod       = -1;

// Bundled fonts — loaded from assets/fonts/ at startup with FR_PRIVATE
// so they're visible to GDI+ but not added to the system font list.
static vector<wstring> g_loadedFonts;

// Persistent PrivateFontCollection holding every bundled .ttf for the
// app's lifetime. Originally LoadFonts used a throwaway local PFC per
// file just to extract family names, relying on AddFontResourceEx
// (FR_PRIVATE) to make later FontFamily(name) lookups resolve. That
// silently failed for the user-font override path on at least some
// systems — FontFamily(name)->GetLastStatus came back non-Ok, the
// override stayed null, and CreateGdipFonts fell back to Exocet for
// every user pick. Keeping the PFC alive lets us pass &g_pfc as the
// FontCollection arg to FontFamily, which guarantees the lookup
// resolves the font we just added.
static Gdiplus::PrivateFontCollection* g_pfc = nullptr;
static FontFamily*  g_ffCinzel     = nullptr;
static FontFamily*  g_ffCinzelBold = nullptr;
static FontFamily*  g_ffFell       = nullptr;
static FontFamily*  g_ffExocet     = nullptr;     // D2 menu font (Exocet Blizzard)
static FontFamily*  g_ffGeorgia    = nullptr;     // fallback when bundled fonts missing

// User font override. Created lazily from g_cfg.fontName when the user
// picks a font in the toolbar dropdown. When set, CreateGdipFonts uses
// this family (with g_userFontStyleOverride style bits) in place of
// every Exocet-family font in the UI. The Georgia-family fonts (italic
// body text — HeroMeta, Status, ModSub, ModPath) stay default, because
// most custom fonts don't ship an italic face and forcing the user's
// pick there would render those texts as upright Roman.
static FontFamily*  g_userFontFamilyOverride = nullptr;
static INT          g_userFontStyleOverride  = FontStyleRegular;

// Cached GDI+ Font instances at the design sizes. Exocet (D2 menu font)
// carries the launcher's identity; Georgia is used where dense legibility
// matters (cmd preview, mod description body, hero meta italics).
static Font* g_fHeroName   = nullptr;   // Exocet 38px  (mod row title fallback)
static Font* g_fHeroMeta   = nullptr;   // Georgia italic, 14px
static Font* g_fTitle      = nullptr;   // Exocet 26px  (D2RLOADER wordmark)
static Font* g_fSubtitle   = nullptr;   // Exocet 11px  (subtitle lines)
static Font* g_fColHdr     = nullptr;   // Exocet 32px  (MODS — largest)
static Font* g_fColHdrMed  = nullptr;   // Exocet 24px  (most section titles at 75%)
static Font* g_fColHdrSm   = nullptr;   // Exocet 16px  (LOADER OPTIONS at 50%)
static Font* g_fExpHdr     = nullptr;   // Exocet 18px  (bottom expand-panel section titles)
static Font* g_fSubLbl     = nullptr;   // Exocet 11px  (sublabels)
static Font* g_fBtn        = nullptr;   // Exocet 13px  (button text)
static Font* g_fCmdArgs    = nullptr;   // Exocet 11px  (launch args preview)
static Font* g_fNav        = nullptr;   // Exocet 26px  (nav button text)
static Font* g_fNavSm      = nullptr;   // Exocet 18.2px  (Nexus/Update + expansion-panel buttons; +40% over the base button size)
static Font* g_fBtnLaunch  = nullptr;   // Exocet 40px  (PLAY button — doubled)
static Font* g_fStatus     = nullptr;   // Georgia italic, 12px
static Font* g_fModName    = nullptr;   // Exocet 18px  (mod row name)
static Font* g_fModSub     = nullptr;   // Georgia italic, 12px
static Font* g_fModPath    = nullptr;   // Georgia, 11px

// ═══════════════════════════════════════════════════════════════════════
//  UTILITIES (carried from D2R_ModLauncher.cpp — proven working)
// ═══════════════════════════════════════════════════════════════════════

static wstring AppDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileName(nullptr, buf, MAX_PATH);
    wstring p(buf);
    size_t s = p.rfind(L'\\');
    return s != wstring::npos ? p.substr(0, s) : p;
}

// ═══════════════════════════════════════════════════════════════════════
//  ASSET CACHE
// ═══════════════════════════════════════════════════════════════════════
//
//  Lazy-loaded GDI+ Bitmap cache for image assets in <exe-dir>\assets\images\.
//  First call to AssetImage(L"name.png") loads + caches; later calls are
//  just a map lookup. Missing or unreadable assets return nullptr; callers
//  are responsible for handling that case (typically by falling back to a
//  programmatic paint). The cache is freed in DestroyAssetCache() at exit.
//
//  Cached images are owned by the cache (do not delete the returned ptr).

static map<wstring, Gdiplus::Bitmap*> g_assetCache;

static wstring AssetDir() {
    return AppDir() + L"\\assets\\images";
}

static Gdiplus::Bitmap* AssetImage(const wchar_t* name) {
    auto it = g_assetCache.find(name);
    if (it != g_assetCache.end()) return it->second;
    wstring full = AssetDir() + L"\\" + name;
    Gdiplus::Bitmap* b = nullptr;
    if (GetFileAttributes(full.c_str()) != INVALID_FILE_ATTRIBUTES) {
        b = new Gdiplus::Bitmap(full.c_str());
        if (!b || b->GetLastStatus() != Ok) {
            delete b;
            b = nullptr;
        }
    }
    g_assetCache[name] = b;       // cache nullptrs too — avoid retrying missing files
    return b;
}

static void DestroyAssetCache() {
    for (auto& kv : g_assetCache) delete kv.second;
    g_assetCache.clear();
}

// Draw an image at a target rect. Stretches to fit; uses
// InterpolationModeHighQualityBicubic for the smoothest result. Caller
// should already have a Graphics set up; this just issues the DrawImage.
static void DrawAssetStretched(Graphics& g, Gdiplus::Bitmap* b,
                               int x, int y, int w, int h) {
    if (!b) return;
    InterpolationMode prev = g.GetInterpolationMode();
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.DrawImage(b, x, y, w, h);
    g.SetInterpolationMode(prev);
}

// Draw an image at its native size, top-left at (x, y).
static void DrawAssetAt(Graphics& g, Gdiplus::Bitmap* b, int x, int y) {
    if (!b) return;
    g.DrawImage(b, x, y, (INT)b->GetWidth(), (INT)b->GetHeight());
}

// Render an asset as a 9-slice into the target rectangle. The four
// corners are blitted pixel-perfect; the top/bottom edges stretch
// horizontally; the left/right edges stretch vertically; the center
// stretches in both directions. Asset bitmap's blue-gem corner art (or
// any other fixed-detail corners) stays sharp at any rendered size.
//
// `corner` is the inset distance from each edge that should remain
// unstretched — the corner art's pixel size. ~12-20px is typical for
// our bronze frames.
//
// No-op when `b` is null so missing assets degrade gracefully.
static void DrawButton9Slice(Graphics& g, Gdiplus::Bitmap* b,
                             int dx, int dy, int dw, int dh, int corner) {
    if (!b || dw <= 0 || dh <= 0) return;
    int sw = (int)b->GetWidth();
    int sh = (int)b->GetHeight();
    // Clamp corner so it never exceeds half the source or destination.
    int c = corner;
    if (c * 2 > sw) c = sw / 2;
    if (c * 2 > sh) c = sh / 2;
    if (c * 2 > dw) c = dw / 2;
    if (c * 2 > dh) c = dh / 2;
    if (c < 1) {
        // Degenerate: just stretch the whole image.
        InterpolationMode prev = g.GetInterpolationMode();
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(b, dx, dy, dw, dh);
        g.SetInterpolationMode(prev);
        return;
    }

    int sCx = sw - c;        // source center x-extent ends at sCx
    int sCy = sh - c;        // source center y-extent ends at sCy
    int sCw = sw - c * 2;    // source center width
    int sCh = sh - c * 2;    // source center height
    int dCx = dx + c;
    int dCy = dy + c;
    int dCw = dw - c * 2;
    int dCh = dh - c * 2;

    InterpolationMode prev = g.GetInterpolationMode();
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);

    // Helper to draw one piece from source rect to dest rect.
    auto piece = [&](int sx, int sy, int spw, int sph,
                     int px, int py, int pw, int ph) {
        if (pw <= 0 || ph <= 0 || spw <= 0 || sph <= 0) return;
        g.DrawImage(b,
            Rect(px, py, pw, ph),
            (REAL)sx, (REAL)sy, (REAL)spw, (REAL)sph,
            UnitPixel);
    };

    // Corners (pixel-perfect, no scaling — source size == dest size == c×c)
    piece(0,       0,       c,   c,   dx,        dy,        c,   c);    // TL
    piece(sCx,     0,       c,   c,   dCx + dCw, dy,        c,   c);    // TR
    piece(0,       sCy,     c,   c,   dx,        dCy + dCh, c,   c);    // BL
    piece(sCx,     sCy,     c,   c,   dCx + dCw, dCy + dCh, c,   c);    // BR
    // Edges (stretched along one axis)
    piece(c,       0,       sCw, c,   dCx,       dy,        dCw, c);    // top
    piece(c,       sCy,     sCw, c,   dCx,       dCy + dCh, dCw, c);    // bottom
    piece(0,       c,       c,   sCh, dx,        dCy,       c,   dCh);  // left
    piece(sCx,     c,       c,   sCh, dCx + dCw, dCy,       c,   dCh);  // right
    // Center (stretched both axes)
    piece(c,       c,       sCw, sCh, dCx,       dCy,       dCw, dCh);

    g.SetInterpolationMode(prev);
}

// Measures the safe-interior inset of a frame asset by scanning each edge
// inward until it finds a row/column whose opacity drops below the "edge
// filigree" density. The result tells layout how far inset from the window
// edges the content area starts.
//
// Cached per-asset by name. Once computed it's reused for the life of the
// process (no need to re-scan a 1536×1024 bitmap on every paint).
struct FrameInset { int top, bottom, left, right; };

static map<wstring, FrameInset> g_frameInsetCache;

static FrameInset MeasureFrameInset(const wchar_t* assetName) {
    auto it = g_frameInsetCache.find(assetName);
    if (it != g_frameInsetCache.end()) return it->second;

    FrameInset r = { 0, 0, 0, 0 };
    Gdiplus::Bitmap* b = AssetImage(assetName);
    if (!b) { g_frameInsetCache[assetName] = r; return r; }

    UINT W = b->GetWidth(), H = b->GetHeight();
    Gdiplus::BitmapData bd = {};
    Gdiplus::Rect lockRect(0, 0, (INT)W, (INT)H);
    if (b->LockBits(&lockRect, Gdiplus::ImageLockModeRead,
                    PixelFormat32bppARGB, &bd) != Ok) {
        g_frameInsetCache[assetName] = r;
        return r;
    }

    // Pixel layout: each row is bd.Stride bytes; ARGB premultiplied is BGRA
    // little-endian (A is byte index 3 within each pixel).
    auto alphaAt = [&](UINT x, UINT y) -> BYTE {
        const BYTE* row = (const BYTE*)bd.Scan0 + (intptr_t)y * bd.Stride;
        return row[x * 4 + 3];
    };

    // Density per row/column: how many pixels along this scan line have
    // alpha > 50. The edge filigree shows as a solid band along the outer
    // edge; the corner ornaments extend further but are sparse.
    auto rowDensity = [&](UINT y) {
        UINT n = 0;
        for (UINT x = 0; x < W; ++x) if (alphaAt(x, y) > 50) ++n;
        return n;
    };
    auto colDensity = [&](UINT x) {
        UINT n = 0;
        for (UINT y = 0; y < H; ++y) if (alphaAt(x, y) > 50) ++n;
        return n;
    };

    // Threshold for "this is the solid edge filigree": at least 30% of the
    // scan line is opaque. Below that we're either in the corner-only
    // region or fully interior.
    const UINT rowThresh = W * 30 / 100;
    const UINT colThresh = H * 30 / 100;

    // Top: scan down from y=0 while row density is above the edge threshold;
    // stop the moment it drops (= we crossed into interior).
    for (UINT y = 0; y < H / 2; ++y) {
        if (rowDensity(y) >= rowThresh) r.top = (int)(y + 1);
        else break;
    }
    // Bottom: scan up
    for (UINT i = 0; i < H / 2; ++i) {
        UINT y = H - 1 - i;
        if (rowDensity(y) >= rowThresh) r.bottom = (int)(i + 1);
        else break;
    }
    // Left: scan right
    for (UINT x = 0; x < W / 2; ++x) {
        if (colDensity(x) >= colThresh) r.left = (int)(x + 1);
        else break;
    }
    // Right: scan left
    for (UINT i = 0; i < W / 2; ++i) {
        UINT x = W - 1 - i;
        if (colDensity(x) >= colThresh) r.right = (int)(i + 1);
        else break;
    }

    b->UnlockBits(&bd);
    g_frameInsetCache[assetName] = r;
    return r;
}

// Measures the three internal region boundaries of frame_panel_right.png
// (the right-column panel asset). The asset has horizontal dividers at
// specific Y coordinates that split it into MOD DESCRIPTION (top),
// LAUNCH OPTIONS (middle), and PLAY (bottom) regions. We detect dividers
// by scanning row density; rows that are mostly-opaque across the full
// width are dividers.
//
// All values are in the asset's native pixel space (not stretched), since
// the asset itself is drawn at 1:1 in the launcher.
struct PanelRegions {
    bool  valid;
    int   topPanelY0,    topPanelY1;     // MOD DESCRIPTION interior
    int   midPanelY0,    midPanelY1;     // LAUNCH OPTIONS interior
    int   botPanelY0,    botPanelY1;     // PLAY interior
    int   sideMargin;                    // x-inset from asset edge to interior
};

static map<wstring, PanelRegions> g_panelRegionCache;

static PanelRegions MeasurePanelRegions(const wchar_t* assetName) {
    auto it = g_panelRegionCache.find(assetName);
    if (it != g_panelRegionCache.end()) return it->second;

    PanelRegions p = {};
    Gdiplus::Bitmap* b = AssetImage(assetName);
    if (!b) { g_panelRegionCache[assetName] = p; return p; }

    UINT W = b->GetWidth(), H = b->GetHeight();
    Gdiplus::BitmapData bd = {};
    Gdiplus::Rect lockRect(0, 0, (INT)W, (INT)H);
    if (b->LockBits(&lockRect, Gdiplus::ImageLockModeRead,
                    PixelFormat32bppARGB, &bd) != Ok) {
        g_panelRegionCache[assetName] = p;
        return p;
    }
    auto alphaAt = [&](UINT x, UINT y) -> BYTE {
        const BYTE* row = (const BYTE*)bd.Scan0 + (intptr_t)y * bd.Stride;
        return row[x * 4 + 3];
    };
    // Walk every row; track contiguous "divider" bands (opaque across >50%
    // of width). Collect up to 4 bands: top edge, middle divider, inner
    // divider (between launch options and play), bottom edge.
    int bandStart[8] = {0}, bandEnd[8] = {0};
    int nBands = 0;
    bool inBand = false;
    int bs = 0;
    for (UINT y = 0; y < H && nBands < 8; ++y) {
        UINT n = 0;
        for (UINT x = 0; x < W; ++x) if (alphaAt(x, y) > 50) ++n;
        bool isBand = (n > W / 2);
        if (isBand && !inBand) { bs = (int)y; inBand = true; }
        else if (!isBand && inBand) {
            bandStart[nBands] = bs;
            bandEnd[nBands]   = (int)y - 1;
            ++nBands;
            inBand = false;
        }
    }
    if (inBand && nBands < 8) {
        bandStart[nBands] = bs;
        bandEnd[nBands]   = (int)H - 1;
        ++nBands;
    }

    // We expect 4 bands (top edge, mid divider, inner divider, bottom edge).
    if (nBands >= 4) {
        p.valid       = true;
        p.topPanelY0  = bandEnd[0] + 1;
        p.topPanelY1  = bandStart[1] - 1;
        p.midPanelY0  = bandEnd[1] + 1;
        p.midPanelY1  = bandStart[2] - 1;
        p.botPanelY0  = bandEnd[2] + 1;
        p.botPanelY1  = bandStart[3] - 1;
    }
    // Measure side margin from a representative row inside the top panel.
    if (p.valid) {
        int sampleY = (p.topPanelY0 + p.topPanelY1) / 2;
        int leftIn = 0, rightIn = 0;
        for (UINT x = 0; x < W; ++x) {
            if (alphaAt(x, (UINT)sampleY) < 50) { leftIn = (int)x; break; }
        }
        for (UINT i = 0; i < W; ++i) {
            UINT x = W - 1 - i;
            if (alphaAt(x, (UINT)sampleY) < 50) { rightIn = (int)i; break; }
        }
        p.sideMargin = max(leftIn, rightIn);
    }

    b->UnlockBits(&bd);
    g_panelRegionCache[assetName] = p;
    return p;
}

// Mod-link resolver. modinfo.json's documents/website/discord fields
// can hold a URL, an absolute path, or a path relative to the mod folder.
// This normalizes whichever was given into something ShellExecute can open.
static wstring ResolveModLink(const wstring& field, const wstring& modDir) {
    if (field.empty()) return field;

    // URL scheme detection: anything with "://" near the start, or a
    // bare "mailto:". Generous — modders may use schemes we don't list.
    size_t colon = field.find(L':');
    if (colon != wstring::npos && colon < 12) {
        // "http:" "https:" "ftp:" "file:" "mailto:" "steam:" etc.
        return field;
    }

    // Absolute Windows path: "C:\..." or "\\server\share"
    if (field.size() >= 2 && field[1] == L':') return field;
    if (field.size() >= 2 && field[0] == L'\\' && field[1] == L'\\') return field;

    // Otherwise: relative to the mod's folder. Strip any leading "./"
    wstring rel = field;
    if (rel.size() >= 2 && rel[0] == L'.' && (rel[1] == L'/' || rel[1] == L'\\')) {
        rel = rel.substr(2);
    }
    // Normalize forward slashes to Windows backslashes
    for (auto& c : rel) if (c == L'/') c = L'\\';

    return modDir + L"\\" + rel;
}

static wstring ReadTextFile(const wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return L"";
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    string raw(sz, '\0');
    fread(&raw[0], 1, sz, f); fclose(f);
    if (sz >= 3 && (unsigned char)raw[0] == 0xEF
                && (unsigned char)raw[1] == 0xBB
                && (unsigned char)raw[2] == 0xBF) raw = raw.substr(3);
    int len = MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), nullptr, 0);
    wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), &out[0], len);
    return out;
}

static void WriteTextFile(const wstring& path, const wstring& content) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    int len = WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, nullptr, 0, nullptr, nullptr);
    string bytes(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, &bytes[0], len, nullptr, nullptr);
    if (len > 0) fwrite(bytes.c_str(), 1, len - 1, f);
    fclose(f);
}

static wstring EscapeJson(const wstring& s) {
    wstring out;
    for (wchar_t c : s) {
        if      (c == L'\\') out += L"\\\\";
        else if (c == L'"')  out += L"\\\"";
        else                 out += c;
    }
    return out;
}

// JSON string extractor with escape-decoding (round-trip stable)
static wstring JsonStr(const wstring& j, const wstring& key) {
    wstring k = L"\"" + key + L"\"";
    size_t p = j.find(k);
    if (p == wstring::npos) return L"";
    p = j.find(L':', p + k.size());
    if (p == wstring::npos) return L"";
    p = j.find(L'"', p);
    if (p == wstring::npos) return L"";
    size_t s = p + 1, e = s;
    while (e < j.size() && !(j[e] == L'"' && j[e - 1] != L'\\')) ++e;

    wstring raw = j.substr(s, e - s);
    wstring out; out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == L'\\' && i + 1 < raw.size()) {
            switch (raw[i + 1]) {
                case L'\\': out += L'\\'; ++i; break;
                case L'"':  out += L'"';  ++i; break;
                case L'/':  out += L'/';  ++i; break;
                case L'n':  out += L'\n'; ++i; break;
                case L't':  out += L'\t'; ++i; break;
                case L'r':  out += L'\r'; ++i; break;
                default:    out += raw[i]; break;
            }
        } else {
            out += raw[i];
        }
    }
    return out;
}

// Boolean variant — reads "true" / "false" without quotes
static bool JsonBool(const wstring& j, const wstring& key, bool def = false) {
    wstring k = L"\"" + key + L"\"";
    size_t p = j.find(k);
    if (p == wstring::npos) return def;
    p = j.find(L':', p);
    if (p == wstring::npos) return def;
    while (p < j.size() && (j[p] == L':' || j[p] == L' ' || j[p] == L'\t')) ++p;
    if (p + 4 <= j.size() && j.compare(p, 4, L"true")  == 0) return true;
    if (p + 5 <= j.size() && j.compare(p, 5, L"false") == 0) return false;
    return def;
}

// Integer variant — reads an unquoted number
static int JsonInt(const wstring& j, const wstring& key, int def = 0) {
    wstring k = L"\"" + key + L"\"";
    size_t p = j.find(k);
    if (p == wstring::npos) return def;
    p = j.find(L':', p);
    if (p == wstring::npos) return def;
    while (p < j.size() && (j[p] == L':' || j[p] == L' ' || j[p] == L'\t')) ++p;
    if (p >= j.size()) return def;
    bool neg = (j[p] == L'-');
    if (neg) ++p;
    if (p >= j.size() || !iswdigit(j[p])) return def;
    int val = 0;
    while (p < j.size() && iswdigit(j[p])) {
        val = val * 10 + (j[p] - L'0');
        ++p;
    }
    return neg ? -val : val;
}

// Floating-point variant — reads an unquoted number, integer or fractional.
// Handles negative sign and a single decimal point. Returns def on any
// malformed input or missing key (no exponent / NaN / Inf support — we
// don't need it for scale factors).
static double JsonDouble(const wstring& j, const wstring& key, double def = 0.0) {
    wstring k = L"\"" + key + L"\"";
    size_t p = j.find(k);
    if (p == wstring::npos) return def;
    p = j.find(L':', p);
    if (p == wstring::npos) return def;
    while (p < j.size() && (j[p] == L':' || j[p] == L' ' || j[p] == L'\t')) ++p;
    if (p >= j.size()) return def;
    bool neg = (j[p] == L'-');
    if (neg) ++p;
    if (p >= j.size() || (!iswdigit(j[p]) && j[p] != L'.')) return def;
    double val = 0.0;
    while (p < j.size() && iswdigit(j[p])) {
        val = val * 10.0 + (double)(j[p] - L'0');
        ++p;
    }
    if (p < j.size() && j[p] == L'.') {
        ++p;
        double frac = 0.1;
        while (p < j.size() && iswdigit(j[p])) {
            val += frac * (double)(j[p] - L'0');
            frac *= 0.1;
            ++p;
        }
    }
    return neg ? -val : val;
}

// ═══════════════════════════════════════════════════════════════════════
//  INI LINE-EDITOR
// ═══════════════════════════════════════════════════════════════════════
//
//  D2RLoader.ini contains user-managed config that we should NOT trash
//  when we touch one of its values. These helpers do line-by-line in-place
//  edits: read preserves the full file as-is, write replaces ONLY the
//  line(s) for the key(s) we care about while keeping every comment,
//  blank line, and unknown key untouched.

// Trim leading/trailing whitespace (in place).
static void TrimWs(wstring& s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.erase(s.begin());
    while (!s.empty() && (s.back()  == L' ' || s.back()  == L'\t')) s.pop_back();
}

// Returns true if `line` (after trimming) sets `key` in any way. Output
// `value` is the trimmed right-hand side; comments after the value are
// stripped so callers see just the assignment.
static bool ParseIniLine(const wstring& line, const wstring& key, wstring& value) {
    wstring s = line;
    // Strip a trailing comment marker, but only if it isn't inside the value
    // — INI comments are conventionally at line start (';') so this is rare.
    TrimWs(s);
    if (s.empty() || s[0] == L';' || s[0] == L'[') return false;
    size_t eq = s.find(L'=');
    if (eq == wstring::npos) return false;
    wstring k = s.substr(0, eq);
    TrimWs(k);
    if (k != key) return false;
    value = s.substr(eq + 1);
    TrimWs(value);
    return true;
}

// Read an INI value by section + key. Returns `def` if absent or malformed.
static int IniGetInt(const wstring& path, const wstring& section,
                     const wstring& key, int def) {
    wstring contents = ReadTextFile(path);
    if (contents.empty()) return def;

    wstring wantSection = L"[" + section + L"]";
    wstring currentSection;
    size_t lineStart = 0;
    while (lineStart <= contents.size()) {
        size_t lineEnd = contents.find(L'\n', lineStart);
        wstring line = (lineEnd == wstring::npos)
                       ? contents.substr(lineStart)
                       : contents.substr(lineStart, lineEnd - lineStart);
        // Strip trailing \r if file has CRLF endings
        if (!line.empty() && line.back() == L'\r') line.pop_back();

        wstring trimmed = line; TrimWs(trimmed);
        if (!trimmed.empty() && trimmed[0] == L'[') {
            currentSection = trimmed;
        } else if (currentSection == wantSection) {
            wstring val;
            if (ParseIniLine(line, key, val)) {
                int n = 0;
                bool any = false;
                size_t p = 0;
                while (p < val.size() && iswdigit(val[p])) {
                    n = n * 10 + (val[p] - L'0');
                    ++p; any = true;
                }
                return any ? n : def;
            }
        }

        if (lineEnd == wstring::npos) break;
        lineStart = lineEnd + 1;
    }
    return def;
}

// Edit one INI value in place. Preserves every other line. Creates the
// file (with the single requested key under the requested section) if it
// doesn't exist. If the section exists but the key doesn't, appends the
// key inside that section. If neither exists, appends both at end of file.
static void IniSetInt(const wstring& path, const wstring& section,
                      const wstring& key, int newValue) {
    wstring contents = ReadTextFile(path);
    wstring wantSection = L"[" + section + L"]";
    wchar_t valBuf[32];
    swprintf(valBuf, 32, L"%d", newValue);
    wstring valStr = valBuf;

    // Fresh file — write minimal stub
    if (contents.empty()) {
        wstring out = wantSection + L"\r\n" + key + L" = " + valStr + L"\r\n";
        WriteTextFile(path, out);
        return;
    }

    // Split into lines (preserve EOL style — detect CRLF vs LF)
    bool isCrlf = contents.find(L"\r\n") != wstring::npos;
    wstring eol = isCrlf ? L"\r\n" : L"\n";

    vector<wstring> lines;
    size_t p = 0;
    while (p <= contents.size()) {
        size_t e = contents.find(L'\n', p);
        if (e == wstring::npos) { lines.push_back(contents.substr(p)); break; }
        wstring ln = contents.substr(p, e - p);
        if (!ln.empty() && ln.back() == L'\r') ln.pop_back();
        lines.push_back(ln);
        p = e + 1;
    }

    // Find/replace within the right section, or note where to insert
    wstring currentSection;
    int sectionStart = -1;      // first line index inside our section
    int sectionEnd   = -1;      // line index of next section (or end)
    bool replaced = false;
    for (size_t i = 0; i < lines.size(); ++i) {
        wstring trimmed = lines[i]; TrimWs(trimmed);
        if (!trimmed.empty() && trimmed[0] == L'[') {
            if (currentSection == wantSection && sectionEnd < 0) {
                sectionEnd = (int)i;
            }
            currentSection = trimmed;
            if (currentSection == wantSection) sectionStart = (int)i + 1;
        } else if (currentSection == wantSection) {
            wstring oldVal;
            if (ParseIniLine(lines[i], key, oldVal)) {
                // Replace the value while keeping any leading whitespace
                // and any inline indentation around `=`.
                size_t eq = lines[i].find(L'=');
                wstring lhs = lines[i].substr(0, eq + 1);
                // Strip trailing comment on the line — preserve only key = val
                lines[i] = lhs + L" " + valStr;
                replaced = true;
                break;
            }
        }
    }

    if (!replaced) {
        if (sectionStart < 0) {
            // Section doesn't exist — append new section at end
            if (!lines.empty() && !lines.back().empty()) lines.push_back(L"");
            lines.push_back(wantSection);
            lines.push_back(key + L" = " + valStr);
        } else {
            // Section exists, key doesn't — insert at end of section
            int insertAt = (sectionEnd >= 0) ? sectionEnd : (int)lines.size();
            lines.insert(lines.begin() + insertAt, key + L" = " + valStr);
        }
    }

    // Rebuild
    wstring out;
    for (size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size()) out += eol;
    }
    WriteTextFile(path, out);
}

// ═══════════════════════════════════════════════════════════════════════
//  CONFIG I/O
// ═══════════════════════════════════════════════════════════════════════

static wstring CfgPath() { return AppDir() + L"\\launcher_config.json"; }

static void LoadCfg() {
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

    // Font / color selections (saved but not yet applied to the UI — see
    // LauncherCfg comments above).
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

static void SaveCfg() {
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

// ══════════════════════════════════════════════════════════════════════
//  D2RLOADER.INI INTEGRATION
// ══════════════════════════════════════════════════════════════════════
//
// We expose two D2RLoader.ini settings via dropdowns in a "LOADER OPTIONS"
// section at the bottom of the Modding column:
//   [Stash]            extra_shared_tabs  (0..16)
//   [Advanced.Logging] damage_indicator   (0..2)
//
// State lives in g_loaderOpts (mirror of what's on disk). LoadLoaderOpts
// reads from D2RLoader.ini at startup; the dropdowns write through
// SaveLoaderOpts which does an in-place line edit (preserves the rest
// of the file). These settings are global (not per-mod) because the
// loader's INI is shared across all mods.


struct LoaderOpts {
    int extraSharedTabs  = 0;     // [Stash] extra_shared_tabs
    int damageIndicator  = 2;     // [Advanced.Logging] damage_indicator
};
static LoaderOpts g_loaderOpts;

static wstring LoaderIniPath() {
    return g_cfg.d2rPath + L"\\D2RLoader.ini";
}

static void LoadLoaderOpts() {
    wstring p = LoaderIniPath();
    g_loaderOpts.extraSharedTabs =
        IniGetInt(p, L"Stash",             L"extra_shared_tabs", 0);
    g_loaderOpts.damageIndicator =
        IniGetInt(p, L"Advanced.Logging",  L"damage_indicator",  2);

    // Clamp to sane ranges in case the file holds something weird
    if (g_loaderOpts.extraSharedTabs < 0)  g_loaderOpts.extraSharedTabs = 0;
    if (g_loaderOpts.extraSharedTabs > 16) g_loaderOpts.extraSharedTabs = 16;
    if (g_loaderOpts.damageIndicator < 0)  g_loaderOpts.damageIndicator = 0;
    if (g_loaderOpts.damageIndicator > 2)  g_loaderOpts.damageIndicator = 2;
}

static void SaveLoaderOptStashTabs(int v) {
    IniSetInt(LoaderIniPath(), L"Stash", L"extra_shared_tabs", v);
}

static void SaveLoaderOptDamageIndicator(int v) {
    IniSetInt(LoaderIniPath(), L"Advanced.Logging", L"damage_indicator", v);
}

// ══════════════════════════════════════════════════════════════════════
//  MOD UPDATE CHECKER
// ══════════════════════════════════════════════════════════════════════
//
// Opt-in: a mod participates by adding "update_github" or
// "update_manifest" to its modinfo.json. The launcher fetches a small
// JSON document, compares versions, and exposes the result via
// g_updateInfo[modFolder]. UI elements consult that map.
//
// Checks happen on launcher startup AND when the user clicks "Refresh
// Mod List". Within UPDATE_CACHE_TTL_SECONDS we serve cached results;
// the refresh click bypasses the TTL for a force-refetch.
//
// Network I/O runs in background threads (one per mod, capped via a
// simple atomic counter). When a fetch completes, the thread posts
// MSG_UPDATE_CHECK_DONE to g_hwMain which triggers a UI repaint.

// ── Cache file I/O ────────────────────────────────────────────────────
static wstring UpdateCachePath() {
    return AppDir() + L"\\assets\\update_cache.json";
}

static void LoadUpdateCache() {
    g_updateInfo.clear();
    wstring j = ReadTextFile(UpdateCachePath());
    if (j.empty()) return;
    // Schema: { "entries": [ { "folder": "...", "remote_version": "...", ... }, ... ] }
    size_t arrStart = j.find(L"\"entries\"");
    if (arrStart == wstring::npos) return;
    arrStart = j.find(L'[', arrStart);
    if (arrStart == wstring::npos) return;
    size_t arrEnd = j.find(L']', arrStart);
    if (arrEnd == wstring::npos) return;

    size_t p = arrStart + 1;
    while (p < arrEnd) {
        size_t obj1 = j.find(L'{', p);
        if (obj1 == wstring::npos || obj1 >= arrEnd) break;
        // Track brace depth so nested objects (changelog etc) don't fool us
        int depth = 1; size_t obj2 = obj1 + 1;
        while (obj2 < arrEnd && depth > 0) {
            if (j[obj2] == L'{') ++depth;
            else if (j[obj2] == L'}') --depth;
            if (depth == 0) break;
            ++obj2;
        }
        if (obj2 >= arrEnd) break;
        wstring chunk = j.substr(obj1, obj2 - obj1 + 1);
        UpdateInfo ui;
        wstring folder = JsonStr(chunk, L"folder");
        ui.remoteVersion  = JsonStr(chunk, L"remote_version");
        ui.localVersion   = JsonStr(chunk, L"local_version");
        ui.changelog      = JsonStr(chunk, L"changelog");
        ui.downloadUrl    = JsonStr(chunk, L"download_url");
        ui.sourceUrl      = JsonStr(chunk, L"source_url");
        ui.releaseDate    = JsonStr(chunk, L"release_date");
        ui.sha256         = JsonStr(chunk, L"sha256");
        ui.skippedVersion = JsonStr(chunk, L"skipped_version");
        ui.fetchedAt      = (time_t)JsonInt(chunk, L"fetched_at", 0);
        ui.httpStatus     = JsonInt(chunk, L"http_status", 0);
        ui.fetched        = !ui.remoteVersion.empty();
        if (!folder.empty()) g_updateInfo[folder] = ui;
        p = obj2 + 1;
    }
}

static void SaveUpdateCache() {
    wstring j;
    j += L"{\n  \"entries\": [\n";
    bool first = true;
    for (const auto& kv : g_updateInfo) {
        if (!first) j += L",\n";
        first = false;
        const UpdateInfo& ui = kv.second;
        wchar_t buf[64];
        j += L"    {\n";
        j += L"      \"folder\":          \"" + EscapeJson(kv.first) + L"\",\n";
        j += L"      \"local_version\":   \"" + EscapeJson(ui.localVersion) + L"\",\n";
        j += L"      \"remote_version\":  \"" + EscapeJson(ui.remoteVersion) + L"\",\n";
        j += L"      \"changelog\":       \"" + EscapeJson(ui.changelog) + L"\",\n";
        j += L"      \"download_url\":    \"" + EscapeJson(ui.downloadUrl) + L"\",\n";
        j += L"      \"source_url\":      \"" + EscapeJson(ui.sourceUrl) + L"\",\n";
        j += L"      \"release_date\":    \"" + EscapeJson(ui.releaseDate) + L"\",\n";
        j += L"      \"sha256\":          \"" + EscapeJson(ui.sha256) + L"\",\n";
        j += L"      \"skipped_version\": \"" + EscapeJson(ui.skippedVersion) + L"\",\n";
        swprintf(buf, 64, L"%lld", (long long)ui.fetchedAt);
        j += wstring(L"      \"fetched_at\":     ") + buf + L",\n";
        swprintf(buf, 64, L"%d", ui.httpStatus);
        j += wstring(L"      \"http_status\":    ") + buf + L"\n";
        j += L"    }";
    }
    j += L"\n  ]\n}";
    WriteTextFile(UpdateCachePath(), j);
}

// ── Playtime cache I/O ───────────────────────────────────────────────────

static wstring PlaytimeCachePath() {
    return AppDir() + L"\\assets\\playtime.json";
}

// Schema: { "entries": [ { "folder": "<name>", "seconds": <int>,
//                          "last_played": <epoch> }, ... ] }
// Same minimal-parser strategy as LoadUpdateCache — walk the array,
// pull the three fields out of each object, drop into the map.
static void LoadPlaytimes() {
    g_playtimes.clear();
    wstring j = ReadTextFile(PlaytimeCachePath());
    if (j.empty()) return;
    size_t arrStart = j.find(L"\"entries\"");
    if (arrStart == wstring::npos) return;
    arrStart = j.find(L'[', arrStart);
    if (arrStart == wstring::npos) return;
    size_t arrEnd = j.find(L']', arrStart);
    if (arrEnd == wstring::npos) return;

    size_t p = arrStart + 1;
    while (p < arrEnd) {
        size_t obj1 = j.find(L'{', p);
        if (obj1 == wstring::npos || obj1 >= arrEnd) break;
        int depth = 1; size_t obj2 = obj1 + 1;
        while (obj2 < arrEnd && depth > 0) {
            if (j[obj2] == L'{') ++depth;
            else if (j[obj2] == L'}') --depth;
            if (depth == 0) break;
            ++obj2;
        }
        if (obj2 >= arrEnd) break;
        wstring chunk = j.substr(obj1, obj2 - obj1 + 1);
        wstring folder = JsonStr(chunk, L"folder");
        if (!folder.empty()) {
            PlaytimeRec r;
            r.seconds    = (uint64_t)JsonInt(chunk, L"seconds",     0);
            r.lastPlayed = (time_t)  JsonInt(chunk, L"last_played", 0);
            g_playtimes[folder] = r;
        }
        p = obj2 + 1;
    }
}

static void SavePlaytimes() {
    wstring j;
    j += L"{\n  \"entries\": [\n";
    bool first = true;
    for (const auto& kv : g_playtimes) {
        if (!first) j += L",\n";
        first = false;
        const PlaytimeRec& r = kv.second;
        wchar_t buf[64];
        j += L"    {\n";
        j += L"      \"folder\":      \"" + EscapeJson(kv.first) + L"\",\n";
        swprintf(buf, 64, L"%llu", (unsigned long long)r.seconds);
        j += wstring(L"      \"seconds\":     ") + buf + L",\n";
        swprintf(buf, 64, L"%lld", (long long)r.lastPlayed);
        j += wstring(L"      \"last_played\": ") + buf + L"\n";
        j += L"    }";
    }
    j += L"\n  ]\n}";
    WriteTextFile(PlaytimeCachePath(), j);
}

// Add a session's elapsed seconds to a mod's accumulator and stamp
// last_played to now. Persists immediately so we don't lose data on a
// crash — playtime entries are small enough that the write cost is
// trivial.
static void RecordPlaytime(const wstring& modFolder, uint64_t secondsToAdd) {
    if (modFolder.empty()) return;
    PlaytimeRec& r = g_playtimes[modFolder];
    r.seconds   += secondsToAdd;
    r.lastPlayed = time(nullptr);
    SavePlaytimes();
}

// Hover tooltip formatting helpers — see the tooltip paint code at the
// bottom of the file for where these are rendered.
static wstring FormatPlaytime(uint64_t seconds) {
    if (seconds == 0) return L"Never played";
    uint64_t h = seconds / 3600;
    uint64_t m = (seconds % 3600) / 60;
    wchar_t buf[64];
    if (h > 0)      swprintf_s(buf, 64, L"%lluh %llum", h, m);
    else if (m > 0) swprintf_s(buf, 64, L"%llum",       m);
    else            swprintf_s(buf, 64, L"< 1m");
    return buf;
}

static wstring FormatLastPlayed(time_t lastPlayed) {
    if (lastPlayed == 0) return L"Never";
    time_t now = time(nullptr);
    time_t d   = now - lastPlayed;
    if (d < 0)        d = 0;
    if (d < 60)       return L"Just now";
    wchar_t buf[64];
    if (d < 3600) {
        long m = (long)(d / 60);
        swprintf_s(buf, 64, L"%ld minute%s ago", m, m == 1 ? L"" : L"s");
        return buf;
    }
    if (d < 86400) {
        long h = (long)(d / 3600);
        swprintf_s(buf, 64, L"%ld hour%s ago", h, h == 1 ? L"" : L"s");
        return buf;
    }
    if (d < 30 * 86400) {
        long days = (long)(d / 86400);
        swprintf_s(buf, 64, L"%ld day%s ago", days, days == 1 ? L"" : L"s");
        return buf;
    }
    // > 30 days — absolute date is more meaningful than "5 months ago".
    struct tm tm_;
    localtime_s(&tm_, &lastPlayed);
    swprintf_s(buf, 64, L"%04d-%02d-%02d",
               tm_.tm_year + 1900, tm_.tm_mon + 1, tm_.tm_mday);
    return buf;
}

// ── Hover-tip state ──────────────────────────────────────────────────────
// Shown over a mod row after the cursor has rested on it for ~2 s. Hidden
// on row change, mouse leave, click, or scroll. The display surface is
// the only consumer of g_playtimes outside the recording path.
static HWND    g_hoverTipHwnd = nullptr;
static wstring g_hoverTipText1;
static wstring g_hoverTipText2;

// Defined further down the file (after the existing modal dialogs) so
// these can be called from ModListProc.
static void ShowHoverTip(int modIdx);
static void HideHoverTip();

// ── Version comparison ────────────────────────────────────────────────
//
// "2.5.0" > "2.4.1", "1.10" > "1.9", "0.9.5b" > "0.9.5a", etc.
// Semantic-ish: compares dot-separated numeric segments first, then
// any trailing string suffix lexicographically. Leading "v" stripped.
static wstring NormalizeVersion(const wstring& v) {
    wstring s = v;
    if (!s.empty() && (s[0] == L'v' || s[0] == L'V')) s = s.substr(1);
    // Trim whitespace
    while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
    while (!s.empty() && iswspace(s.back()))  s.pop_back();
    return s;
}

static int CompareVersions(const wstring& a, const wstring& b) {
    wstring sa = NormalizeVersion(a);
    wstring sb = NormalizeVersion(b);
    size_t i = 0, j = 0;
    while (i < sa.size() || j < sb.size()) {
        // Parse numeric segment from each
        long long na = 0, nb = 0;
        bool hasA = (i < sa.size() && iswdigit(sa[i]));
        bool hasB = (j < sb.size() && iswdigit(sb[j]));
        while (i < sa.size() && iswdigit(sa[i])) { na = na * 10 + (sa[i] - L'0'); ++i; }
        while (j < sb.size() && iswdigit(sb[j])) { nb = nb * 10 + (sb[j] - L'0'); ++j; }
        if (hasA || hasB) {
            if (na != nb) return (na < nb) ? -1 : 1;
        }
        // Now compare any trailing non-digit chars up to next digit or end
        wstring tailA, tailB;
        while (i < sa.size() && !iswdigit(sa[i])) { tailA += sa[i]; ++i; }
        while (j < sb.size() && !iswdigit(sb[j])) { tailB += sb[j]; ++j; }
        // Strip a single dot separator for fair compare
        if (!tailA.empty() && tailA[0] == L'.') tailA.erase(0, 1);
        if (!tailB.empty() && tailB[0] == L'.') tailB.erase(0, 1);
        if (tailA != tailB) return (tailA < tailB) ? -1 : 1;
    }
    return 0;
}

// ── WinHTTP fetch ─────────────────────────────────────────────────────
//
// Synchronous GET. Returns body as a UTF-8-decoded wstring (we treat the
// payload as UTF-8 since JSON should be). On failure, body is empty and
// httpStatus contains 0 (network error) or the HTTP code (server error).
struct HttpResult {
    int     status   = 0;     // HTTP status or 0 for network/timeout failures
    bool    timedOut = false;
    wstring body;             // UTF-8 → wstring (decoded)
};

static wstring Utf8ToWide(const string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

// Splits "https://host/path" into host + path. Returns true on success.
static bool ParseUrl(const wstring& url,
                     wstring& outHost, wstring& outPath,
                     bool& outHttps, INTERNET_PORT& outPort) {
    if (url.rfind(L"https://", 0) == 0) {
        outHttps = true; outPort = INTERNET_DEFAULT_HTTPS_PORT;
    } else if (url.rfind(L"http://", 0) == 0) {
        outHttps = false; outPort = INTERNET_DEFAULT_HTTP_PORT;
    } else {
        return false;
    }
    size_t schemeEnd = url.find(L"://") + 3;
    size_t pathStart = url.find(L'/', schemeEnd);
    if (pathStart == wstring::npos) {
        outHost = url.substr(schemeEnd);
        outPath = L"/";
    } else {
        outHost = url.substr(schemeEnd, pathStart - schemeEnd);
        outPath = url.substr(pathStart);
    }
    // Strip any ":port" from host (we don't override defaults — keep it simple)
    size_t colon = outHost.find(L':');
    if (colon != wstring::npos) outHost = outHost.substr(0, colon);
    return !outHost.empty();
}

static HttpResult HttpGet(const wstring& url, int timeoutMs) {
    HttpResult r;
    wstring host, path;
    bool https; INTERNET_PORT port;
    if (!ParseUrl(url, host, path, https, port)) return r;

    HINTERNET hSession = WinHttpOpen(L"Angiris/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return r;
    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET hConn = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConn) { WinHttpCloseHandle(hSession); return r; }

    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) {
        WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession); return r;
    }

    // GitHub requires a User-Agent for API requests
    WinHttpAddRequestHeaders(hReq,
        L"User-Agent: Angiris-Launcher/1.0\r\n"
        L"Accept: application/vnd.github+json, application/json, */*\r\n",
        (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    BOOL ok = WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_WINHTTP_TIMEOUT) r.timedOut = true;
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
        return r;
    }
    ok = WinHttpReceiveResponse(hReq, nullptr);
    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_WINHTTP_TIMEOUT) r.timedOut = true;
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
        return r;
    }

    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(hReq,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    r.status = (int)status;

    // Drain body
    string body;
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
        size_t off = body.size();
        body.resize(off + avail);
        DWORD read = 0;
        WinHttpReadData(hReq, &body[off], avail, &read);
        body.resize(off + read);
        if (body.size() > 2 * 1024 * 1024) break;  // 2 MB cap — manifests are tiny
    }
    r.body = Utf8ToWide(body);
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
    return r;
}

// Binary-safe download — stream the response body straight to a file
// on disk. Used for the launcher self-update flow: GitHub release
// zips are several MB and aren't text, so HttpGet (which decodes the
// body as UTF-8 into a wstring) would corrupt them. This variant
// mirrors the WinHttp setup of HttpGet but writes received chunks
// directly via WriteFile, with no decoding and no in-memory accumulation.
//
// Returns 0 on success or a non-zero status/error code on failure
// (HTTP status if we got that far, -1 for network/setup errors). On
// any failure the destination file is left in a partial state — the
// caller should delete it before retrying.
static int HttpDownloadFile(const wstring& url,
                            const wstring& destPath,
                            int timeoutMs) {
    wstring host, path;
    bool https; INTERNET_PORT port;
    if (!ParseUrl(url, host, path, https, port)) return -1;

    HINTERNET hSession = WinHttpOpen(L"Angiris/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return -1;
    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET hConn = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConn) { WinHttpCloseHandle(hSession); return -1; }

    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        https ? WINHTTP_FLAG_SECURE : 0);
    if (!hReq) {
        WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession); return -1;
    }
    // GitHub redirects /releases/download/... to a CDN URL — let WinHttp
    // follow it automatically (HTTP 302 → blob storage).
    DWORD redirOpt = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hReq, WINHTTP_OPTION_REDIRECT_POLICY,
                     &redirOpt, sizeof(redirOpt));

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
        return -1;
    }
    if (!WinHttpReceiveResponse(hReq, nullptr)) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
        return -1;
    }
    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(hReq,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
        return (int)status;
    }

    // Open the destination file. CREATE_ALWAYS replaces any partial
    // leftover from a previous failed download.
    HANDLE hFile = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
        return -1;
    }

    // Stream the body to disk. 64 KB chunks — small enough to keep
    // memory flat, large enough to avoid syscall overhead dominating
    // throughput.
    bool writeOk = true;
    BYTE buf[64 * 1024];
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
        DWORD toRead = (avail > sizeof(buf)) ? sizeof(buf) : avail;
        DWORD read = 0;
        if (!WinHttpReadData(hReq, buf, toRead, &read) || read == 0) break;
        DWORD written = 0;
        if (!WriteFile(hFile, buf, read, &written, nullptr)
            || written != read) {
            writeOk = false;
            break;
        }
    }
    CloseHandle(hFile);
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
    return writeOk ? 0 : -1;
}


//
// GitHub Releases API response: parse `tag_name`, `body`, `published_at`,
// first .zip in `assets`, `html_url`.
// Generic manifest: parse `latest_version`, `changelog`, `download_url`,
// `source_url`, `release_date`, optional `sha256`.

static UpdateInfo ParseGithubReleaseJson(const wstring& body,
                                          const wstring& localVersion) {
    UpdateInfo ui;
    ui.remoteVersion = JsonStr(body, L"tag_name");
    ui.changelog     = JsonStr(body, L"body");
    ui.releaseDate   = JsonStr(body, L"published_at");
    ui.sourceUrl     = JsonStr(body, L"html_url");
    // Find the first ".zip" download URL within assets[]
    size_t assets = body.find(L"\"assets\"");
    if (assets != wstring::npos) {
        size_t arr = body.find(L'[', assets);
        size_t end = (arr != wstring::npos) ? body.find(L']', arr) : wstring::npos;
        if (arr != wstring::npos && end != wstring::npos) {
            // Scan for browser_download_url ending in .zip
            size_t p = arr;
            while (p < end) {
                size_t k = body.find(L"\"browser_download_url\"", p);
                if (k == wstring::npos || k >= end) break;
                size_t colon = body.find(L':', k);
                size_t q1    = body.find(L'"', colon + 1);
                size_t q2    = (q1 != wstring::npos) ? body.find(L'"', q1 + 1) : wstring::npos;
                if (q2 == wstring::npos) break;
                wstring url = body.substr(q1 + 1, q2 - q1 - 1);
                if (url.size() >= 4 &&
                    _wcsicmp(url.c_str() + url.size() - 4, L".zip") == 0) {
                    ui.downloadUrl = url;
                    break;
                }
                p = q2 + 1;
            }
        }
    }
    ui.localVersion = localVersion;
    ui.fetched = !ui.remoteVersion.empty();
    if (ui.fetched) {
        ui.available = (CompareVersions(ui.remoteVersion, localVersion) > 0);
    }
    return ui;
}

static UpdateInfo ParseGenericManifest(const wstring& body,
                                        const wstring& localVersion) {
    UpdateInfo ui;
    ui.remoteVersion = JsonStr(body, L"latest_version");
    ui.changelog     = JsonStr(body, L"changelog");
    ui.releaseDate   = JsonStr(body, L"release_date");
    ui.sourceUrl     = JsonStr(body, L"source_url");
    ui.downloadUrl   = JsonStr(body, L"download_url");
    ui.sha256        = JsonStr(body, L"sha256");
    ui.localVersion  = localVersion;
    ui.fetched       = !ui.remoteVersion.empty();
    if (ui.fetched) {
        ui.available = (CompareVersions(ui.remoteVersion, localVersion) > 0);
    }
    return ui;
}

// ── Fetch one mod's update info ───────────────────────────────────────
//
// Resolves URL, runs HTTP GET, parses, fills UpdateInfo. Called from a
// worker thread; main thread posts MSG_UPDATE_CHECK_DONE on completion
// (so the UI repaints).
static UpdateInfo FetchModUpdateOnce(const ModInfo& mod, int timeoutMs,
                                      bool& outTimedOut) {
    outTimedOut = false;
    UpdateInfo ui;
    ui.localVersion = mod.version;

    wstring url;
    bool isGithub = false;
    if (!mod.updateGithub.empty()) {
        url = L"https://api.github.com/repos/" + mod.updateGithub + L"/releases/latest";
        isGithub = true;
    } else if (!mod.updateManifest.empty()) {
        url = mod.updateManifest;
    } else {
        return ui;     // not opted in
    }

    HttpResult r = HttpGet(url, timeoutMs);
    ui.httpStatus = r.status;
    if (r.timedOut) { outTimedOut = true; return ui; }
    if (r.status != 200 || r.body.empty()) return ui;

    if (isGithub) ui = ParseGithubReleaseJson(r.body, mod.version);
    else          ui = ParseGenericManifest (r.body, mod.version);
    ui.fetchedAt = time(nullptr);

    // Honor skipped-version: if the user previously skipped this exact
    // remote version, don't show it as available.
    auto it = g_updateInfo.find(mod.folder);
    if (it != g_updateInfo.end() && !it->second.skippedVersion.empty()
        && it->second.skippedVersion == ui.remoteVersion) {
        ui.skippedVersion = it->second.skippedVersion;
        ui.available = false;
    }
    return ui;
}

// ── Public entry point ────────────────────────────────────────────────
//
// Kicks off background fetches for every opted-in mod. force=true bypasses
// the per-mod TTL check.
static atomic<int> g_updateChecksPending(0);

static DWORD WINAPI UpdateFetchWorker(LPVOID param) {
    wstring* folder = (wstring*)param;
    // Find the ModInfo by folder name (the worker only stores folder so
    // we can't race against a g_mods reshuffle holding pointers).
    ModInfo modCopy;
    bool found = false;
    for (const auto& m : g_mods) {
        if (m.folder == *folder) { modCopy = m; found = true; break; }
    }
    if (found) {
        bool timedOut = false;
        UpdateInfo ui = FetchModUpdateOnce(modCopy, UPDATE_HTTP_TIMEOUT_FAST, timedOut);
        if (timedOut) {
            ui.timedOut = true;
            ui.fetched  = false;
        }
        // Preserve skipped_version from any previous cache entry
        auto it = g_updateInfo.find(*folder);
        if (it != g_updateInfo.end()) {
            if (ui.skippedVersion.empty())
                ui.skippedVersion = it->second.skippedVersion;
        }
        g_updateInfo[*folder] = ui;
    }
    delete folder;
    if (--g_updateChecksPending == 0) {
        SaveUpdateCache();
        if (g_hwMain) PostMessage(g_hwMain, MSG_UPDATE_CHECK_DONE, 0, 0);
    } else {
        if (g_hwMain) PostMessage(g_hwMain, MSG_UPDATE_CHECK_DONE, 0, 0);
    }
    return 0;
}

static void KickUpdateChecks(bool force) {
    time_t now = time(nullptr);
    for (const auto& m : g_mods) {
        if (m.updateGithub.empty() && m.updateManifest.empty()) continue;

        // TTL check: skip if we have a fresh enough cached result
        if (!force) {
            auto it = g_updateInfo.find(m.folder);
            if (it != g_updateInfo.end() && it->second.fetched
                && now - it->second.fetchedAt < UPDATE_CACHE_TTL_SECONDS) {
                continue;
            }
        }
        ++g_updateChecksPending;
        wstring* folder = new wstring(m.folder);
        HANDLE h = CreateThread(nullptr, 0, UpdateFetchWorker, folder, 0, nullptr);
        if (h) CloseHandle(h);
        else { --g_updateChecksPending; delete folder; }
    }
}

// ── Helper for UI consumers ───────────────────────────────────────────
static const UpdateInfo* GetUpdateInfo(const wstring& folder) {
    auto it = g_updateInfo.find(folder);
    return (it == g_updateInfo.end()) ? nullptr : &it->second;
}

//
// Persisted to <mod>\Launcher Files\launcher_mod_cfg.json. Each mod has
// its own independent set of flags; switching mods loads that mod's saved
// state, switching back restores it.
static wstring ModCfgPath(const ModInfo& mod) {
    return mod.launcherDir + L"\\launcher_mod_cfg.json";
}

// Forward declaration — definition lives after the FLAGS table.
static void EnforceLockedFlags();

static void LoadModSettings(const ModInfo& mod) {
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

static void SaveModSettings(const ModInfo& mod) {
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

// ═══════════════════════════════════════════════════════════════════════
//  D2R DETECTION + MOD SCANNING
// ═══════════════════════════════════════════════════════════════════════

static wstring FindD2RInstall() {
    struct { const wchar_t* key; const wchar_t* val; } tries[] = {
        {L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Diablo II Resurrected", L"InstallLocation"},
        {L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Diablo II Resurrected",             L"InstallLocation"},
        {L"SOFTWARE\\WOW6432Node\\Blizzard Entertainment\\Diablo II Resurrected",                       L"Path"},
    };
    for (auto& t : tries) {
        HKEY hk;
        if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, t.key, 0, KEY_READ, &hk) == ERROR_SUCCESS) {
            wchar_t buf[MAX_PATH] = {};
            DWORD sz = sizeof(buf), type = REG_SZ;
            if (RegQueryValueEx(hk, t.val, nullptr, &type, (BYTE*)buf, &sz) == ERROR_SUCCESS
                && GetFileAttributes(buf) != INVALID_FILE_ATTRIBUTES) {
                RegCloseKey(hk); return buf;
            }
            RegCloseKey(hk);
        }
    }
    const wchar_t* defs[] = {
        L"C:\\Program Files (x86)\\Diablo II Resurrected",
        L"C:\\Program Files\\Diablo II Resurrected",
        L"D:\\Diablo II Resurrected", L"E:\\Diablo II Resurrected",
        L"F:\\Diablo II Resurrected", L"G:\\Diablo II Resurrected",
    };
    for (auto* p : defs) if (GetFileAttributes(p) != INVALID_FILE_ATTRIBUTES) return p;
    return L"";
}

static ModInfo ModInfoFromJsonPath(const wstring& jsonPath) {
    wstring json = ReadTextFile(jsonPath);

    wstring name = JsonStr(json, L"name");
    if (name.empty()) name = JsonStr(json, L"savepath");

    wstring parentDir  = jsonPath.substr(0, jsonPath.rfind(L'\\'));
    wstring parentName = parentDir.substr(parentDir.rfind(L'\\') + 1);

    wstring modDir = parentDir;
    if (parentName.size() > 4
        && _wcsicmp(parentName.substr(parentName.size() - 4).c_str(), L".mpq") == 0) {
        modDir = parentDir.substr(0, parentDir.rfind(L'\\'));
    }
    wstring folder = modDir.substr(modDir.rfind(L'\\') + 1);
    if (name.empty()) name = folder;

    ModInfo mi;
    mi.name        = name;
    mi.folder      = folder;
    mi.dir         = modDir;
    mi.title       = JsonStr(json, L"title");
    mi.description = JsonStr(json, L"description");
    mi.overview    = JsonStr(json, L"overview");
    mi.version     = JsonStr(json, L"version");
    mi.author      = JsonStr(json, L"author");
    mi.docsUrl       = JsonStr(json, L"docs");
    if (mi.docsUrl.empty())
        mi.docsUrl   = JsonStr(json, L"documents");      // back-compat alias
    mi.websiteUrl    = JsonStr(json, L"website");
    mi.discordUrl    = JsonStr(json, L"discord");
    mi.updateGithub   = JsonStr(json, L"update_github");
    mi.updateManifest = JsonStr(json, L"update_manifest");
    mi.savePath       = JsonStr(json, L"savepath");

    wstring jsonDir = jsonPath.substr(0, jsonPath.rfind(L'\\'));
    mi.launcherDir = jsonDir + L"\\Launcher Files";

    wstring bannerFile = JsonStr(json, L"banner");
    if (!bannerFile.empty()) {
        mi.bannerPath = mi.launcherDir + L"\\" + bannerFile;
        if (GetFileAttributes(mi.bannerPath.c_str()) == INVALID_FILE_ATTRIBUTES)
            mi.bannerPath.clear();
    }
    return mi;
}

static vector<ModInfo> FindMods(const wstring& d2rPath) {
    vector<ModInfo> out;
    if (d2rPath.empty()) return out;
    wstring modsDir = d2rPath + L"\\mods";

    WIN32_FIND_DATA ffd;
    HANDLE h = FindFirstFile((modsDir + L"\\*").c_str(), &ffd);
    if (h == INVALID_HANDLE_VALUE) return out;

    vector<wstring> modDirs;
    do {
        if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            && wcscmp(ffd.cFileName, L".") != 0
            && wcscmp(ffd.cFileName, L"..") != 0)
            modDirs.push_back(ffd.cFileName);
    } while (FindNextFile(h, &ffd));
    FindClose(h);

    sort(modDirs.begin(), modDirs.end(),
         [](auto& a, auto& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });

    for (const auto& dir : modDirs) {
        wstring modPath = modsDir + L"\\" + dir;
        wstring direct = modPath + L"\\modinfo.json";
        if (GetFileAttributes(direct.c_str()) != INVALID_FILE_ATTRIBUTES) {
            out.push_back(ModInfoFromJsonPath(direct));
            continue;
        }
        // Nested .mpq layout
        WIN32_FIND_DATA ffd2;
        HANDLE h2 = FindFirstFile((modPath + L"\\*").c_str(), &ffd2);
        if (h2 != INVALID_HANDLE_VALUE) {
            do {
                if (!(ffd2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                wstring sub = ffd2.cFileName;
                if (sub == L"." || sub == L"..") continue;
                wstring candidate = modPath + L"\\" + sub + L"\\modinfo.json";
                if (GetFileAttributes(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    out.push_back(ModInfoFromJsonPath(candidate));
                    break;
                }
            } while (FindNextFile(h2, &ffd2));
            FindClose(h2);
        }
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════════
//  FONT LOADING
// ═══════════════════════════════════════════════════════════════════════
//
//  Bundled fonts live in assets/fonts/. We load them via FR_PRIVATE so
//  they're visible to this app only — no system font list pollution.
//  Required files: Cinzel-Regular/Bold/Black.ttf, IMFellEnglishSC-Regular.ttf.
//  Missing fonts fall back to Georgia.

// ─────────────────────────────────────────────────────────────────────
// Available fonts discovered in assets/fonts/ at startup. Four parallel
// vectors keyed by index i:
//   g_availableFonts[i]    — filename without extension (display key,
//                            persisted in cfg.fontName)
//   g_availableFamilies[i] — actual Gdiplus family name resolved via
//                            PrivateFontCollection. Currently unused by
//                            the live UI (kept for a potential future
//                            "actually apply this font" pass).
//   g_availableStyles[i]   — FontStyle flags derived from filename. Same
//                            future-use note as above.
//   g_availableAbbrevs[i]  — short label shown in the Font dropdown and
//                            idle textbox. The full filenames don't fit
//                            in the toolbar's value box (rendering them
//                            in each face had its own ambiguity issues
//                            with Cinzel-Regular vs Cinzel-Bold both
//                            reporting family "Cinzel"), so we ship a
//                            single-font abbreviated label instead.
static std::vector<std::wstring> g_availableFonts;
static std::vector<std::wstring> g_availableFamilies;
static std::vector<INT>          g_availableStyles;
static std::vector<std::wstring> g_availableAbbrevs;

// Abbreviate a font filename stem to a compact label. The pattern is:
// first three chars of the first segment, "-", first three chars of the
// LAST segment. Middle segments are dropped (Exocet-Blizzard-Medium →
// "Exo-Med"). Files with no "-" separator just get the first 4 chars.
//
//   "Cinzel-Bold"             → "Cin-Bol"
//   "Cinzel-Regular"          → "Cin-Reg"
//   "Cinzel-Black"            → "Cin-Bla"
//   "Exocet-Blizzard-Medium"  → "Exo-Med"
//   "IMFellEnglishSC-Regular" → "IMF-Reg"
static wstring AbbreviateFontName(const wstring& name) {
    if (name.empty()) return L"";
    // Split on "-"
    std::vector<wstring> parts;
    wstring cur;
    for (wchar_t c : name) {
        if (c == L'-') {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
        } else cur.push_back(c);
    }
    if (!cur.empty()) parts.push_back(cur);
    if (parts.empty()) return name.substr(0, 4);
    wstring out = parts[0].substr(0, 3);
    if (parts.size() >= 2) out += L"-" + parts.back().substr(0, 3);
    return out;
}

// ─────────────────────────────────────────────────────────────────────
// Owner-drawn popup-menu rendering context. Set by the caller right
// before TrackPopupMenu, consulted by WM_MEASUREITEM and WM_DRAWITEM
// ODT_MENU. Three modes:
//   IntValue    — each item shows its integer value (used by the
//                 existing Stash Tabs / DMG Display dropdowns).
//   StringList  — each item shows g_menuCtx.labels[id-1] (Font dropdown).
//   ColorSwatch — each item shows a color swatch followed by its label
//                 (Color dropdown — uses both labels[] and colors[]).
enum class MenuKind { IntValue, StringList, ColorSwatch, FontPreview };
struct MenuRenderCtx {
    MenuKind kind = MenuKind::IntValue;
    int itemWidth  = 80;       // logical px
    int itemHeight = 28;
    std::vector<std::wstring> labels;
    std::vector<COLORREF>     colors;
};
static MenuRenderCtx g_menuCtx;

// Color presets for the toolbar's Colour dropdown. Index order is what
// the popup shows and what we persist in LauncherCfg::fontColorIdx
// (with -1 meaning "use the default Gold"). The display label is the
// menu text; the swatch shows the actual hue.
//
// Curation rules:
//   * Luminance ≥ ~0.30 against the dark stone background so text
//     stays readable. Dark Red and Dark Green sit near that floor —
//     they're kept for the people who want a heavy, brooding tone.
//   * No pure white / pure black — both wash out against the gold
//     accent frames and produce poor contrast at the engraved chrome.
//   * Hues span the wheel: warm reds/golds/ambers, cool blues/teals,
//     a purple accent, and a single cool neutral (Silver).
//
// Note: existing cfgs may have fontColorIdx pointing at the old Black
// (was idx 0) or Bright Green (was idx 4). Both are removed here, so
// those cfgs now resolve to the colour that sits at the same numeric
// index (Dark Red for old-Black, Bright Gold for old-Bright Green) on
// next load. ApplyColorChange's range check still protects against
// out-of-bound indices (falls back to default Gold), so deleting at the
// end of the list later wouldn't break anything either.
struct ColorPreset { const wchar_t* label; COLORREF rgb; };
static const ColorPreset g_colorPresets[] = {
    { L"Dark Red",     RGB(0x8B, 0x00, 0x00) },   //  0 — kept
    { L"Crimson",      RGB(0xC8, 0x3C, 0x50) },   //  1 — NEW: warm rose-red, brighter than Dark Red
    { L"Bright Red",   RGB(0xC8, 0x10, 0x20) },   //  2 — kept
    { L"Amber",        RGB(0xFF, 0xA5, 0x32) },   //  3 — NEW: warm orange between reds and golds
    { L"Gold",         RGB(0xE8, 0xC2, 0x5E) },   //  4 — kept (was idx 5)
    { L"Bright Gold",  RGB(0xFF, 0xD7, 0x00) },   //  5 — kept (was idx 6)
    { L"Pale Gold",    RGB(0xF8, 0xE6, 0xA0) },   //  6 — kept (was idx 7)
    { L"Silver",       RGB(0xC8, 0xC8, 0xD2) },   //  7 — NEW: cool metallic neutral
    { L"Dark Green",   RGB(0x00, 0x6B, 0x1C) },   //  8 — kept
    { L"Frost Teal",   RGB(0x50, 0xC8, 0xC8) },   //  9 — NEW: cool aqua, "ice" magic feel
    { L"Sapphire",     RGB(0x50, 0x82, 0xDC) },   // 10 — NEW: jewel blue
    { L"Royal Purple", RGB(0xA0, 0x5A, 0xD2) },   // 11 — NEW: mystic accent
};

// ─────────────────────────────────────────────────────────────────────
// UI scale presets for the toolbar Scale cycling button. The percentage
// label is what the on-screen button shows; the multiplier is what gets
// stored in LauncherCfg::uiScale. Final g_scale = multiplier * g_dpiScale.
// The active preset SET is DPI-dependent (see ActiveScalePresets below):
// at 150% Windows scaling only the smaller three make sense (anything
// above 100% would push the launcher past most monitors); at 100% the
// larger three give the user room to scale up.
struct ScalePreset { const wchar_t* label; double mul; };
static const ScalePreset g_scalePresets[] = {
    { L"75%",  0.75  },
    { L"85%",  0.85  },
    { L"100%", 1.00  },
    { L"115%", 1.15  },
    { L"127%", 1.275 },
};

// Return the indices into g_scalePresets[] that are active under the
// current g_dpiScale. The boundary is 1.25 — anything at-or-above
// returns the {75/85/100} subset (typical "150%" Windows scaling),
// anything below returns the {100/115/127} subset (typical "100%"
// scaling on a high-pixel-density display).
static void ActiveScalePresets(int& a, int& b, int& c) {
    if (g_dpiScale >= 1.25) { a = 0; b = 1; c = 2; }   // 75 / 85 / 100
    else                    { a = 2; b = 3; c = 4; }   // 100 / 115 / 127
}

// Return the slider state (0/1/2) for the current cfg.uiScale. Used
// both to pick which btn_toggle*.png to render and as the starting
// index for the cycle-on-click action.
static int ScaleToggleState() {
    int a, b, c;
    ActiveScalePresets(a, b, c);
    int presetIdx[3] = { a, b, c };
    for (int i = 0; i < 3; ++i) {
        if (g_scalePresets[presetIdx[i]].mul == g_cfg.uiScale) return i;
    }
    return 0;
}

// ── On Launch toggle ─────────────────────────────────────────────────────
// Mirrors the Scale toggle's three-state pattern, but the states map to
// post-PLAY behaviors instead of UI sizes. The slider order is
// intentional — Minimize is the default (slider far left), Close is
// the destructive option, Stay Open is the "do nothing" no-op. Map
// to/from the existing LB_* enum so the launch-completion handler at
// the bottom of WM_LBUTTONUP doesn't need to change.
//
//   slider state 0  →  Minimize   (LB_MINIMIZE)
//   slider state 1  →  Close      (LB_CLOSE)
//   slider state 2  →  Stay Open  (LB_STAY)
static int OnLaunchSliderState() {
    switch (g_cfg.launchBehavior) {
        case LB_MINIMIZE: return 0;
        case LB_CLOSE:    return 1;
        case LB_STAY:     return 2;
    }
    return 0;
}
static int OnLaunchSliderStateToBehavior(int state) {
    switch (state) {
        case 0: return LB_MINIMIZE;
        case 1: return LB_CLOSE;
        case 2: return LB_STAY;
    }
    return LB_MINIMIZE;
}
static const wchar_t* OnLaunchStateLabel() {
    switch (g_cfg.launchBehavior) {
        case LB_MINIMIZE: return L"Min";
        case LB_CLOSE:    return L"Close";
        case LB_STAY:     return L"Stay";
    }
    return L"Min";
}

static void TryLoadFont(const wstring& filename) {
    wstring path = AppDir() + L"\\assets\\fonts\\" + filename;
    if (GetFileAttributes(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    if (AddFontResourceEx(path.c_str(), FR_PRIVATE, nullptr) > 0) {
        g_loadedFonts.push_back(path);
    }
}

// Scan assets/fonts/ for .ttf files. Each one is registered with the
// system as a private font (FR_PRIVATE — invisible to other apps) and
// its bare filename (without extension) is added to g_availableFonts
// so the toolbar dropdown can list it. In parallel, a temporary
// PrivateFontCollection is used to extract the actual Gdiplus family
// name from each file — the menu owner-draw uses that name to render
// each item's "STYLE" preview text in the corresponding face.
static void LoadFonts() {
    g_availableFonts.clear();
    g_availableFamilies.clear();
    g_availableStyles.clear();
    g_availableAbbrevs.clear();
    wstring dir = AppDir() + L"\\assets\\fonts\\";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*.ttf").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        wstring name = fd.cFileName;
        wstring path = dir + name;

        // Register with the process so later Gdiplus FontFamily(name)
        // lookups succeed (and so the existing CreateGdipFonts code,
        // which hard-codes face names like "Cinzel", continues to find
        // the files it expects).
        TryLoadFont(name);

        // Also add to the persistent PFC. This is the bulletproof path
        // for the user-font override: FontFamily(name, g_pfc) finds the
        // file we just registered, where FontFamily(name) without a
        // collection silently misses it on some systems (the bug that
        // made every user font selection render in Exocet anyway).
        if (g_pfc) g_pfc->AddFontFile(path.c_str());

        // Pull the family name via a throwaway PrivateFontCollection
        // holding ONLY this file — g_pfc dedupes shared families
        // (Cinzel-Regular and Cinzel-Bold both report family "Cinzel"
        // and become one entry there), so we can't safely use it to
        // attribute a family to a specific file. A per-file PFC holds
        // exactly one family, no ambiguity.
        wstring family;
        {
            Gdiplus::PrivateFontCollection pfc;
            if (pfc.AddFontFile(path.c_str()) == Ok) {
                INT cnt = pfc.GetFamilyCount();
                if (cnt > 0) {
                    FontFamily* fams = new FontFamily[cnt];
                    INT found = 0;
                    pfc.GetFamilies(cnt, fams, &found);
                    if (found > 0) {
                        WCHAR nm[LF_FACESIZE] = { 0 };
                        if (fams[0].GetFamilyName(nm) == Ok) family = nm;
                    }
                    delete[] fams;
                }
            }
        }
        g_availableFamilies.push_back(family);

        // Strip extension for the display name AND derive the FontStyle
        // bits from the filename suffix. Family alone is ambiguous when
        // multiple .ttf files share a family — Cinzel-Regular.ttf and
        // Cinzel-Bold.ttf both report "Cinzel", and without explicit
        // style bits the preview would draw both at the same weight.
        size_t dot = name.find_last_of(L'.');
        if (dot != wstring::npos) name.resize(dot);

        wstring lc = name;
        for (auto& c : lc) c = (wchar_t)towlower(c);
        INT style = FontStyleRegular;
        if (lc.find(L"bold")  != wstring::npos)  style |= FontStyleBold;
        if (lc.find(L"black") != wstring::npos)  style |= FontStyleBold;   // closest available
        if (lc.find(L"heavy") != wstring::npos)  style |= FontStyleBold;
        if (lc.find(L"italic") != wstring::npos
            || lc.find(L"oblique") != wstring::npos)
            style |= FontStyleItalic;
        g_availableStyles.push_back(style);

        g_availableFonts.push_back(name);
        g_availableAbbrevs.push_back(AbbreviateFontName(name));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void UnloadFonts() {
    for (const auto& p : g_loadedFonts)
        RemoveFontResourceEx(p.c_str(), FR_PRIVATE, nullptr);
    g_loadedFonts.clear();
}

// Try to construct a FontFamily; if it doesn't exist (font wasn't loaded),
// return the Georgia fallback so the rest of the code never sees a null.
static FontFamily* MakeFamily(const wchar_t* primary) {
    FontFamily* f = new FontFamily(primary);
    if (f->GetLastStatus() != Ok) {
        delete f;
        f = new FontFamily(L"Georgia");
        if (f->GetLastStatus() != Ok) {
            delete f;
            return nullptr;
        }
    }
    return f;
}

// Resolve the user's chosen face (g_cfg.fontName) into a fresh
// FontFamily + style bits. Called before CreateGdipFonts so the
// override is in place when the fonts are built. The FontFamily is
// looked up against g_pfc (which owns every bundled .ttf) so the
// lookup is guaranteed to find the file we registered — passing no
// collection to FontFamily(name) silently failed on some systems
// and left the override null on every pick.
static void UpdateUserFontFromCfg() {
    delete g_userFontFamilyOverride;
    g_userFontFamilyOverride = nullptr;
    g_userFontStyleOverride  = FontStyleRegular;
    if (g_cfg.fontName.empty()) return;
    for (size_t i = 0; i < g_availableFonts.size(); ++i) {
        if (g_availableFonts[i] != g_cfg.fontName) continue;
        if (i < g_availableFamilies.size() && !g_availableFamilies[i].empty()) {
            FontFamily* ff = new FontFamily(g_availableFamilies[i].c_str(), g_pfc);
            if (ff->GetLastStatus() == Ok) {
                g_userFontFamilyOverride = ff;
            } else {
                // PFC lookup failed — try the collection-less form as
                // a fallback (the system might have the font registered
                // even when our PFC pointer is missing it).
                delete ff;
                ff = new FontFamily(g_availableFamilies[i].c_str());
                if (ff->GetLastStatus() == Ok) g_userFontFamilyOverride = ff;
                else                            delete ff;
            }
        }
        if (i < g_availableStyles.size()) {
            g_userFontStyleOverride = g_availableStyles[i];
        }
        return;
    }
}

static void CreateGdipFonts() {
    // IMPORTANT: the family name passed to MakeFamily must match the font's
    // nameID 1 (family name) as Windows sees it, NOT nameID 16 (typographic
    // family). For Exocet Blizzard the nameID 1 includes "Medium" because
    // Emigre's TTF fuses the weight into the family name. Passing the wrong
    // name silently falls back to Georgia (see MakeFamily) with no visible
    // error, which is how this bug hid for a while. Inspect a TTF with
    // fonttools or the Windows Font Viewer if a font isn't appearing.
    g_ffCinzel     = MakeFamily(L"Cinzel");
    g_ffCinzelBold = MakeFamily(L"Cinzel");        // same family, weighted via FontStyle
    g_ffFell       = MakeFamily(L"IM Fell English SC");
    g_ffExocet     = MakeFamily(L"Exocet Blizzard OT Medium");
    g_ffGeorgia    = MakeFamily(L"Georgia");

    // When the user has picked a Font in the toolbar, swap that family
    // in for every Exocet-based UI font (titles, headers, buttons, mod
    // row names, PLAY). dispFam and dispStyle below collapse the
    // override-or-default decision into one place so each new Font()
    // call below stays a one-liner.
    FontFamily* dispFam  = g_userFontFamilyOverride ? g_userFontFamilyOverride : g_ffExocet;
    FontStyle   dispStyle = g_userFontFamilyOverride
                            ? (FontStyle)g_userFontStyleOverride
                            : FontStyleRegular;

    // Per-family cell-height normalization. Different families have wildly
    // different cell-to-em ratios — Cinzel Black's rendered cell at em=38
    // is ~30% taller than Exocet's at the same em, which clips mod row
    // banners and overflows the Loader Dir textbox at high DPI. The
    // launcher's design sizes (38, 26, 18, ...) are calibrated for
    // Exocet's metrics, so we treat Exocet's rendered cell as the target
    // and scale the user font's em DOWN so its cell matches.
    //
    //   cellPx(font, em)        = em * (ascent + descent) / emHeight
    //   want cellPx(user, em_u) = cellPx(Exocet, em)
    //   → em_u = em * (cellE * emU) / (emE * cellU)
    //
    // The factor is only applied when it would SHRINK the user font
    // (factor < 1). When the user font has a smaller native cell ratio
    // than Exocet, we leave emSize alone rather than stretch it up —
    // the user asked for a max height per character, not a fixed one.
    REAL emScale = 1.0f;
    if (g_userFontFamilyOverride && g_ffExocet) {
        UINT16 emE   = g_ffExocet->GetEmHeight   (FontStyleRegular);
        UINT16 cellE = g_ffExocet->GetCellAscent (FontStyleRegular)
                     + g_ffExocet->GetCellDescent(FontStyleRegular);
        UINT16 emU   = dispFam->GetEmHeight   (dispStyle);
        UINT16 cellU = dispFam->GetCellAscent (dispStyle)
                     + dispFam->GetCellDescent(dispStyle);
        if (emE > 0 && cellE > 0 && emU > 0 && cellU > 0) {
            REAL f = ((REAL)cellE * (REAL)emU) / ((REAL)emE * (REAL)cellU);
            if (f < 1.0f) emScale = f;
        }
    }
    // SFE = "scaled font em" — SF() with the per-family cap applied.
    // Use for every dispFam-based font; Georgia/raw-Exocet fonts keep
    // plain SF() since they don't switch with the user pick.
    auto SFE = [emScale](float em) { return SF(em) * emScale; };

    // Exocet (D2 menu font) carries the launcher's identity: title wordmark,
    // section headers, button labels, mod row names, flag labels, PLAY.
    // Georgia is kept for long-form / dense text where Exocet's stylized
    // forms harm legibility: cmd preview, mod description body, hero meta.
    //
    // Every pixel size below is the LOGICAL size (the value as if g_scale
    // were 1.0). SF() multiplies by g_scale so the rasterized glyphs come
    // out at the right physical size — at the default 0.85 with system DPI
    // 1.0, a 38px font renders at ~32 physical px.
    g_fHeroName  = new Font(dispFam,        SFE(38.0f), dispStyle,        UnitPixel);
    g_fHeroMeta  = new Font(g_ffGeorgia,    SF (14.0f), FontStyleItalic,  UnitPixel);
    g_fTitle     = new Font(dispFam,        SFE(26.0f), dispStyle,        UnitPixel);
    g_fSubtitle  = new Font(dispFam,        SFE(11.0f), dispStyle,        UnitPixel);
    g_fColHdr    = new Font(dispFam,        SFE(32.0f), dispStyle,        UnitPixel);
    g_fColHdrMed = new Font(dispFam,        SFE(24.0f), dispStyle,        UnitPixel);
    g_fColHdrSm  = new Font(dispFam,        SFE(16.0f), dispStyle,        UnitPixel);
    g_fExpHdr    = new Font(dispFam,        SFE(18.0f), dispStyle,        UnitPixel);
    g_fSubLbl    = new Font(dispFam,        SFE(11.0f), dispStyle,        UnitPixel);
    g_fBtn       = new Font(dispFam,        SFE(13.0f), dispStyle,        UnitPixel);
    g_fCmdArgs   = new Font(g_ffExocet,     SF (11.0f), FontStyleRegular, UnitPixel);
    g_fNav       = new Font(dispFam,        SFE(26.0f), dispStyle,        UnitPixel);
    g_fNavSm     = new Font(dispFam,        SFE(18.2f), dispStyle,        UnitPixel);
    g_fBtnLaunch = new Font(dispFam,        SFE(40.0f), dispStyle,        UnitPixel);
    g_fStatus    = new Font(g_ffGeorgia,    SF (12.0f), FontStyleItalic,  UnitPixel);
    g_fModName   = new Font(dispFam,        SFE(18.0f), dispStyle,        UnitPixel);
    g_fModSub    = new Font(g_ffGeorgia,    SF (12.0f), FontStyleItalic,  UnitPixel);
    g_fModPath   = new Font(g_ffGeorgia,    SF (11.0f), FontStyleRegular, UnitPixel);

    // ── Measure the "Seed" label width ──────────────────────────────────
    // The seed-row combo is positioned relative to the label's RENDERED
    // right edge (label_x + measured_width + 20 px gap), so the combo
    // sits a fixed distance past where "Seed" actually ends, regardless
    // of which font family the user has selected. Recomputed here so
    // ApplyFontChange's CreateGdipFonts call updates the layout on the
    // next paint without needing a separate hook.
    //
    // The measurement uses the SAME StringFormat as paint (Near +
    // NoWrap) — default StringFormat over-pads in one direction and
    // under-pads in another, which left some fonts truncated even
    // after dividing by g_scale. Matching paint exactly closes that
    // gap.
    if (g_fModName) {
        HDC hdc = GetDC(nullptr);
        if (hdc) {
            Graphics gMeasure(hdc);
            gMeasure.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            StringFormat sfMeasure;
            sfMeasure.SetAlignment(StringAlignmentNear);
            sfMeasure.SetLineAlignment(StringAlignmentCenter);
            sfMeasure.SetFormatFlags(sfMeasure.GetFormatFlags()
                                     | StringFormatFlagsNoWrap);
            RectF bounds;
            gMeasure.MeasureString(L"Seed", -1, g_fModName,
                                   PointF(0.0f, 0.0f), &sfMeasure, &bounds);
            ReleaseDC(nullptr, hdc);
            // MeasureString returns scaled pixels (g_fModName's em size
            // already bakes in g_scale via SFE). Convert back to the
            // logical units BodyLayout uses.
            double wPx = (double)bounds.Width;
            if (g_scale > 0.0) wPx /= g_scale;
            g_seedLabelLogicalW = (int)ceil(wPx);
            if (g_seedLabelLogicalW < 30) g_seedLabelLogicalW = 30;   // sanity floor
        }
    }
}

static void DestroyGdipFonts() {
    delete g_fHeroName;  delete g_fHeroMeta;
    delete g_fTitle;     delete g_fSubtitle;
    delete g_fColHdr;    delete g_fColHdrMed; delete g_fColHdrSm;  delete g_fSubLbl;
    delete g_fExpHdr;
    delete g_fBtn;       delete g_fCmdArgs;
    delete g_fNav;       delete g_fNavSm;
    delete g_fBtnLaunch; delete g_fStatus;
    delete g_fModName;   delete g_fModSub;    delete g_fModPath;
    delete g_ffCinzel;   delete g_ffCinzelBold;
    delete g_ffFell;     delete g_ffExocet;     delete g_ffGeorgia;
}

// Apply a new UI scale at runtime: persist the choice to config,
// recompute the effective g_scale, recreate every GDI+ font at the
// new size (since UnitPixel sizes bake in g_scale via SF()), resize
// the window so the new physical pixel dimensions match the new
// scale, then let WM_SIZE→Layout reposition all children. Called
// from the scale dropdown's setter in WM_LBUTTONDOWN.
static void ApplyScaleChange(double newUserScale) {
    if (newUserScale <= 0.0) return;
    if (newUserScale == g_userScale) return;     // no-op

    // 1. Persist the new choice.
    g_cfg.uiScale = newUserScale;
    SaveCfg();
    g_userScale   = newUserScale;
    g_scale       = g_userScale * g_dpiScale;

    // 2. Rebuild fonts at the new pixel sizes.
    DestroyGdipFonts();
    CreateGdipFonts();

    // 3. Resize the main window. Width is always S(WIN_W); height is
    //    S(WIN_H) or S(WIN_H + EXPAND_H) when the bottom panel is open.
    //    Position is preserved (SWP_NOMOVE) — jumping the window to the
    //    screen center on every scale change is more disruptive than
    //    helpful. The user can move it if the new size pushes off-screen.
    if (g_hwMain) {
        int newW = S(LO::WIN_W);
        int newH = S(g_bottomExpanded
                     ? (LO::WIN_H + LO::EXPAND_H)
                     : LO::WIN_H);
        SetWindowPos(g_hwMain, nullptr, 0, 0, newW, newH,
                     SWP_NOMOVE | SWP_NOZORDER);
    }

    // WM_SIZE fires from SetWindowPos and runs Layout() with the new
    // physical dimensions. We then need every child window (mod list,
    // owner-draw buttons, etc.) to repaint at the new size — a plain
    // InvalidateRect on the main window only invalidates the parent's
    // own paint, not the children's, so the mod list would render with
    // stale row-paint pixels until the user happened to hover it.
    // RedrawWindow with RDW_ALLCHILDREN | RDW_INVALIDATE cascades the
    // invalidation through every descendant in one call.
    if (g_hwMain) {
        RedrawWindow(g_hwMain, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
}

// Apply a Font dropdown selection: re-resolve the override family from
// g_cfg.fontName, rebuild every cached GDI+ Font, and force a full UI
// repaint. Called from the Font popMenu setter.
static void ApplyFontChange() {
    UpdateUserFontFromCfg();
    DestroyGdipFonts();
    CreateGdipFonts();
    if (g_hwMain) {
        RedrawWindow(g_hwMain, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
}

// Apply a Colour dropdown selection: reassign Tok::Gold (and the
// matching highlight Tok::GoldBright) so every existing paint site
// picks up the new color the next time it constructs a brush/pen.
// Called from the Colour popMenu setter.
static void ApplyColorChange() {
    // Default state: original launcher gold (no preset chosen).
    if (g_cfg.fontColorIdx < 0 ||
        g_cfg.fontColorIdx >= (int)(sizeof(g_colorPresets)/sizeof(g_colorPresets[0]))) {
        Tok::Gold       = GP(0xC8, 0xA8, 0x4B);
        Tok::GoldBright = GP(0xFF, 0xD7, 0x00);
    } else {
        const ColorPreset& cp = g_colorPresets[g_cfg.fontColorIdx];
        Color c(255, GetRValue(cp.rgb), GetGValue(cp.rgb), GetBValue(cp.rgb));
        Tok::Gold       = c;
        // GoldBright is used for hover/selected highlights. Lifting it
        // a fixed amount above the base picks up brightness on the
        // dark-color presets without inventing a separate palette.
        BYTE r = (BYTE)min(255, GetRValue(cp.rgb) + 0x30);
        BYTE g = (BYTE)min(255, GetGValue(cp.rgb) + 0x30);
        BYTE b = (BYTE)min(255, GetBValue(cp.rgb) + 0x30);
        Tok::GoldBright = Color(255, r, g, b);
    }
    if (g_hwMain) {
        RedrawWindow(g_hwMain, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  GDI+ HELPERS
// ═══════════════════════════════════════════════════════════════════════

// Double-buffered paint into a memory DC, blit to dst on dtor.
struct MemDC {
    HDC dst, dc; HBITMAP bmp, oldBmp;
    int x, y, w, h;
    MemDC(HDC d, int X, int Y, int W, int H,
          COLORREF initFill = 0x000000)
        : dst(d), x(X), y(Y), w(W), h(H) {
        dc = CreateCompatibleDC(d);
        bmp = CreateCompatibleBitmap(d, w, h);
        oldBmp = (HBITMAP)SelectObject(dc, bmp);

        // Initialize the back buffer with raw GDI BEFORE any GDI+ work
        // touches it. Without this, if a later GDI+ call silently fails,
        // the destructor's BitBlt copies uninitialized memory to dst.
        RECT br = { 0, 0, w, h };
        HBRUSH hb = CreateSolidBrush(initFill);
        FillRect(dc, &br, hb);
        DeleteObject(hb);

        // Translate so 0,0 is top-left of the area we're drawing
        SetViewportOrgEx(dc, -x, -y, nullptr);
    }
    ~MemDC() {
        SetViewportOrgEx(dc, 0, 0, nullptr);
        BitBlt(dst, x, y, w, h, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBmp);
        DeleteObject(bmp);
        DeleteDC(dc);
    }
};

static void FillSolid(HDC hdc, int x, int y, int w, int h, COLORREF c) {
    RECT r = { x, y, x + w, y + h };
    HBRUSH br = CreateSolidBrush(c);
    FillRect(hdc, &r, br);
    DeleteObject(br);
}

// Draw text with shadow + glow (spec §3 — gold text glow pattern)
static void DrawGoldText(Graphics& g, const wstring& s, Font* fnt,
                         RectF rect, StringFormat* sf,
                         Color main = Tok::Gold, bool glow = true) {
    if (glow) {
        // Atmospheric drop
        SolidBrush shadow(GPA(180, 0, 0, 0));
        RectF sr = rect; sr.Y += 2; sr.X += 0;
        g.DrawString(s.c_str(), -1, fnt, sr, sf, &shadow);
    }
    SolidBrush mb(main);
    g.DrawString(s.c_str(), -1, fnt, rect, sf, &mb);
}

// ═══════════════════════════════════════════════════════════════════════
//  MOD FOLDER WATCHER
// ═══════════════════════════════════════════════════════════════════════
//
//  Watches <D2R>\mods\ for changes and posts MSG_MODS_DIRTY to the main
//  window when anything moves. The main window doesn't auto-rescan — it
//  just lights up the "Refresh Mod List" button to let the user know
//  there's new state to pick up. This keeps the UI from flickering
//  every time a mod is installed/extracted.

constexpr UINT MSG_MODS_DIRTY = WM_USER + 11;
constexpr UINT IDT_MODS_DEBOUNCE = 9001;

static HANDLE g_watchThread     = nullptr;
static HANDLE g_watchCancelEvt  = nullptr;     // set to wake the thread
static HANDLE g_watchDirHandle  = nullptr;     // owned by watcher thread
static wstring g_watchDirPath;                 // last-watched path

static DWORD WINAPI ModsWatcherThread(LPVOID) {
    constexpr DWORD BUFFER_SIZE = 16 * 1024;   // 16KB — covers normal bursts
    BYTE* buffer = (BYTE*)malloc(BUFFER_SIZE);
    if (!buffer) return 1;

    OVERLAPPED overlapped = {};
    HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    overlapped.hEvent = evt;

    while (true) {
        // Wait for cancellation OR a directory change
        DWORD bytes = 0;
        BOOL ok = ReadDirectoryChangesW(
            g_watchDirHandle, buffer, BUFFER_SIZE,
            TRUE,                                    // watch subtree
            FILE_NOTIFY_CHANGE_FILE_NAME
                | FILE_NOTIFY_CHANGE_DIR_NAME
                | FILE_NOTIFY_CHANGE_LAST_WRITE,
            &bytes, &overlapped, nullptr);
        if (!ok) break;

        HANDLE waitOn[2] = { evt, g_watchCancelEvt };
        DWORD w = WaitForMultipleObjects(2, waitOn, FALSE, INFINITE);
        if (w == WAIT_OBJECT_0 + 1) {
            // Cancellation
            CancelIoEx(g_watchDirHandle, &overlapped);
            break;
        }
        if (w != WAIT_OBJECT_0) break;

        // Got a real change — let the main thread debounce + rescan
        if (g_hwMain) PostMessage(g_hwMain, MSG_MODS_DIRTY, 0, 0);
    }

    CloseHandle(evt);
    free(buffer);
    return 0;
}

static void StopModsWatcher() {
    if (!g_watchThread) return;
    if (g_watchCancelEvt) SetEvent(g_watchCancelEvt);
    if (g_watchDirHandle) CancelIoEx(g_watchDirHandle, nullptr);
    WaitForSingleObject(g_watchThread, 1500);
    CloseHandle(g_watchThread);   g_watchThread    = nullptr;
    if (g_watchCancelEvt) { CloseHandle(g_watchCancelEvt); g_watchCancelEvt = nullptr; }
    if (g_watchDirHandle) { CloseHandle(g_watchDirHandle); g_watchDirHandle = nullptr; }
    g_watchDirPath.clear();
}

static void StartModsWatcher() {
    // No-op if already watching the same folder
    wstring target = g_cfg.d2rPath.empty() ? L"" : (g_cfg.d2rPath + L"\\mods");
    if (target == g_watchDirPath && g_watchThread) return;

    StopModsWatcher();   // tear down any previous watcher

    if (target.empty()
        || GetFileAttributes(target.c_str()) == INVALID_FILE_ATTRIBUTES)
        return;

    g_watchDirHandle = CreateFileW(
        target.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);
    if (g_watchDirHandle == INVALID_HANDLE_VALUE) {
        g_watchDirHandle = nullptr;
        return;
    }

    g_watchCancelEvt = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    g_watchDirPath   = target;
    g_watchThread    = CreateThread(nullptr, 0, ModsWatcherThread, nullptr, 0, nullptr);
}




// ═══════════════════════════════════════════════════════════════════════
//  LAUNCH FLAG DEFINITIONS
// ═══════════════════════════════════════════════════════════════════════
//
//  The six launch flags live in g_modSettings (per-mod). FlagDef binds
//  each to a member pointer plus its CLI arg and display label. Locked
//  flags are forced true after every config load and ignore clicks.

struct FlagDef {
    bool ModSettings::* member;
    const wchar_t* name;        // "No Sound"
    const wchar_t* arg;         // "-ns"
    const wchar_t* desc;        // "Disables all audio"
    bool isLocked;              // true → always on, cannot be toggled
};

// "Use Txts" is locked on for now: the current D2RLoader.exe requires it.
// Toggle this flag back to false once the loader supports running without -txt.
// Order matters: this controls BOTH the visual layout of the 2×3 grid
// AND the order in which flags appear in the launch-args string. The
// grid fills left-to-right, top-to-bottom (col = i%2, row = i/2):
//
//   [0: Use Txts  ] [1: Respec     ]   ← top row
//   [2: Window    ] [3: Reset Maps ]   ← middle row
//   [4: No Sound  ] [5: Skip Intro ]   ← bottom row
//
// BuildLaunchArgs walks this array in order, so the cmd preview reads:
//   -mod MODNAME -txt -enablerespec -w -resetofflinemaps -ns -skiplogovideo -seed VALUE
// (-seed is appended last by BuildLaunchArgs from g_modSettings.seedArg
//  when non-empty; the seed UI lives in its own row below the flag grid.)
static const FlagDef FLAGS[] = {
    { &ModSettings::useTxt,    L"Use Txts",   L"-txt",             L"Use raw .txt data",         true  },
    { &ModSettings::respec,    L"Respec",     L"-enablerespec",    L"Allow free skill respec",   false },
    { &ModSettings::windowed,  L"Window",     L"-w",               L"Run in a window",           false },
    { &ModSettings::resetMaps, L"Reset Maps", L"-resetofflinemaps",L"Re-roll all map seeds",     false },
    { &ModSettings::noSound,   L"No Sound",   L"-ns",              L"Disables all audio",        false },
    { &ModSettings::skipIntro, L"Skip Intro", L"-skiplogovideo",   L"Skip the intro videos",     false },
};

// Force any locked flag to its required state. Called by LoadModSettings
// so old saved configs that had -txt off get corrected automatically.
static void EnforceLockedFlags() {
    for (const auto& f : FLAGS) {
        if (f.isLocked) g_modSettings.*(f.member) = true;
    }
}

// ── -seed dropdown contents ───────────────────────────────────────────────
// Loaded once at startup from assets/seeds.json. The JSON has two arrays:
//
//   "seeds"  — author-defined presets ({"name": "...", "value": "..."}).
//              Display names + values; never written back from the launcher.
//   "recent" — up to 3 entries of values the user has TYPED into the seed
//              text input, oldest first. Slot 0 = oldest (displayed as
//              "Recent1"), slot 2 = newest (displayed as "Recent3").
//              The launcher rewrites this array whenever the user commits
//              a new typed value; presets pass through unmodified.
//
// In memory the two lists are kept separate (g_seedNames / g_seedValues for
// presets, g_recentSeeds for the rolling history). The dropdown renders
// them as one list with recents first (newest at top) for fast re-pick.
static std::vector<wstring> g_seedNames;
static std::vector<wstring> g_seedValues;
static std::vector<wstring> g_recentSeeds;     // 0..3 entries; back = newest
// Last index the user picked from the dropdown — used as the default
// when the user re-checks the Seed checkbox after having cleared it.
// Not persisted; resets to 0 on launch.
static int g_lastSeedIdx = 0;

// Path to seeds.json (called from multiple places — keep the formula in
// one spot so a future move to a config-folder location is one edit).
static wstring SeedsJsonPath() {
    return AppDir() + L"\\assets\\seeds.json";
}

// Parse a JSON array body ([...]) into a list of {name, value} pairs and
// emit them via the callback. Used for both the "seeds" and "recent"
// arrays since they share the same {name, value} entry shape.
static void ParseSeedArray(const wstring& body,
                           void (*emit)(const wstring& name, const wstring& value,
                                        void* user),
                           void* user) {
    size_t pos = 0;
    while (pos < body.size()) {
        size_t open = body.find(L'{', pos);
        if (open == wstring::npos) break;
        size_t close = body.find(L'}', open + 1);
        if (close == wstring::npos) break;
        wstring block = body.substr(open, close - open + 1);
        wstring nm = JsonStr(block, L"name");
        wstring vl = JsonStr(block, L"value");
        if (!vl.empty()) emit(nm, vl, user);
        pos = close + 1;
    }
}

static void LoadSeedsJson() {
    g_seedNames.clear();
    g_seedValues.clear();
    g_recentSeeds.clear();

    wstring j = ReadTextFile(SeedsJsonPath());
    if (!j.empty()) {
        // Find each top-level array key, then parse the array body. The
        // launcher's hand-rolled JSON style means we use string search
        // rather than a proper parser — keys are at known positions and
        // there's no nested ambiguity.
        auto findArray = [&](const wchar_t* key) -> wstring {
            wstring k = wstring(L"\"") + key + L"\"";
            size_t p = j.find(k);
            if (p == wstring::npos) return L"";
            p = j.find(L'[', p);
            if (p == wstring::npos) return L"";
            // Match the closing ']' by depth.
            int depth = 1;
            size_t q = p + 1;
            while (q < j.size() && depth > 0) {
                if (j[q] == L'[') depth++;
                else if (j[q] == L']') depth--;
                if (depth == 0) break;
                ++q;
            }
            return j.substr(p + 1, q - p - 1);
        };

        wstring seedsBody  = findArray(L"seeds");
        wstring recentBody = findArray(L"recent");

        struct PresetCtx { std::vector<wstring>* names; std::vector<wstring>* values; };
        PresetCtx pc{ &g_seedNames, &g_seedValues };
        ParseSeedArray(seedsBody,
            [](const wstring& nm, const wstring& vl, void* u) {
                auto* c = (PresetCtx*)u;
                c->names ->push_back(nm.empty() ? vl : nm);
                c->values->push_back(vl);
            }, &pc);

        ParseSeedArray(recentBody,
            [](const wstring& /*nm*/, const wstring& vl, void* u) {
                auto* v = (std::vector<wstring>*)u;
                if (v->size() < 3) v->push_back(vl);
            }, &g_recentSeeds);
    }
    // Fallback presets if seeds.json is missing or has no "seeds" array.
    // Drop a real file in assets/ and these go away on next launch.
    if (g_seedNames.empty()) {
        g_seedNames  = { L"Random (0)",    L"Standard (1)", L"Speedrun A",   L"Speedrun B" };
        g_seedValues = { L"0",             L"1",            L"1234567",      L"7654321"    };
    }
}

// Rewrite assets/seeds.json preserving the presets and updating the
// recents array. Called from CommitTypedSeedToRecents after a manually
// typed value is committed.
static void SaveSeedsJson() {
    wstring j;
    j += L"{\n";
    j += L"  \"seeds\": [\n";
    for (size_t i = 0; i < g_seedNames.size(); ++i) {
        j += L"    {\"name\": \"" + EscapeJson(g_seedNames[i])
          +  L"\", \"value\": \"" + EscapeJson(g_seedValues[i]) + L"\"}";
        if (i + 1 < g_seedNames.size()) j += L",";
        j += L"\n";
    }
    j += L"  ],\n";
    j += L"  \"recent\": [\n";
    for (size_t i = 0; i < g_recentSeeds.size(); ++i) {
        // Slot label is fixed by position: index 0 → "Recent1" (oldest),
        // index 2 → "Recent3" (newest). Matches the spec.
        wchar_t nm[16]; swprintf(nm, 16, L"Recent%zu", i + 1);
        j += L"    {\"name\": \"" + wstring(nm)
          +  L"\", \"value\": \"" + EscapeJson(g_recentSeeds[i]) + L"\"}";
        if (i + 1 < g_recentSeeds.size()) j += L",";
        j += L"\n";
    }
    j += L"  ]\n";
    j += L"}\n";
    WriteTextFile(SeedsJsonPath(), j);
}

// Add `value` to the recents history, shifting older entries down. If
// `value` matches an existing preset or recent, it's not duplicated.
// Returns true if the recents list was changed (caller can decide
// whether to redraw / re-save).
static bool CommitTypedSeedToRecents(const wstring& value) {
    if (value.empty()) return false;
    // Don't pollute recents with values that already match a preset
    // (the user can just re-pick from the dropdown).
    for (const auto& v : g_seedValues)
        if (v == value) return false;
    // Don't duplicate within recents — if it's already there, leave it
    // alone rather than reshuffling identical entries.
    for (const auto& v : g_recentSeeds)
        if (v == value) return false;
    // Shift down: drop the oldest entry once we'd exceed 3, then append
    // the new value at the end (which becomes "Recent3" / newest).
    g_recentSeeds.push_back(value);
    while (g_recentSeeds.size() > 3) g_recentSeeds.erase(g_recentSeeds.begin());
    SaveSeedsJson();
    return true;
}

// Return the index in g_seedValues whose value matches `val`, or -1 when
// nothing matches (e.g. a previously saved seed that no longer exists in
// the dropdown list).
static int FindSeedIndexForValue(const wstring& val) {
    for (int i = 0; i < (int)g_seedValues.size(); ++i)
        if (g_seedValues[i] == val) return i;
    return -1;
}

// ── Custom checkbox glyph (rune-X style) ─────────────────────────────
//
// Drawn directly into Graphics. State:
//   checked + isLocked → checkbox_locked.png  (asset's yellow-X variant)
//   checked            → checkbox_checked.png (asset's X-mark)
//   unchecked          → checkbox.png         (asset's empty box)
//
// Falls back to a programmatic gold-and-X rendering when the assets
// aren't available, so the launcher still functions visually with no art.
static void DrawFlagCheckbox(Graphics& g, REAL x, REAL y, bool checked,
                             bool hover, bool isLocked) {
    constexpr int CB_SIZE = 27;     // matches asset native size (27×28)
    REAL S  = (REAL)CB_SIZE;
    REAL Sh = 28.0f;                // height per asset

    // Asset path
    const wchar_t* assetName = checked
        ? (isLocked ? L"checkbox_locked.png" : L"checkbox_checked.png")
        : L"checkbox.png";
    Gdiplus::Bitmap* bm = AssetImage(assetName);
    if (bm) {
        InterpolationMode prev = g.GetInterpolationMode();
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(bm, (int)x, (int)y, (int)S, (int)Sh);
        g.SetInterpolationMode(prev);
        // Hover highlight: a faint gold outline overlay on top of the asset
        if (hover) {
            Pen out(GPA(140, 0xE8, 0xC2, 0x5E), 1.5f);
            g.DrawRectangle(&out, x, y, S - 1, Sh - 1);
        }
        return;
    }

    // ── Programmatic fallback (original behavior) ───────────────────────
    if (checked) {
        SolidBrush base(Tok::GoldDeep);
        g.FillRectangle(&base, x, y, S, S);
        LinearGradientBrush hl(PointF(x, y), PointF(x, y + S),
            GP(0xC8, 0xA8, 0x4B), GP(0x6B, 0x4F, 0x1A));
        g.FillRectangle(&hl, x + 1, y + 1, S - 2, S - 2);
        Pen out(hover ? Tok::GoldBright : Tok::Gold, 1.5f);
        g.DrawRectangle(&out, x, y, S - 1, S - 1);
        Pen glyph(GP(0x2A, 0x1A, 0x0A), 2.0f);
        glyph.SetStartCap(LineCapRound);
        glyph.SetEndCap(LineCapRound);
        REAL pad = 4.0f;
        g.DrawLine(&glyph, x + pad,       y + pad,       x + S - pad, y + S - pad);
        g.DrawLine(&glyph, x + S - pad,   y + pad,       x + pad,     y + S - pad);
    } else {
        SolidBrush bg(Tok::BgPanel2);
        g.FillRectangle(&bg, x + 1, y + 1, S - 2, S - 2);
        Pen out(hover ? Tok::Gold : Tok::GoldDim, 1.0f);
        g.DrawRectangle(&out, x, y, S - 1, S - 1);
    }
}


// ═══════════════════════════════════════════════════════════════════════
//  TOOL RESOLUTION HELPERS
// ═══════════════════════════════════════════════════════════════════════
//
//  Logic: when the user clicks a tool button, we look up the cached path
//  in g_cfg. If empty or invalid, we recursively scan the user's tools
//  directory for a matching exe (or .lnk shortcut). If still not found,
//  we offer a file picker so the user can locate it manually. Resolved
//  paths get cached back into g_cfg so subsequent clicks are instant.

// Resolve a Windows .lnk shortcut to its target exe path.
static wstring ResolveShortcut(const wstring& lnkPath) {
    HRESULT hr;
    IShellLinkW* psl = nullptr;
    wstring result;

    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                   (void**)&psl))) {
        IPersistFile* ppf = nullptr;
        if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void**)&ppf))) {
            if (SUCCEEDED(ppf->Load(lnkPath.c_str(), STGM_READ))) {
                wchar_t buf[MAX_PATH] = {};
                if (SUCCEEDED(psl->GetPath(buf, MAX_PATH, nullptr, 0)))
                    result = buf;
            }
            ppf->Release();
        }
        psl->Release();
    }
    return result;
    (void)hr;
}

// Recursive search for `targetExe` (e.g. "Code.exe") under `root`.
// Also matches `targetStem.lnk` (e.g. "Code.lnk") and resolves the link.
// Capped at 6 levels deep + 5000 visited to avoid pathological loops.
// Returns absolute path on hit, empty on miss.
static wstring SearchToolRecursive(const wstring& root, const wstring& targetExe,
                                   int depth = 0, int* visited = nullptr) {
    int localVisited = 0;
    if (!visited) visited = &localVisited;
    if (depth > 6 || *visited > 5000) return L"";

    // Stem: "Code.exe" -> "Code"
    wstring stem = targetExe;
    size_t dot = stem.rfind(L'.');
    if (dot != wstring::npos) stem = stem.substr(0, dot);
    wstring linkName = stem + L".lnk";

    WIN32_FIND_DATAW ffd;
    HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &ffd);
    if (h == INVALID_HANDLE_VALUE) return L"";

    wstring found;
    do {
        ++(*visited);
        wstring name = ffd.cFileName;
        if (name == L"." || name == L"..") continue;
        wstring full = root + L"\\" + name;

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            wstring sub = SearchToolRecursive(full, targetExe, depth + 1, visited);
            if (!sub.empty()) { found = sub; break; }
        } else {
            // Direct .exe match
            if (_wcsicmp(name.c_str(), targetExe.c_str()) == 0) {
                found = full; break;
            }
            // Shortcut match — resolve it
            if (_wcsicmp(name.c_str(), linkName.c_str()) == 0) {
                wstring target = ResolveShortcut(full);
                if (!target.empty()) { found = target; break; }
            }
        }
    } while (FindNextFileW(h, &ffd));
    FindClose(h);
    return found;
}

// Pop a "locate manually" file picker. Accepts both .exe and .lnk.
static wstring BrowseForTool(HWND owner, const wstring& title) {
    wchar_t buf[MAX_PATH] = {};
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner   = owner;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrFilter = L"Programs and shortcuts (*.exe;*.lnk)\0*.exe;*.lnk\0"
                      L"Programs (*.exe)\0*.exe\0"
                      L"Shortcuts (*.lnk)\0*.lnk\0"
                      L"All files (*.*)\0*.*\0";
    ofn.lpstrTitle  = title.c_str();
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return L"";

    wstring picked = buf;
    // If the user picked a shortcut, resolve to target so we can launch directly
    size_t dot = picked.rfind(L'.');
    if (dot != wstring::npos
        && _wcsicmp(picked.c_str() + dot, L".lnk") == 0) {
        wstring target = ResolveShortcut(picked);
        if (!target.empty()) return target;
    }
    return picked;
}

// Click handler for one of the three tool buttons.
//   cachedPath: reference to the relevant g_cfg.toolXxx field
//   exeName:    e.g. "Code.exe"
//   friendlyName: shown in the "not found" dialog
static void LaunchTool(HWND owner, wstring& cachedPath,
                       const wstring& exeName, const wstring& friendlyName) {
    // 1. Try cached path
    if (!cachedPath.empty()
        && GetFileAttributes(cachedPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        ShellExecute(owner, L"open", cachedPath.c_str(),
                     nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }

    // 2. Search the configured tools directory
    if (!g_cfg.toolsDir.empty()
        && GetFileAttributes(g_cfg.toolsDir.c_str()) != INVALID_FILE_ATTRIBUTES) {
        wstring hit = SearchToolRecursive(g_cfg.toolsDir, exeName);
        if (!hit.empty()) {
            cachedPath = hit;
            SaveCfg();
            ShellExecute(owner, L"open", hit.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }
    }

    // 3. Not found — ask the user
    wstring msg = friendlyName + L" was not found";
    if (g_cfg.toolsDir.empty())
        msg += L".\n\nNo Tools Directory has been set. Browse to the executable manually?";
    else
        msg += L"\nin: " + g_cfg.toolsDir + L"\n\nBrowse manually?";

    int rc = MessageBox(owner, msg.c_str(),
                        L"Tool Not Found", MB_YESNO | MB_ICONQUESTION);
    if (rc != IDYES) return;

    wstring picked = BrowseForTool(owner, L"Locate " + friendlyName);
    if (picked.empty()) return;
    cachedPath = picked;
    SaveCfg();
    ShellExecute(owner, L"open", picked.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}


// ═══════════════════════════════════════════════════════════════════════
//  CMD PREVIEW BUILDER
// ═══════════════════════════════════════════════════════════════════════
//
//  Returns the launch command string from current g_modSettings, formatted
//  for display ("-mod RoK -ns -w"). Used both by the live preview in the
//  footer and as the actual args passed to D2RLoader.exe.

static wstring BuildLaunchArgs() {
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
//  MAIN WINDOW LAYOUT
// ═══════════════════════════════════════════════════════════════════════
//
//  Three regions stacked top-to-bottom:
//
//    BODY    — always visible. 3 columns:
//                LEFT RAIL (280px)  : logo, nav buttons, Loader Options
//                CENTER             : MODS header + list + browse/update btns
//                RIGHT COL (440px)  : Mod Description / Launch Options stacked
//
//    BOTTOM EXPANSION ARROW — always visible, ~24px tall strip with a
//                centered arrow toggle.
//
//    BOTTOM PANEL — visible only when g_bottomExpanded. 3 sub-sections:
//                GUIDES & REFERENCES | TOOLS & PROGRAMS | DOWNLOADS
//
//  Window height grows by LO::EXPAND_H when the panel is open.

static void RepositionForExpansion();   // forward decl

// ── Right-column body geometry ──────────────────────────────────────────
//
// Declared here (before Layout) because Layout uses it to position the
// right-column Win32 button children. The painters that consume the same
// geometry live further down with the BODY GEOMETRY + PAINT section.

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

static BodyLayout ComputeBodyLayout(int W, int H) {
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
struct LeftPanelGeom { int x, y, w, h; int mainTop, dividerY, bottom; };
static LeftPanelGeom ComputeLeftPanelGeom(const BodyLayout& B) {
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
static RECT BodyFlagRect(const BodyLayout& B, int flagIdx) {
    int col = flagIdx % BODY_FLAG_GRID_COLS;
    int row = flagIdx / BODY_FLAG_GRID_COLS;
    int x = B.loX + 8 + col * B.loFlagColW;
    int y = B.loFlagsY + row * B.loFlagRowH;
    return RECT{ x, y, x + B.loFlagColW - 4, y + B.loFlagRowH };
}

// Seed row layout — same horizontal pattern as the flag rows so the
// checkbox lines up with the flag-grid checkboxes vertically:
//   [checkbox 27x28] gap 8 [label "Seed"]      [text input ~136x26] [arrow 24x26]
// loSeedY / loSeedH define the row band; the helpers below return the
// individual clickable rects. The text input + arrow read visually as
// one chrome (single text_box.png stretched across both), but they have
// separate hit-tests: input area = focus + start typing, arrow area =
// open the seeds dropdown.
constexpr int SEED_CB_W    = 27;
constexpr int SEED_CB_H    = 28;
constexpr int SEED_COMBO_W = 160;     // total width of input + arrow
constexpr int SEED_COMBO_H = 26;
constexpr int SEED_ARROW_W = 24;      // arrow button slice on the right edge

static RECT BodySeedCheckRect(const BodyLayout& B) {
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
static RECT BodySeedComboRect(const BodyLayout& B) {
    RECT cb = BodySeedCheckRect(B);
    int labelX     = cb.right + 8;
    int labelEnd   = labelX + g_seedLabelLogicalW;
    int x          = labelEnd + 30;
    int y = B.loSeedY + (B.loSeedH - SEED_COMBO_H) / 2;
    return { x, y, x + SEED_COMBO_W, y + SEED_COMBO_H };
}
// Text input area — everything inside the combo EXCEPT the arrow slice.
static RECT BodySeedInputRect(const BodyLayout& B) {
    RECT c = BodySeedComboRect(B);
    return { c.left, c.top, c.right - SEED_ARROW_W, c.bottom };
}
// Arrow button — the right SEED_ARROW_W slice of the combo.
static RECT BodySeedArrowRect(const BodyLayout& B) {
    RECT c = BodySeedComboRect(B);
    return { c.right - SEED_ARROW_W, c.top, c.right, c.bottom };
}

// Text-input state for the seed value. The input is custom-painted
// (Approach B) rather than a Win32 EDIT control so it matches the
// launcher's chrome. Focus is virtual — when g_seedInputFocused is
// true the main window's WM_CHAR / WM_KEYDOWN route to the seed
// input handlers; click anywhere else clears it.
//
// Selection model: g_seedSelStart is the anchor end of a selection
// range; g_seedCaretPos is the moving end. When g_seedSelStart < 0,
// there is no selection (caret-only mode) and only the caret is
// rendered. When it's >= 0 AND != g_seedCaretPos, paint draws a
// highlight rect from min(start, caret) to max(start, caret), and
// editing ops (typing / backspace / delete / paste) replace the
// selected range. Shift+arrow keys set the anchor (if not already
// set) and move the caret; plain arrow keys clear the anchor and
// collapse the caret to the appropriate end.
static bool g_seedInputFocused = false;
static int  g_seedCaretPos     = 0;     // caret index within g_modSettings.seedArg
static int  g_seedSelStart     = -1;    // selection anchor; -1 = no selection
static bool g_seedCaretVisible = true;  // blink toggle
static bool g_seedDragging     = false; // mouse held down inside the input — drives drag-select
constexpr UINT IDT_SEED_CARET = 9201;
constexpr UINT SEED_CARET_BLINK_MS = 530;
// Maximum length we'll accept (uint32 fits in 10 decimal digits;
// 12 leaves a little room without inviting arbitrarily long inputs).
constexpr int SEED_MAX_DIGITS = 12;

// Low / high indices of the current selection, normalised so callers
// don't need to know which end is the anchor. When there's no
// selection both return g_seedCaretPos (so erase(lo, hi-lo) is a
// no-op rather than a wraparound).
static int SeedSelLo() {
    if (g_seedSelStart < 0) return g_seedCaretPos;
    return min(g_seedSelStart, g_seedCaretPos);
}
static int SeedSelHi() {
    if (g_seedSelStart < 0) return g_seedCaretPos;
    return max(g_seedSelStart, g_seedCaretPos);
}
static bool SeedHasSelection() {
    return g_seedSelStart >= 0 && g_seedSelStart != g_seedCaretPos;
}
// Erase the selected range and park the caret at the left edge. No-op
// when nothing is selected. Used by every editing op (typing, paste,
// backspace, delete) so the "type replaces selection" behavior lives
// in exactly one place.
static void DeleteSeedSelection() {
    if (!SeedHasSelection()) return;
    int lo = SeedSelLo(), hi = SeedSelHi();
    g_modSettings.seedArg.erase(lo, hi - lo);
    g_seedCaretPos = lo;
    g_seedSelStart = -1;
}

// Convert a logical X coordinate inside the seed input to a caret index
// (0 .. seedArg.size()). Uses GDI+ MeasureString with the same font,
// StringFormat, and ScaleTransform the paint code applies, so the
// measurement units line up exactly with the on-screen caret positions
// — this is why the function takes inTextX/inTextY directly rather than
// recomputing them: callers already have those from BodySeedInputRect.
//
// Bisects each character by its midpoint: a click on the LEFT half of
// character N lands the caret to N's left (index N), a click on the
// right half lands it to N's right (index N+1). Same gesture model as
// the Win32 EDIT control.
//
// Scale-aware: the launcher's Graphics applies ScaleTransform(g_scale)
// during paint and constructs fonts at SF(em) = em * g_scale physical
// pixels. Mirroring that setup here means MeasureString returns widths
// in LOGICAL units (one logical unit per logical-pixel of the paint),
// which is what we want to compare against the logical xLogical input.
static int SeedXToCaretIndex(int xLogical, REAL inTextX, REAL inTextY) {
    const wstring& s = g_modSettings.seedArg;
    if (s.empty()) return 0;
    if ((REAL)xLogical <= inTextX) return 0;

    HDC hdc = GetDC(g_hwMain);
    if (!hdc) return (int)s.size();
    Graphics g(hdc);
    g.ScaleTransform((REAL)g_scale, (REAL)g_scale);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    StringFormat sf;
    sf.SetAlignment(StringAlignmentNear);
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsNoWrap);

    REAL prevX = inTextX;
    int sz = (int)s.size();
    int result = sz;
    for (int p = 1; p <= sz; ++p) {
        wstring prefix = s.substr(0, p);
        RectF mr;
        g.MeasureString(prefix.c_str(), -1, g_fBtn,
                        PointF(inTextX, inTextY), &sf, &mr);
        REAL nextX = inTextX + mr.Width;
        REAL midX  = (prevX + nextX) * 0.5f;
        if ((REAL)xLogical < midX) { result = p - 1; break; }
        prevX = nextX;
    }
    ReleaseDC(g_hwMain, hdc);
    return result;
}

// ── Custom title bar geometry ────────────────────────────────────────────
//
// Two clickable buttons painted over the top filigree of frame_main.png:
//   button index 0 = minimize, 1 = close.
// The drag region is the rest of the top filigree band.
//
// Button images are 43×42 native (btn_minimize.png + btn_close.png).
// We give them generous breathing room from the frame corner.

// Custom title-bar button geometry. The button images (43×42) are taller
// than the frame's top filigree band (~30px), so they can't fit entirely
// inside the filigree. They sit just below the filigree band's bottom,
// floating in the window's interior with a small gap.
constexpr int TB_BTN_W       = 43;
constexpr int TB_BTN_H       = 42;
constexpr int TB_BTN_GAP     = 14;     // between minimize and close
constexpr int TB_BTN_INSET_R = 58;     // from right edge of the frame
constexpr int TB_BTN_INSET_T = 30;     // tucked just under the top filigree (no hover-grow headroom)

// Returns the screen-relative rect of one title-bar button. idx = 0 (min) or 1 (close).
static RECT TBButtonRect(HWND hw, int idx) {
    // Returns the button rect in LOGICAL coords. GetClientRectL converts
    // the physical client width to logical via U(), so subtracting the
    // logical TB_BTN_INSET_R below gives a coord system that matches how
    // PaintBody (which has ScaleTransform applied) draws the button and
    // how the converted mouse handlers (WM_LBUTTONDOWN/UP, WM_NCHITTEST,
    // WM_MOUSEMOVE) test against it.
    RECT rc; GetClientRectL(hw, &rc);
    int W = rc.right;
    // Close is rightmost, Minimize to its left.
    int rightOfClose = W - TB_BTN_INSET_R;
    int closeX = rightOfClose - TB_BTN_W;
    int minX   = closeX - TB_BTN_GAP - TB_BTN_W;
    int y      = TB_BTN_INSET_T;
    int x = (idx == 0) ? minX : closeX;
    return RECT{ x, y, x + TB_BTN_W, y + TB_BTN_H };
}

// Hit-test a client-coords point against the two buttons; returns -1, 0, or 1.
static int TBHitTest(HWND hw, int x, int y) {
    for (int i = 0; i < 2; ++i) {
        RECT r = TBButtonRect(hw, i);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
            return i;
    }
    return -1;
}

// The drag region: the top filigree band of frame_main.png, minus the
// two buttons. We return HTCAPTION over this band so Windows handles the
// actual drag (including snap-to-edge). Anywhere else returns HTCLIENT.
//
// The band height matches the frame's top inset (measured from the asset)
// so dragging only works where the filigree actually is.
static bool TBPointInDragBand(HWND hw, int x, int y) {
    FrameInset fi = MeasureFrameInset(L"frame_main.png");
    if (y < 0 || y >= fi.top) return false;
    // Exclude the button hit-rects (with a small pad around each)
    for (int i = 0; i < 2; ++i) {
        RECT r = TBButtonRect(hw, i);
        if (x >= r.left - 4 && x < r.right + 4
            && y >= r.top - 4 && y < r.bottom + 4) return false;
    }
    return true;
}

// Show/hide the per-mod link buttons in the Mod Description panel based on
// which fields the selected mod's modinfo.json populates.
static void RefreshModDescriptionLinks() {
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

static void Layout(int W, int H) {
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
        HWND nav[6] = {
            g_hwNavMods, g_hwNavOptions, g_hwNavLogs,
            g_hwNavHelp, g_hwNavAbout,   g_hwNavExit,
        };
        for (int i = 0; i < 6; ++i) {
            if (nav[i])
                SPosL(nav[i], nullptr,
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
    //   DMG Display dropdown
    // The DMG Display bottom stays where the original Loader Dir bar's
    // bottom was, so the section grows upward as items are added.
    {
        constexpr int ELLIPSE_W = 37;     // aspect-correct for 36 tall
        constexpr int ELLIPSE_H = 36;
        constexpr int BAR_H     = 44;   // path bar height (taller so wrapped paths breathe)
        constexpr int LOADER_X_NUDGE = 28;   // 10 px right of the centered-in-rail position
                                              // (was 18; user preferred a slightly-right-of-center placement)
        constexpr int ROW_H_DD  = 28;     // dropdown row height
        constexpr int ROW_GAP   = 6;      // gap between dropdown rows
        constexpr int BAR_GAP   = 8;      // gap between path bar and Stash
        // dirW is constrained by the 300-px bg_loader_options.png frame
        // (which has ~12 px of bronze border on each side). The path bar
        // plus 6-px gap plus 37-px ellipse must fit inside the bronze
        // (bg right - 12). Anchoring dirW to navW broke this once when
        // LOADER_X_NUDGE shifted from 36 to 18 — the path bar grew with
        // navW and the ellipse popped out of the frame. Use a fixed
        // value that keeps the ellipse safely inside the bronze.
        int navW = LEFT_RAIL_W - COL_PAD * 2 - LOADER_X_NUDGE;
        int dirW = 233;
        int loaderX = insetL + COL_PAD + LOADER_X_NUDGE;

        // Anchor: DMG Display bottom drives the whole block (header, path
        // bar, ellipse, both dropdowns, and bg_loader_options.png all derive
        // from it via g_loaderDirRect). Shift this anchor and the entire
        // section moves with it. Currently bodyBot − 8 − 6 − 25: the −25
        // lifts the block 25 px above its original baseline.
        int sectionBot = bodyBot - 8 - 6 - 25;
        int dmgBot     = sectionBot;
        int dmgTop     = dmgBot - ROW_H_DD;
        int stashTop   = dmgTop - ROW_GAP - ROW_H_DD;
        int stashBot   = stashTop + ROW_H_DD;
        int barBot     = stashTop - BAR_GAP;
        int barTop     = barBot - BAR_H;

        // Loader Dir path bar
        g_loaderDirRect = { loaderX, barTop, loaderX + dirW, barBot };

        // Ellipse button vertically centered on the path bar (path bar is
        // 36 tall, ellipse art renders at 36 — they align top/bottom). No
        // overflow pad: the HWND is sized exactly to the visible art, so
        // visual tightness wins out over hover-scale headroom. Hover scale
        // (1.08) may clip a pixel or two at the edges — acceptable.
        int ellipseY = barTop + (BAR_H - ELLIPSE_H) / 2;
        if (g_hwLoaderDirBtn)
            SPosL(g_hwLoaderDirBtn, nullptr,
                         loaderX + dirW + 6, ellipseY,
                         ELLIPSE_W, ELLIPSE_H, SWP_NOZORDER);

        // Publish the dropdown rects via globals so ComputeLoaderOptRects
        // can return them without re-deriving positions.
        g_stashDropdownRect = { loaderX, stashTop, loaderX + navW, stashBot };
        g_dmgDropdownRect   = { loaderX, dmgTop,   loaderX + navW, dmgBot   };
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
    if (g_hwExpandToggle) {
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
static void RepositionForExpansion() {
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

static void RefreshMods() {
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

// Shared themed button frame — gold border, dark bg, optional highlight.
// Used by the refresh button (and by the bottom panel buttons in commit 6).
static void OPDrawBtnFrame(Graphics& g, int x, int y, int w, int h, bool hot) {
    SolidBrush bg(Tok::BgPanel);
    g.FillRectangle(&bg, x + 1, y + 1, w - 2, h - 2);
    Pen border(hot ? Tok::GoldBright : Tok::Bronze, 1.0f);
    g.DrawRectangle(&border, x, y, w - 1, h - 1);
}

// ═══════════════════════════════════════════════════════════════════════
//  MOD LIST
// ═══════════════════════════════════════════════════════════════════════
//
//  Plain bordered rectangle rows, 96px tall, banner image as background
//  when modinfo.json provides one. When no banner is provided we render
//  a clean text fallback: mod Name top, and "by Author" + version on a
//  second line (version left-anchored at the row's horizontal midpoint).
//
//  Selection: brighter gold border + subtle gold tint overlay. The gold
//  ↑ badge appears in the top-right corner when an update is available.
//
//  Notifications sent to the parent (g_hwMain):
//    ML_REFRESH         — recompute layout + repaint
//    ML_NOTIFY_SELECT   — wp = new selected index

constexpr UINT ML_REFRESH       = WM_USER + 1;
constexpr UINT ML_NOTIFY_SELECT = WM_USER + 2;

namespace ML {
    constexpr int ROW_GAP    = 6;            // vertical gap between rows
    constexpr int ROW_PAD    = 12;           // horizontal padding inside row
    constexpr int BADGE_SIZE = 22;           // gold ↑ update badge

    // ── Custom scrollbar ─────────────────────────────────────────────────
    // Right-edge gutter holding scrollbar_track.png (30 wide), scroll_up /
    // scroll_down arrow caps, and the scroll.png thumb (15 wide, centered).
    constexpr int SB_W         = 30;   // gutter / track / arrow width (asset native)
    constexpr int SB_PAD_R     = 4;    // gap from the list's right edge
    constexpr int SB_ROW_GAP   = 8;    // gap between rows and the scrollbar gutter
    constexpr int SB_THUMB_W   = 15;   // thumb asset native width
    constexpr int SB_MIN_THUMB = 40;   // smallest the proportional thumb shrinks to
    constexpr int SB_THUMB_CAP = 16;   // vertical 3-slice cap so the grip ends stay sharp
    constexpr int SB_UP_H_FB   = 35;   // arrow-cap height fallbacks (asset-measured at runtime)
    constexpr int SB_DOWN_H_FB = 32;
}

enum class SbPart { None, Up, Down, Thumb, Track };

struct MLState {
    int    scrollY    = 0;       // pixel scroll offset
    int    totalH     = 0;       // total content height
    int    hoverRow   = -1;
    SbPart sbHover    = SbPart::None;  // scrollbar element under cursor
    SbPart sbPressed  = SbPart::None;  // element pressed (arrows give visual feedback)
    bool   sbDragging = false;   // thumb being dragged
    int    sbGrabDY   = 0;       // mouse-Y minus thumb-top at drag start
};
static map<HWND, MLState*> g_mlStates;
static MLState* GetMLState(HWND hw) {
    auto it = g_mlStates.find(hw);
    return it == g_mlStates.end() ? nullptr : it->second;
}

static int MLRowTop(int rowIndex) {
    return rowIndex * (LO::ROW_H + ML::ROW_GAP);
}

static int MLRowAt(MLState* st, int clientY, int /*viewportH*/) {
    int surfY = clientY + st->scrollY;
    if (surfY < 0) return -1;
    int row = surfY / (LO::ROW_H + ML::ROW_GAP);
    if (row < 0 || row >= (int)g_mods.size()) return -1;
    int rowTop = MLRowTop(row);
    if (surfY >= rowTop + LO::ROW_H) return -1;     // in the gap
    return row;
}

static void MLClampScroll(MLState* st, int viewportH) {
    int maxScroll = max(0, st->totalH - viewportH);
    if (st->scrollY < 0) st->scrollY = 0;
    if (st->scrollY > maxScroll) st->scrollY = maxScroll;
}

// Returns one row's rect in the mod-list child's CLIENT space (i.e. the
// coordinates InvalidateRect expects when given g_hwList). Used by the
// selection-change handler to invalidate only the rows that actually
// changed visually, instead of repainting the whole list.
//
// Expanded by 4px on all sides to cover the selection halo MLPaintRow
// draws when a row is selected — otherwise the halo from a previously-
// selected row would linger when selection moves.
static bool MLRowClientRect(HWND hwList, int rowIndex, RECT* out) {
    if (!hwList || rowIndex < 0 || rowIndex >= (int)g_mods.size()) return false;
    MLState* st = GetMLState(hwList);
    if (!st) return false;
    // Returns the row's rect in LOGICAL coords. Win32's GetClientRect
    // gives physical pixels; convert to logical so this rect can be
    // compared against MLRowTop()/LO::ROW_H (logical) and fed to
    // InvalidateRectL() at the call site.
    RECT rc; GetClientRectL(hwList, &rc);
    int W         = (int)rc.right;
    int viewportH = (int)rc.bottom;
    int top = MLRowTop(rowIndex) - st->scrollY;
    int bot = top + LO::ROW_H;
    if (bot <= 0 || top >= viewportH) return false;
    const int halo = 4;
    out->left   = max(0, ML::ROW_GAP - halo);
    out->top    = max(0, top - halo);
    out->right  = min(W,         W - ML::ROW_GAP + halo);
    out->bottom = min(viewportH, bot + halo);
    return true;
}

// Load a banner image (cached). Returns nullptr if missing/invalid.
static Gdiplus::Bitmap* MLGetBanner(const wstring& path) {
    if (path.empty()) return nullptr;
    if (g_bannerCache && g_bannerCacheKey == path) return g_bannerCache;
    delete g_bannerCache;
    g_bannerCache = nullptr;
    g_bannerCacheKey.clear();
    if (GetFileAttributes(path.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
    Gdiplus::Bitmap* b = new Gdiplus::Bitmap(path.c_str());
    if (!b || b->GetLastStatus() != Ok) { delete b; return nullptr; }
    g_bannerCache    = b;
    g_bannerCacheKey = path;
    return b;
}

// Paint one row at (rx, ry, rw, LO::ROW_H) in client space.
static void MLPaintRow(Graphics& g, const ModInfo& mod, int rx, int ry, int rw,
                       bool selected, bool hover, const UpdateInfo* upd) {
    using namespace LO;
    const int rh = ROW_H;

    // ── Selection halo (drawn FIRST, behind everything) ─────────────────
    // 4px outer gold rectangle, alpha-blended, gives the selected row a
    // soft "lit" feel without changing the frame asset itself.
    if (selected) {
        const int halo = 4;
        SolidBrush haloBr(GPA(70, 0xE8, 0xC2, 0x5E));
        g.FillRectangle(&haloBr, rx - halo, ry - halo,
                        rw + halo * 2, rh + halo * 2);
    }

    // ── Banner / text content (the row's interior) ──────────────────────
    // Inset slightly so the gold frame border (drawn last) doesn't overlap
    // the artwork. The frame asset is 565×124 with ~3-4px border inside,
    // so an inset of 4 keeps banners cleanly inside the frame.
    constexpr int FRAME_INSET = 4;
    int ix = rx + FRAME_INSET;
    int iy = ry + FRAME_INSET;
    int iw = rw - FRAME_INSET * 2;
    int ih = rh - FRAME_INSET * 2;

    // Banner backdrop. With a banner, we fill near-black underneath so any
    // transparent edges in the image look right. Without a banner, leave the
    // interior transparent so the parent panel backdrop shows through and
    // only the text + frame_modbanner border read — the row becomes a clean
    // text-only slot instead of a hard dark box.
    Gdiplus::Bitmap* banner = MLGetBanner(mod.bannerPath);
    if (banner) {
        SolidBrush bgBr(Tok::BgDeep);
        g.FillRectangle(&bgBr, ix, iy, iw, ih);
        // "object-fit: cover": scale to fill the interior, cropping any
        // overflow. Wide banners crop horizontally; tall banners crop top/bottom.
        UINT bw = banner->GetWidth(), bh = banner->GetHeight();
        if (bw > 0 && bh > 0) {
            float rowRatio = (float)iw / (float)ih;
            float imgRatio = (float)bw / (float)bh;
            int sx, sy, sw, sh;
            if (imgRatio > rowRatio) {
                sh = bh; sw = (int)(bh * rowRatio);
                sx = (bw - sw) / 2; sy = 0;
            } else {
                sw = bw; sh = (int)(bw / rowRatio);
                sx = 0; sy = (bh - sh) / 2;
            }
            g.DrawImage(banner,
                        RectF((REAL)ix, (REAL)iy, (REAL)iw, (REAL)ih),
                        (REAL)sx, (REAL)sy, (REAL)sw, (REAL)sh,
                        UnitPixel);
        }
    } else {
        // Text fallback: Mod Name top, "by Author" + "v X.Y.Z" second line.
        StringFormat sfLeft;
        sfLeft.SetAlignment(StringAlignmentNear);
        sfLeft.SetLineAlignment(StringAlignmentNear);
        // Title in Tok::Gold (antique gold ~0xC8/0xA8/0x4B) rather than
        // Tok::GoldBright (pure 0xFF/0xD7/0x00). GoldBright was reading
        // too saturated and too bright when used as a constant body color
        // — it's fine for selection highlights and hover accents where
        // the brightness is the affordance, but as a steady title color
        // it dominated the row. Gold drops saturation from ~100% to
        // ~53% and brightness from 100% to ~78%, still reads as warm
        // gold against the stone background and still picks up the
        // user's selected accent (Tok::Gold tracks the Colour dropdown
        // via ApplyColorChange).
        SolidBrush titleBr(Tok::Gold);
        SolidBrush dimBr(Tok::TextParchment);

        const wchar_t* title = !mod.title.empty() ? mod.title.c_str()
                                                  : mod.folder.c_str();
        // Title rect height 50 (was 40) — SF(38) Exocet has a line-
        // height/em ratio around 1.25, which at g_scale 1.275–1.9125
        // means the rendered line height exceeds a 40-tall rect and
        // the descenders clip. 50 gives 12 logical px of headroom over
        // SF(38)'s em, comfortably absorbing the line gap at all scales.
        // (Author/version rect below shifts down 4 logical to absorb.)
        g.DrawString(title, -1, g_fHeroName ? g_fHeroName : g_fBtn,
                     RectF((REAL)(ix + ML::ROW_PAD - FRAME_INSET),
                           (REAL)(iy + 8),
                           (REAL)(iw - ML::ROW_PAD * 2 + FRAME_INSET * 2),
                           50.0f),
                     &sfLeft, &titleBr);

        wstring authorLine = mod.author.empty() ? L"" : (L"by " + mod.author);
        // Author/version rect lowered by 4 logical px to clear the
        // title's new 50-tall rect (title now ends at iy + 58).
        g.DrawString(authorLine.c_str(), -1, g_fBtn,
                     RectF((REAL)(ix + ML::ROW_PAD - FRAME_INSET),
                           (REAL)(iy + ih - 26),
                           (REAL)(iw / 2 - ML::ROW_PAD + FRAME_INSET),
                           20.0f),
                     &sfLeft, &dimBr);
        if (!mod.version.empty()) {
            wstring versionLine = L"v " + mod.version;
            g.DrawString(versionLine.c_str(), -1, g_fBtn,
                         RectF((REAL)(ix + iw / 2),
                               (REAL)(iy + ih - 26),
                               (REAL)(iw / 2 - ML::ROW_PAD + FRAME_INSET),
                               20.0f),
                         &sfLeft, &dimBr);
        }
    }

    // ── State tints (gold overlay inside the interior) ──────────────────
    // Drawn on top of the banner art so the row reads as "warmed up".
    if (selected) {
        SolidBrush tint(GPA(50, 0xE8, 0xC2, 0x5E));
        g.FillRectangle(&tint, ix, iy, iw, ih);
    } else if (hover) {
        SolidBrush tint(GPA(25, 0xE8, 0xC2, 0x5E));
        g.FillRectangle(&tint, ix, iy, iw, ih);
    }

    // ── Frame overlay (the gold border) ─────────────────────────────────
    // Stretched to the row's actual dimensions. If the asset is missing
    // we fall back to a programmatic bronze rectangle so the row is still
    // visible.
    Gdiplus::Bitmap* frame = AssetImage(L"frame_modbanner.png");
    if (frame) {
        InterpolationMode prev = g.GetInterpolationMode();
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(frame, rx, ry, rw, rh);
        g.SetInterpolationMode(prev);
    } else {
        Pen border(selected ? Tok::GoldBright
                            : (hover ? Tok::Gold : Tok::Bronze),
                   selected ? 2.0f : 1.0f);
        g.DrawRectangle(&border, rx, ry, rw - 1, rh - 1);
    }

    // ── Update badge (gold ↑ in the top-right corner) ───────────────────
    if (upd && upd->available) {
        int bx = rx + rw - ML::BADGE_SIZE - 6;
        int by = ry + 6;
        SolidBrush badgeBg(Tok::GoldBright);
        g.FillEllipse(&badgeBg, bx, by, ML::BADGE_SIZE, ML::BADGE_SIZE);
        Pen badgeBorder(Tok::Gold, 1.5f);
        g.DrawEllipse(&badgeBorder, bx, by, ML::BADGE_SIZE - 1, ML::BADGE_SIZE - 1);
        StringFormat sfC;
        sfC.SetAlignment(StringAlignmentCenter);
        sfC.SetLineAlignment(StringAlignmentCenter);
        SolidBrush dark(Tok::BgDeep);
        g.DrawString(L"\u2191", -1, g_fBtn,
                     RectF((REAL)bx, (REAL)by,
                           (REAL)ML::BADGE_SIZE, (REAL)ML::BADGE_SIZE),
                     &sfC, &dark);
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  CUSTOM SCROLLBAR — geometry, thumb draw, paint
// ═══════════════════════════════════════════════════════════════════════
//  All scrollbar pixels live in a right-edge gutter inside the list window,
//  so a single window proc owns both row and scrollbar input. Geometry is
//  derived purely from the client rect + content height, so the paint pass
//  and the mouse handlers agree without shared mutable state.

struct SbGeom {
    bool present    = false;  // gutter is drawn (always, when there are mods)
    bool scrollable = false;  // content overflows → thumb can move
    RECT area  = {};          // full gutter
    RECT up    = {};          // up-arrow cap
    RECT down  = {};          // down-arrow cap
    RECT track = {};          // groove between the arrows
    RECT thumb = {};          // grip
    int  maxScroll = 0;       // == max(0, totalH - viewportH)
    int  trackTop  = 0, trackH = 0;
};

// Compute every scrollbar rect from the live client rect and mod count.
// `scrollY` is read (and treated as already-clamped by the caller) only to
// position the thumb.
static SbGeom MLScrollbarGeom(MLState* st, const RECT& rc) {
    using namespace ML;
    SbGeom s;
    int W = rc.right, H = rc.bottom;
    int n = (int)g_mods.size();
    if (n <= 0 || W <= 0 || H <= 0) return s;

    int totalH = n * LO::ROW_H + (n - 1) * ROW_GAP;
    s.maxScroll = max(0, totalH - H);

    int upH   = SB_UP_H_FB,   downH = SB_DOWN_H_FB;
    if (Gdiplus::Bitmap* a = AssetImage(L"scroll_up.png"))   upH   = (int)a->GetHeight();
    if (Gdiplus::Bitmap* a = AssetImage(L"scroll_down.png")) downH = (int)a->GetHeight();

    s.present = true;
    s.area.right  = W - SB_PAD_R;
    s.area.left   = s.area.right - SB_W;
    s.area.top    = 0;
    s.area.bottom = H;

    s.up   = { s.area.left, 0,         s.area.right, upH };
    s.down = { s.area.left, H - downH, s.area.right, H };

    s.trackTop = upH;
    int trackBot = H - downH;
    s.trackH = max(0, trackBot - s.trackTop);
    s.track  = { s.area.left, s.trackTop, s.area.right, trackBot };

    // Thumb: proportional height with a floor, positioned by scroll fraction.
    s.scrollable = (s.maxScroll > 0) && (s.trackH > SB_MIN_THUMB);
    int thumbH;
    if (!s.scrollable) {
        thumbH = s.trackH;                       // fills the track when nothing to scroll
    } else {
        thumbH = (int)((long long)s.trackH * H / totalH);
        thumbH = max(SB_MIN_THUMB, min(thumbH, s.trackH));
    }
    int thumbTop = s.trackTop;
    if (s.scrollable) {
        int travel = s.trackH - thumbH;
        thumbTop = s.trackTop + (int)((long long)st->scrollY * travel / s.maxScroll);
        thumbTop = max(s.trackTop, min(thumbTop, s.trackTop + travel));
    }
    int thumbX = s.area.left + (SB_W - SB_THUMB_W) / 2;
    s.thumb = { thumbX, thumbTop, thumbX + SB_THUMB_W, thumbTop + thumbH };
    return s;
}

// Vertical 3-slice for the thumb: keep `cap` px of art at top and bottom,
// stretch the middle. Drawn at the asset's native width (no horizontal
// distortion). Falls back to a plain stretch if the thumb is shorter than
// the two caps combined.
static void MLDrawThumb(Graphics& g, Gdiplus::Bitmap* b,
                        int x, int y, int w, int h, int cap) {
    if (!b) return;
    int sw = (int)b->GetWidth(), sh = (int)b->GetHeight();
    if (h >= sh && h > cap * 2 && sh > cap * 2) {
        g.DrawImage(b, Rect(x, y, w, cap), 0, 0, sw, cap, UnitPixel);
        g.DrawImage(b, Rect(x, y + cap, w, h - cap * 2),
                    0, cap, sw, sh - cap * 2, UnitPixel);
        g.DrawImage(b, Rect(x, y + h - cap, w, cap), 0, sh - cap, sw, cap, UnitPixel);
    } else {
        g.DrawImage(b, Rect(x, y, w, h), 0, 0, sw, sh, UnitPixel);
    }
}

// Paint the gutter: stretched track, arrow caps, thumb, with a subtle
// translucent highlight on the hovered / pressed element (single-asset
// pattern — no per-state art).
static void MLPaintScrollbar(Graphics& g, MLState* st, const RECT& rc) {
    SbGeom s = MLScrollbarGeom(st, rc);
    if (!s.present) return;

    auto W = [](const RECT& r) -> int { return (int)(r.right - r.left); };
    auto H = [](const RECT& r) -> int { return (int)(r.bottom - r.top); };

    // Track groove (stretched vertically across the whole gutter).
    if (Gdiplus::Bitmap* tk = AssetImage(L"scrollbar_track.png")) {
        g.DrawImage(tk, Rect((INT)s.area.left, (INT)s.area.top, W(s.area), H(s.area)),
                    0, 0, (INT)tk->GetWidth(), (INT)tk->GetHeight(), UnitPixel);
    } else {
        SolidBrush groove(GPA(150, 0x10, 0x0A, 0x06));
        g.FillRectangle(&groove, (INT)s.area.left, (INT)s.area.top, W(s.area), H(s.area));
        Pen edge(Tok::BronzeDim, 1.0f);
        g.DrawRectangle(&edge, (INT)s.area.left, (INT)s.area.top, W(s.area) - 1, H(s.area) - 1);
    }

    // Thumb (only meaningful when there's something to scroll, but we draw
    // it always — full-track when not scrollable — to match the mockup).
    if (Gdiplus::Bitmap* th = AssetImage(L"scroll.png")) {
        MLDrawThumb(g, th, (INT)s.thumb.left, (INT)s.thumb.top,
                    W(s.thumb), H(s.thumb), ML::SB_THUMB_CAP);
    } else {
        LinearGradientBrush grip(
            RectF((REAL)s.thumb.left, (REAL)s.thumb.top,
                  (REAL)W(s.thumb), (REAL)max(1, H(s.thumb))),
            Tok::BronzeBright, Tok::BronzeDim, LinearGradientModeHorizontal);
        g.FillRectangle(&grip, (INT)s.thumb.left, (INT)s.thumb.top, W(s.thumb), H(s.thumb));
        Pen rim(Tok::Gold, 1.0f);
        g.DrawRectangle(&rim, (INT)s.thumb.left, (INT)s.thumb.top, W(s.thumb) - 1, H(s.thumb) - 1);
    }

    // Arrow caps.
    if (Gdiplus::Bitmap* up = AssetImage(L"scroll_up.png"))
        g.DrawImage(up, (INT)s.up.left, (INT)s.up.top, (INT)up->GetWidth(), (INT)up->GetHeight());
    if (Gdiplus::Bitmap* dn = AssetImage(L"scroll_down.png"))
        g.DrawImage(dn, (INT)s.down.left, (INT)s.down.top, (INT)dn->GetWidth(), (INT)dn->GetHeight());

    // Hover / press highlight.
    auto glow = [&](const RECT& r, int alpha) {
        SolidBrush hi(GPA(alpha, 0xFF, 0xE0, 0x90));
        g.FillRectangle(&hi, (INT)r.left, (INT)r.top, W(r), H(r));
    };
    if (st->sbPressed == SbPart::Up   || st->sbHover == SbPart::Up)
        glow(s.up,   st->sbPressed == SbPart::Up   ? 22 : 36);
    if (st->sbPressed == SbPart::Down || st->sbHover == SbPart::Down)
        glow(s.down, st->sbPressed == SbPart::Down ? 22 : 36);
    if (st->sbDragging || st->sbHover == SbPart::Thumb)
        glow(s.thumb, st->sbDragging ? 30 : 24);
}

static void MLPaint(HWND hw, HDC hdc, MLState* st, const RECT& dirty) {
    RECT rc; GetClientRect(hw, &rc);
    // Convert the physical client size to LOGICAL pixels so all paint code
    // below (row positions, scrollbar geometry, hover/selection rects) can
    // be written in the same logical units as Layout/LO::* constants. The
    // Graphics gets a ScaleTransform that maps logical → physical at output.
    int W = U(rc.right), H = U(rc.bottom);

    // Size the back buffer to the dirty rect only — much cheaper than
    // allocating one big enough for the entire list when a single row
    // changed (e.g. on hover or selection). The dirty rect stays in
    // PHYSICAL pixels because the MemDC bitmap is at physical resolution;
    // GDI+'s ScaleTransform composes with the MemDC's device-origin offset
    // so logical paint coords still land on the right physical region.
    int dx = dirty.left, dy = dirty.top;
    int dw = dirty.right  - dirty.left;
    int dh = dirty.bottom - dirty.top;
    if (dw <= 0 || dh <= 0) return;

    MemDC m(hdc, dx, dy, dw, dh);
    Graphics g(m.dc);
    g.ScaleTransform((REAL)g_scale, (REAL)g_scale);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // Backdrop: paint the parent's stone showing through. We composite
    // the stone texture in our own coords (relative to our HWND), offset
    // by our position within the parent. This makes the list visually
    // "sit on" the stone instead of having an opaque black rectangle.
    //
    // Two coordinate systems are in play: MapWindowPoints and the parent's
    // client rect return PHYSICAL pixels (Win32 doesn't know about
    // g_scale), but the destination of DrawImage is now LOGICAL (the
    // Graphics has a ScaleTransform). The source-coordinate fraction
    // (dx + origin.x) / pc.right is scale-invariant — it's the same ratio
    // in physical and logical units — so we can leave that math alone and
    // only convert the destination rect.
    int lDx = U(dx), lDy = U(dy), lDw = U(dw), lDh = U(dh);
    POINT origin = { 0, 0 };
    MapWindowPoints(hw, GetParent(hw), &origin, 1);
    if (Gdiplus::Bitmap* bgStone = AssetImage(L"bg_stone.png")) {
        RECT pc; GetClientRect(GetParent(hw), &pc);
        // Paint the parent's stone region that falls under our window —
        // we shift the image by our negative position so its pixels line
        // up with the parent's.
        g.DrawImage(bgStone,
            Rect(lDx, lDy, lDw, lDh),
            (REAL)(dx + origin.x) * (REAL)bgStone->GetWidth()  / (REAL)pc.right,
            (REAL)(dy + origin.y) * (REAL)bgStone->GetHeight() / (REAL)pc.bottom,
            (REAL)dw * (REAL)bgStone->GetWidth()  / (REAL)pc.right,
            (REAL)dh * (REAL)bgStone->GetHeight() / (REAL)pc.bottom,
            UnitPixel);
    } else {
        SolidBrush bgBr(Tok::BgDeep);
        g.FillRectangle(&bgBr, lDx, lDy, lDw, lDh);
    }

    if (g_mods.empty()) {
        StringFormat sfC;
        sfC.SetAlignment(StringAlignmentCenter);
        sfC.SetLineAlignment(StringAlignmentCenter);
        SolidBrush dimBr(Tok::TextDim);
        g.DrawString(L"No mods found.", -1, g_fBtn,
                     RectF(0, 0, (REAL)W, (REAL)H), &sfC, &dimBr);
        return;
    }

    // Update content height + clamp scroll
    int n = (int)g_mods.size();
    st->totalH = n * LO::ROW_H + (n - 1) * ML::ROW_GAP;
    MLClampScroll(st, H);

    // Compute visible row range
    int firstVis = max(0, st->scrollY / (LO::ROW_H + ML::ROW_GAP));
    int lastVis  = min(n - 1,
                       (st->scrollY + H) / (LO::ROW_H + ML::ROW_GAP));

    // Dirty rect comes from BeginPaint in PHYSICAL pixels; convert to
    // LOGICAL here so the per-row skip check below compares apples to
    // apples against the logical `ry` and `LO::ROW_H`. Without this,
    // any partial invalidate (hover transition, selection change,
    // scrollbar-area repaint) at scales where S(ry+ROW_H) > dirty.bottom
    // physical erases the bottom row's pixels but skips its repaint —
    // and the threshold for that is exactly g_scale ≈ 1.25, which is
    // why 70% (g_scale=1.05) worked and 85% (g_scale=1.275) broke.
    int ldTop = U(dirty.top);
    int ldBot = U(dirty.bottom);

    for (int i = firstVis; i <= lastVis; ++i) {
        int rx = ML::ROW_GAP;
        int ry = MLRowTop(i) - st->scrollY;
        // Reserve the right-edge gutter for the custom scrollbar.
        int rw = (W - ML::SB_PAD_R - ML::SB_W - ML::SB_ROW_GAP) - rx;

        // Skip rows that don't intersect the dirty rect (logical vs logical).
        if (ry + LO::ROW_H <= ldTop || ry >= ldBot) continue;

        bool selected = (i == g_selMod);
        bool hover    = (i == st->hoverRow) && !selected;

        const UpdateInfo* upd = nullptr;
        auto it = g_updateInfo.find(g_mods[i].folder);
        if (it != g_updateInfo.end()) upd = &it->second;

        MLPaintRow(g, g_mods[i], rx, ry, rw, selected, hover, upd);
    }

    // Custom scrollbar in the right gutter (drawn last, over the stone).
    // Construct a logical rect so the scrollbar geometry is in the same
    // space as the row paints above (the Graphics has a ScaleTransform).
    RECT lrc = { 0, 0, W, H };
    MLPaintScrollbar(g, st, lrc);
}

static void MLScrollBy(HWND hw, MLState* st, int dy) {
    RECT rc; GetClientRectL(hw, &rc);
    st->scrollY += dy;
    MLClampScroll(st, rc.bottom);
    InvalidateRect(hw, nullptr, FALSE);
}

static LRESULT CALLBACK ModListProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    MLState* st = GetMLState(hw);
    switch (msg) {

    case WM_NCCREATE: {
        st = new MLState;
        g_mlStates[hw] = st;
        return DefWindowProc(hw, msg, wp, lp);
    }

    case WM_NCDESTROY: {
        if (st) { delete st; g_mlStates.erase(hw); }
        return DefWindowProc(hw, msg, wp, lp);
    }

    case WM_ERASEBKGND: return TRUE;

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hw, &ps);
        if (st) MLPaint(hw, hdc, st, ps.rcPaint);
        EndPaint(hw, &ps);
        return 0;
    }

    case ML_REFRESH: {
        if (st) { st->scrollY = 0; st->hoverRow = -1; }
        InvalidateRect(hw, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!st) return 0;
        int x = U(GET_X_LPARAM(lp)), y = U(GET_Y_LPARAM(lp));
        RECT rc; GetClientRectL(hw, &rc);

        // While dragging the thumb, map mouse-Y → scrollY and bail.
        if (st->sbDragging) {
            SbGeom s = MLScrollbarGeom(st, rc);
            int thumbH = s.thumb.bottom - s.thumb.top;
            int travel = s.trackH - thumbH;
            if (travel > 0 && s.maxScroll > 0) {
                int newTop = y - st->sbGrabDY;
                newTop = max(s.trackTop, min(newTop, s.trackTop + travel));
                st->scrollY = (int)((long long)(newTop - s.trackTop) * s.maxScroll / travel);
                MLClampScroll(st, rc.bottom);
                InvalidateRect(hw, nullptr, FALSE);
            }
            return 0;
        }

        // Scrollbar hover.
        SbGeom s = MLScrollbarGeom(st, rc);
        SbPart newSb = SbPart::None;
        if (s.present) {
            POINT p = { x, y };
            if      (PtInRect(&s.thumb, p)) newSb = SbPart::Thumb;
            else if (PtInRect(&s.up,    p)) newSb = SbPart::Up;
            else if (PtInRect(&s.down,  p)) newSb = SbPart::Down;
            else if (PtInRect(&s.track, p)) newSb = SbPart::Track;
        }
        if (newSb != st->sbHover) {
            st->sbHover = newSb;
            InvalidateRectL(hw, &s.area, FALSE);
        }

        // Row hover — suppressed while the cursor is over the gutter.
        int newHover = (newSb == SbPart::None) ? MLRowAt(st, y, rc.bottom) : -1;
        if (newHover != st->hoverRow) {
            int prevHover = st->hoverRow;
            st->hoverRow = newHover;
            RECT r;
            if (prevHover >= 0 && MLRowClientRect(hw, prevHover, &r))
                InvalidateRectL(hw, &r, FALSE);
            if (newHover  >= 0 && MLRowClientRect(hw, newHover,  &r))
                InvalidateRectL(hw, &r, FALSE);
            // Hover transition — anything that was on its way to a
            // tooltip is now stale. Kill the pending timer and hide
            // the tip if it was already showing. If we moved onto a
            // new row, arm a fresh 2 s timer; if we moved off rows
            // entirely (newHover == -1), leave the timer dead.
            KillTimer(hw, IDT_HOVER_TIP);
            HideHoverTip();
            if (newHover >= 0) {
                SetTimer(hw, IDT_HOVER_TIP, 2000, nullptr);
            }
        }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hw, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_TIMER: {
        // The 2-second dwell timer expired. If the cursor is still on
        // a row, show the playtime tooltip for it. The timer is
        // one-shot — we KillTimer here and re-arm on the next hover
        // transition.
        if (wp == IDT_HOVER_TIP) {
            KillTimer(hw, IDT_HOVER_TIP);
            if (st && st->hoverRow >= 0
                && st->hoverRow < (int)g_mods.size()) {
                ShowHoverTip(st->hoverRow);
            }
            return 0;
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        if (!st) return 0;
        // Tear down any pending or visible tooltip — the cursor isn't
        // over the list at all anymore.
        KillTimer(hw, IDT_HOVER_TIP);
        HideHoverTip();
        if (st->hoverRow != -1) {
            int prevHover = st->hoverRow;
            st->hoverRow = -1;
            RECT r;
            if (MLRowClientRect(hw, prevHover, &r))
                InvalidateRectL(hw, &r, FALSE);
        }
        if (st->sbHover != SbPart::None) {
            st->sbHover = SbPart::None;
            RECT rc; GetClientRectL(hw, &rc);
            SbGeom s = MLScrollbarGeom(st, rc);
            if (s.present) InvalidateRectL(hw, &s.area, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        if (!st) return 0;
        // Any click dismisses the hover tooltip — user's done looking.
        KillTimer(hw, IDT_HOVER_TIP);
        HideHoverTip();
        int x = U(GET_X_LPARAM(lp)), y = U(GET_Y_LPARAM(lp));
        RECT rc; GetClientRectL(hw, &rc);

        // Scrollbar gutter takes priority over row selection.
        SbGeom s = MLScrollbarGeom(st, rc);
        if (s.present) {
            POINT p = { x, y };
            if (PtInRect(&s.thumb, p) && s.scrollable) {
                st->sbDragging = true;
                st->sbGrabDY   = y - s.thumb.top;
                SetCapture(hw);
                InvalidateRectL(hw, &s.area, FALSE);
                return 0;
            }
            if (PtInRect(&s.up, p)) {
                st->sbPressed = SbPart::Up;
                MLScrollBy(hw, st, -(LO::ROW_H + ML::ROW_GAP));
                return 0;
            }
            if (PtInRect(&s.down, p)) {
                st->sbPressed = SbPart::Down;
                MLScrollBy(hw, st, +(LO::ROW_H + ML::ROW_GAP));
                return 0;
            }
            if (PtInRect(&s.track, p)) {
                int page = max(LO::ROW_H + ML::ROW_GAP,
                               (int)rc.bottom - (LO::ROW_H + ML::ROW_GAP));
                MLScrollBy(hw, st, (y < s.thumb.top) ? -page : +page);
                return 0;
            }
        }

        int row = MLRowAt(st, y, rc.bottom);
        if (row >= 0 && row != g_selMod)
            SendMessage(GetParent(hw), ML_NOTIFY_SELECT, (WPARAM)row, 0);
        return 0;
    }

    case WM_LBUTTONUP: {
        if (!st) return 0;
        bool wasActive = st->sbDragging || (st->sbPressed != SbPart::None);
        if (st->sbDragging) { st->sbDragging = false; ReleaseCapture(); }
        st->sbPressed = SbPart::None;
        if (wasActive) {
            RECT rc; GetClientRectL(hw, &rc);
            SbGeom s = MLScrollbarGeom(st, rc);
            if (s.present) InvalidateRectL(hw, &s.area, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        // Double-click on a mod row → launch that mod through the same
        // path as the PLAY button. The first click of the pair fires the
        // normal WM_LBUTTONDOWN above and selects the row (if it wasn't
        // already selected); by the time we get here, g_selMod is
        // already pointing at the right mod. We still re-check + fix up
        // selection defensively in case the user double-clicks faster
        // than the LBUTTONDOWN can be processed.
        //
        // Scrollbar gutter excluded — double-clicks there are nonsense
        // for a launch action and would just confuse the user.
        if (!st) return 0;
        // Game is about to launch — tear down any visible tooltip.
        KillTimer(hw, IDT_HOVER_TIP);
        HideHoverTip();
        int x = U(GET_X_LPARAM(lp)), y = U(GET_Y_LPARAM(lp));
        RECT rc; GetClientRectL(hw, &rc);
        SbGeom s = MLScrollbarGeom(st, rc);
        if (s.present) {
            POINT p = { x, y };
            if (PtInRect(&s.thumb, p) || PtInRect(&s.up, p) ||
                PtInRect(&s.down,  p) || PtInRect(&s.track, p)) return 0;
        }
        int row = MLRowAt(st, y, rc.bottom);
        if (row < 0) return 0;

        HWND parent = GetParent(hw);
        if (row != g_selMod) {
            // Defensive — should already be selected by the first
            // click's LBUTTONDOWN, but cover the case where the click
            // pair races faster than message delivery.
            SendMessage(parent, ML_NOTIFY_SELECT, (WPARAM)row, 0);
        }
        // PostMessage rather than SendMessage so this handler returns
        // before the launch path enters its WaitForInputIdle block —
        // keeps the message pump clean for the LBUTTONUP that follows
        // the dblclick.
        PostMessage(parent, WM_COMMAND,
                    MAKEWPARAM(IDC_LAUNCH_BTN, BN_CLICKED), 0);
        return 0;
    }

    case WM_RBUTTONDOWN: {
        // Right-click selects the row before the context menu pops, so
        // the menu actions operate on the row the user just clicked
        // (not whatever was selected before). The actual menu is shown
        // from WM_CONTEXTMENU below — that's the right place to hook
        // it because Windows also fires WM_CONTEXTMENU for Shift+F10
        // and the right-click happens to dispatch through it too.
        if (!st) return 0;
        // Dismiss the hover tooltip — the menu is about to take over.
        KillTimer(hw, IDT_HOVER_TIP);
        HideHoverTip();
        int x = U(GET_X_LPARAM(lp)), y = U(GET_Y_LPARAM(lp));
        RECT rc; GetClientRectL(hw, &rc);
        SbGeom s = MLScrollbarGeom(st, rc);
        if (s.present) {
            POINT p = { x, y };
            if (PtInRect(&s.thumb, p) || PtInRect(&s.up, p) ||
                PtInRect(&s.down,  p) || PtInRect(&s.track, p)) return 0;
        }
        int row = MLRowAt(st, y, rc.bottom);
        if (row >= 0 && row != g_selMod) {
            SendMessage(GetParent(hw), ML_NOTIFY_SELECT, (WPARAM)row, 0);
        }
        return 0;
    }

    case WM_CONTEXTMENU: {
        // Show the mod row context menu. lp carries SCREEN coords for
        // mouse-invoked menus, or (-1, -1) when invoked from the
        // keyboard (Shift+F10). For the keyboard case we anchor at the
        // selected row center so the menu doesn't pop in the corner of
        // the screen.
        if (!st) return 0;
        if (g_selMod < 0 || g_selMod >= (int)g_mods.size()) return 0;

        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (pt.x == -1 && pt.y == -1) {
            RECT cr; GetClientRect(hw, &cr);
            pt.x = (cr.left + cr.right) / 2;
            pt.y = (cr.top  + cr.bottom) / 2;
            ClientToScreen(hw, &pt);
        }

        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING,
                    IDM_MOD_OPEN_FOLDER,  L"Open Folder in Explorer");
        AppendMenuW(menu, MF_STRING,
                    IDM_MOD_BACKUP_SAVES, L"Backup Saves");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING,
                    IDM_MOD_REZIP,        L"Re-zip Mod...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING,
                    IDM_MOD_UNINSTALL,    L"Uninstall...");

        // Route commands to MainProc (parent) so its WM_COMMAND
        // dispatcher handles them alongside other commands.
        TrackPopupMenu(menu,
                       TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                       pt.x, pt.y, 0, GetParent(hw), nullptr);
        DestroyMenu(menu);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (!st) return 0;
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        st->scrollY -= (delta / WHEEL_DELTA) * (LO::ROW_H + ML::ROW_GAP) / 2;
        RECT rc; GetClientRectL(hw, &rc);
        MLClampScroll(st, rc.bottom);
        InvalidateRect(hw, nullptr, FALSE);
        // Scrolling shuffles which row is under the cursor — the
        // tooltip's content would now be stale, and the geometry it
        // was positioned against has moved. Easiest fix: tear it down.
        // The hover transition logic in WM_MOUSEMOVE re-arms a fresh
        // timer if the new row under cursor warrants it.
        KillTimer(hw, IDT_HOVER_TIP);
        HideHoverTip();
        return 0;
    }
    }
    return DefWindowProc(hw, msg, wp, lp);
}

static void RegisterModListClass(HINSTANCE hInst) {
    WNDCLASS wc = {};
    wc.style         = CS_DBLCLKS;   // enable WM_LBUTTONDBLCLK delivery
    wc.lpfnWndProc   = ModListProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"Angiris_ModList";
    RegisterClass(&wc);
}

// ═══════════════════════════════════════════════════════════════════════
//  BODY GEOMETRY + PAINT
// ═══════════════════════════════════════════════════════════════════════
//
//  Right column = MOD DESCRIPTION (top half) + LAUNCH OPTIONS (bottom half).
//  Painted as part of MainProc::WM_PAINT. The flag checkboxes are hit-tested
//  in WM_LBUTTONDOWN against geometry derived from BodyLayout, which is
//  declared earlier (above Layout()) so Layout() can use it to position
//  the right column's Win32 button children.

// Loader Options rows (left rail). Two click-target rows, each painted by
// PaintLeftRail. Returned in client-space.
struct LoaderOptHits {
    RECT stash;       // "Stash Tabs" row
    RECT dmg;         // "Dmg Display" row
};

static LoaderOptHits ComputeLoaderOptRects() {
    // Layout() now publishes the dropdown rects directly via globals;
    // this helper just exposes them in the existing LoaderOptHits shape
    // so hit-testing and PaintLeftRail can keep their current calls.
    LoaderOptHits L = {};
    L.stash = g_stashDropdownRect;
    L.dmg   = g_dmgDropdownRect;
    return L;
}

static void PaintLeftRail(Graphics& g, int /*W*/, int /*H*/) {
    // The D2RLOADER wordmark + "Diablo II: Resurrected / Mod Launcher"
    // subtitle are all baked into logo_d2rloader.png (drawn earlier in
    // PaintBody). No live text needed here for the logo.
    using namespace LO;
    StringFormat sfC;
    sfC.SetAlignment(StringAlignmentCenter);
    sfC.SetLineAlignment(StringAlignmentCenter);
    SolidBrush gold(Tok::Gold);
    SolidBrush dim(Tok::GoldDim);

    // LOADER OPTIONS section sublabel + the two dropdown rows
    LoaderOptHits L = ComputeLoaderOptRects();
    StringFormat sfL;
    sfL.SetAlignment(StringAlignmentNear);
    sfL.SetLineAlignment(StringAlignmentCenter);
    SolidBrush sublbl(Tok::GoldDim);

    // ── Backdrop: bg_loader_options.png ─────────────────────────────────
    // Section stack (top → bottom):
    //   LOADER OPTIONS header
    //   Loader Dir path bar
    //   Stash Tabs dropdown
    //   DMG Display dropdown
    // The backdrop spans from above the header to just below the DMG
    // Display row, with horizontal padding around the content column.
    constexpr int LO_HDR_H    = 40;
    constexpr int LO_HDR_PAD  = 8;     // pad between header and path bar
    constexpr int LO_BG_PADT  = 8;     // breathing room above header
    constexpr int LO_BG_PADB  = 12;    // breathing room below DMG row
    int hdrY  = g_loaderDirRect.top - LO_HDR_PAD - LO_HDR_H;
    int bgX = g_loaderDirRect.left - 12;
    int bgY = hdrY - LO_BG_PADT;
    int bgW = 300;     // wider than the content column, but left X stays anchored
    int bgH = (g_dmgDropdownRect.bottom - hdrY) + LO_BG_PADT + LO_BG_PADB;
    if (Gdiplus::Bitmap* lobg = AssetImage(L"bg_loader_options.png")) {
        InterpolationMode prev = g.GetInterpolationMode();
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(lobg, bgX, bgY, bgW, bgH);
        g.SetInterpolationMode(prev);
    }

    // Header sits above the Loader Dir path bar. NoWrap + a slightly
    // wider rect (the path bar itself is 233 logical; the header was
    // wrapping to two lines at 150% DPI × 100% UI scale because the
    // rendered "LOADER OPTIONS" hits ~250 logical at SF(14) caps).
    StringFormat sfLOHdr;
    sfLOHdr.SetAlignment(StringAlignmentNear);
    sfLOHdr.SetLineAlignment(StringAlignmentCenter);
    sfLOHdr.SetFormatFlags(sfLOHdr.GetFormatFlags() | StringFormatFlagsNoWrap);
    g.DrawString(L"LOADER OPTIONS", -1, g_fColHdrSm,
                 RectF((REAL)(g_loaderDirRect.left + 12), (REAL)hdrY,
                       275.0f,
                       (REAL)LO_HDR_H),
                 &sfLOHdr, &gold);

    // Two dropdown rows: "Stash Tabs  [ N  ▾ ]"  and  "Dmg Display [ N  ▾ ]"
    //
    // The label is inset 10 px from the row's left edge, and the value
    // box is inset 60 px from the row's right edge — both moved inward
    // by 10 px each (the previous layout was 0 / 50), bringing the label
    // and value 20 logical px closer to each other. loValueBox below
    // returns matching coordinates so popup-anchor and paint stay synced.
    auto drawDD = [&](const RECT& r, const wchar_t* label, int value) {
        SolidBrush textBr(Tok::TextParchment);
        SolidBrush valBr(Tok::Gold);

        // Value box on the right (~70 px wide, sitting 60 px in from the
        // row's right edge). The label rect extends from r.left + 10
        // (10 px inset) up to just before the value box, with NoWrap so
        // long labels like "Dmg Display" stay on a single line at any
        // scale instead of breaking into "DMG / DISPLAY".
        constexpr int LABEL_INSET_L = 10;
        constexpr int VALUE_INSET_R = 60;
        int boxW = 70;
        int bx = r.right - boxW - VALUE_INSET_R;
        int by = r.top + 2;
        int bw = boxW;
        int bh = (r.bottom - r.top) - 4;

        StringFormat sfLbl;
        sfLbl.SetAlignment(StringAlignmentNear);
        sfLbl.SetLineAlignment(StringAlignmentCenter);
        sfLbl.SetFormatFlags(sfLbl.GetFormatFlags() | StringFormatFlagsNoWrap);
        int labelL = r.left + LABEL_INSET_L;
        g.DrawString(label, -1, g_fBtn,
                     RectF((REAL)labelL, (REAL)r.top,
                           (REAL)(bx - 6 - labelL),
                           (REAL)(r.bottom - r.top)),
                     &sfLbl, &textBr);

        // text_box.png provides the bronze chrome; dropdown_chevron.png
        // is the chevron glyph. Box shifted 50 px LEFT from the row's
        // right edge.
        if (Gdiplus::Bitmap* tb = AssetImage(L"text_box.png")) {
            InterpolationMode prev = g.GetInterpolationMode();
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.DrawImage(tb, bx, by, bw, bh);
            g.SetInterpolationMode(prev);
        } else {
            OPDrawBtnFrame(g, bx, by, bw, bh, false);
        }

        wchar_t buf[16]; swprintf(buf, 16, L"%d", value);
        StringFormat sfC2;
        sfC2.SetAlignment(StringAlignmentCenter);
        sfC2.SetLineAlignment(StringAlignmentCenter);
        // Leave room on the right for the chevron asset
        g.DrawString(buf, -1, g_fBtn,
                     RectF((REAL)bx, (REAL)by, (REAL)(bw - 22), (REAL)bh),
                     &sfC2, &valBr);

        // Chevron at the right edge
        if (Gdiplus::Bitmap* ch = AssetImage(L"dropdown_chevron.png")) {
            int chW = (int)ch->GetWidth();
            int chH = (int)ch->GetHeight();
            // Fit to half the box height so it doesn't dominate
            int targetH = bh - 4;
            int targetW = chW * targetH / chH;
            int cx = bx + bw - targetW - 4;
            int cy = by + (bh - targetH) / 2;
            InterpolationMode prev = g.GetInterpolationMode();
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.DrawImage(ch, cx, cy, targetW, targetH);
            g.SetInterpolationMode(prev);
        } else {
            // Programmatic fallback: Unicode ▾
            StringFormat sfR;
            sfR.SetAlignment(StringAlignmentCenter);
            sfR.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(L"\u25BE", -1, g_fBtn,
                         RectF((REAL)(bx + bw - 16), (REAL)by, 16.0f, (REAL)bh),
                         &sfR, &valBr);
        }
    };
    drawDD(L.stash, L"Stash Tabs",  g_loaderOpts.extraSharedTabs);
    drawDD(L.dmg,   L"Dmg Display", g_loaderOpts.damageIndicator);

    // ── Loader Dir path bar ─────────────────────────────────────────────
    // Drawn programmatically (no Win32 HWND). text_box.png as backdrop,
    // path text rendered with ellipsis trimming so long paths stay readable.
    {
        const RECT& r = g_loaderDirRect;
        int rw = r.right - r.left;
        int rh = r.bottom - r.top;
        if (Gdiplus::Bitmap* tb = AssetImage(L"text_box.png")) {
            InterpolationMode prev = g.GetInterpolationMode();
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.DrawImage(tb, r.left, r.top, rw, rh);
            g.SetInterpolationMode(prev);
        } else {
            SolidBrush bg(Tok::BgDeep);
            g.FillRectangle(&bg, r.left, r.top, rw, rh);
            Pen border(Tok::Bronze, 1.0f);
            g.DrawRectangle(&border, r.left, r.top, rw - 1, rh - 1);
        }
        StringFormat sfPath;
        sfPath.SetAlignment(StringAlignmentNear);
        sfPath.SetLineAlignment(StringAlignmentCenter);
        sfPath.SetTrimming(StringTrimmingEllipsisPath);
        SolidBrush pathBr(Tok::TextParchment);
        g.DrawString(g_cfg.d2rPath.c_str(), -1, g_fBtn,
                     RectF((REAL)(r.left + 8), (REAL)r.top,
                           (REAL)(rw - 16), (REAL)rh),
                     &sfPath, &pathBr);
    }
}

static void PaintModDescription(Graphics& g, const BodyLayout& B) {
    using namespace LO;
    // No background fill / border drawn here — frame_panel_right.png (painted
    // earlier in PaintBody) supplies the panel chrome. The stone shows
    // through inside the panel where the asset is transparent.

    // Header
    StringFormat sfL;
    sfL.SetAlignment(StringAlignmentNear);
    sfL.SetLineAlignment(StringAlignmentCenter);
    SolidBrush gold(Tok::Gold);
    g.DrawString(L"MOD DESCRIPTION", -1, g_fColHdrMed,
                 RectF((REAL)(B.descX + 12), (REAL)(B.descY + 4),
                       (REAL)(B.descW - 24), 44.0f),
                 &sfL, &gold);
    // Bronze separator under the header
    Pen sep(Tok::Bronze, 1.0f);
    g.DrawLine(&sep,
               B.descX + 12, B.descY + 34,
               B.descX + B.descW - 12, B.descY + 34);

    // Body text — prefer overview, fall back to description
    if (g_selMod >= 0 && g_selMod < (int)g_mods.size()) {
        const ModInfo& mod = g_mods[g_selMod];
        const wstring& body = !mod.overview.empty() ? mod.overview : mod.description;
        if (!body.empty()) {
            StringFormat sfBody;
            sfBody.SetAlignment(StringAlignmentNear);
            sfBody.SetLineAlignment(StringAlignmentNear);
            sfBody.SetTrimming(StringTrimmingWord);
            SolidBrush textBr(Tok::TextParchment);
            g.DrawString(body.c_str(), -1, g_fBtn,
                         RectF((REAL)(B.descX + 12), (REAL)B.descBodyY,
                               (REAL)(B.descW - 24), (REAL)B.descBodyH),
                         &sfBody, &textBr);
        }

        // Update-available bar at the bottom of the panel
        auto it = g_updateInfo.find(mod.folder);
        if (it != g_updateInfo.end() && it->second.available) {
            int bx = B.descX + 8;
            int by = B.descUpdateBarY;
            int bw = B.descW - 16;
            int bh = B.descUpdateBarH;
            SolidBrush barBg(GPA(64, 0xC8, 0xA8, 0x4B));
            g.FillRectangle(&barBg, bx, by, bw, bh);
            Pen barBorder(Tok::GoldDim, 1.0f);
            g.DrawRectangle(&barBorder, bx, by, bw - 1, bh - 1);

            wstring msg = L"\u2191 Update available: " + it->second.localVersion
                        + L" \u2192 " + it->second.remoteVersion;
            g.DrawString(msg.c_str(), -1, g_fBtn,
                         RectF((REAL)(bx + 8), (REAL)by,
                               (REAL)(bw - 80), (REAL)bh),
                         &sfL, &gold);
            // [Details] link on the right
            StringFormat sfR;
            sfR.SetAlignment(StringAlignmentFar);
            sfR.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(L"[Details]", -1, g_fBtn,
                         RectF((REAL)bx, (REAL)by,
                               (REAL)(bw - 8), (REAL)bh),
                         &sfR, &gold);
        }
    } else {
        // No mod selected — show a hint
        StringFormat sfC;
        sfC.SetAlignment(StringAlignmentCenter);
        sfC.SetLineAlignment(StringAlignmentCenter);
        SolidBrush dim(Tok::TextDim);
        g.DrawString(L"(Select a mod to view its description)",
                     -1, g_fBtn,
                     RectF((REAL)B.descX, (REAL)B.descBodyY,
                           (REAL)B.descW, (REAL)B.descBodyH),
                     &sfC, &dim);
    }
}

// Break a command-line string at "-flag" boundaries so wrapping never
// leaves a "-" hanging at the end of a line. Tokens are defined as
// whitespace-separated runs starting with "-"; non-flag tokens (e.g. paths
// after -mod) follow their preceding flag without break. Greedy packing:
// each token joins the current line if it fits, otherwise a newline is
// inserted before it.
static wstring BreakLaunchArgsAtDash(Graphics& g, const wstring& s,
                                     Font* font, StringFormat* fmt,
                                     REAL maxWidth) {
    if (s.empty()) return s;
    // Tokenize: split on whitespace, then group so each "-flag" pulls
    // its (optional) following non-flag argument with it. Avoids
    // breaking between "-mod" and "ROK".
    vector<wstring> tokens;
    {
        size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && iswspace(s[i])) ++i;
            if (i >= s.size()) break;
            size_t start = i;
            while (i < s.size() && !iswspace(s[i])) ++i;
            tokens.push_back(s.substr(start, i - start));
        }
    }
    // Coalesce: a token that does NOT start with '-' attaches to the
    // previous token via a space.
    vector<wstring> groups;
    for (auto& t : tokens) {
        if (!groups.empty() && !t.empty() && t[0] != L'-') {
            groups.back() += L" " + t;
        } else {
            groups.push_back(t);
        }
    }
    // Greedy line packing
    wstring out;
    wstring line;
    auto measure = [&](const wstring& str) -> REAL {
        if (str.empty()) return 0.0f;
        RectF r;
        g.MeasureString(str.c_str(), -1, font,
                        RectF(0, 0, 4096, 4096), fmt, &r);
        return r.Width;
    };
    for (size_t i = 0; i < groups.size(); ++i) {
        wstring candidate = line.empty() ? groups[i]
                                         : (line + L" " + groups[i]);
        if (measure(candidate) <= maxWidth || line.empty()) {
            line = candidate;
        } else {
            // Group doesn't fit on current line — push current and
            // start a new line with this group.
            if (!out.empty()) out += L"\n";
            out += line;
            line = groups[i];
        }
    }
    if (!line.empty()) {
        if (!out.empty()) out += L"\n";
        out += line;
    }
    return out;
}

static void PaintLaunchOptions(Graphics& g, const BodyLayout& B) {
    using namespace LO;

    // No background fill / border drawn here — frame_panel_right.png supplies
    // the panel chrome. Stone shows through where the asset is transparent.

    // Header
    StringFormat sfL;
    sfL.SetAlignment(StringAlignmentNear);
    sfL.SetLineAlignment(StringAlignmentCenter);
    SolidBrush gold(Tok::Gold);
    g.DrawString(L"LAUNCH OPTIONS", -1, g_fColHdrMed,
                 RectF((REAL)(B.loX + 12), (REAL)(B.loY + 4),
                       (REAL)(B.loW - 24), 44.0f),
                 &sfL, &gold);
    Pen sep(Tok::Bronze, 1.0f);
    g.DrawLine(&sep,
               B.loX + 12, B.loY + 50,
               B.loX + B.loW - 12, B.loY + 50);

    // Flag checkboxes
    SolidBrush textBr(Tok::TextParchment);
    SolidBrush dimBr(Tok::TextDim);
    for (int i = 0; i < (int)(sizeof(FLAGS) / sizeof(FLAGS[0])); ++i) {
        const FlagDef& f = FLAGS[i];
        RECT fr = BodyFlagRect(B, i);
        bool checked = g_modSettings.*(f.member);

        // Checkbox glyph (27×28 native asset, or programmatic fallback)
        constexpr int CB_H = 28;
        int cbY = fr.top + ((fr.bottom - fr.top) - CB_H) / 2;
        DrawFlagCheckbox(g, (REAL)fr.left, (REAL)cbY, checked, false, f.isLocked);

        // Label to the right of the box (18 px Exocet, larger than other
        // UI text so it reads clearly inside the larger panel chrome).
        // NoWrap so multi-word labels ("Reset Maps", "Skip Intro") stay
        // on a single line at every scale — wrap was breaking them into
        // "RESET / MAPS" at 150% DPI × 100% UI. The rect width has an
        // extra 10 logical px past the cell's right edge — GDI+'s
        // MeasureString slightly underestimates Exocet at high DPI,
        // and without the extra room "Skip Intro" was clipping to
        // "Skip Intr". The +10 only consumes space if the label
        // needs it (NoWrap + left-aligned), and the next column's
        // own label rect starts well to the right of where this one
        // extends, so there's no visible overlap.
        SolidBrush* lblBr = f.isLocked ? &dimBr : &textBr;
        StringFormat sfFlag;
        sfFlag.SetAlignment(StringAlignmentNear);
        sfFlag.SetLineAlignment(StringAlignmentCenter);
        sfFlag.SetFormatFlags(sfFlag.GetFormatFlags() | StringFormatFlagsNoWrap);
        g.DrawString(f.name, -1, g_fModName,
                     RectF((REAL)(fr.left + 34), (REAL)fr.top,
                           (REAL)(fr.right - fr.left - 34 + 10),
                           (REAL)(fr.bottom - fr.top)),
                     &sfFlag, lblBr);
    }

    // ── Seed row (between flag grid and cmd preview) ─────────────────────
    // Checkbox toggles whether `-seed VALUE` actually gets appended to
    // the command line. Text input on the right shows / accepts the
    // numeric value; the arrow slice on the input's right edge opens
    // the seeds dropdown (presets from seeds.json + the rolling 3-slot
    // recents history). The input and arrow share one chrome (single
    // text_box.png stretched across both) but split into separate
    // hit-targets — see BodySeedInputRect / BodySeedArrowRect.
    {
        bool seedOn = g_modSettings.useSeed;
        RECT cbR = BodySeedCheckRect(B);
        DrawFlagCheckbox(g, (REAL)cbR.left, (REAL)cbR.top, seedOn, false, false);

        // "Seed" label — sits between the checkbox and the combo, same
        // font/treatment as the flag-grid labels so the seed row reads
        // as part of the same family of controls. The combo starts at
        // labelX + g_seedLabelLogicalW + 20 (see BodySeedComboRect), so
        // the gap from the label's rendered right edge to the combo's
        // left edge is a constant 20 px regardless of font family.
        //
        // The rect width is g_seedLabelLogicalW + 20 (not + 4) — a
        // generous safety pad so the text never clips even when
        // MeasureString slightly underestimates the actual rendered
        // glyph extent (some fonts include sub-pixel right-side
        // bearings that the default measurement skips). The combo
        // still sits at the originally-computed +20 gap from the
        // measured end, so the visible gap between "Seed" and the
        // input is preserved.
        SolidBrush seedLbl(Tok::TextParchment);
        StringFormat sfSeed;
        sfSeed.SetAlignment(StringAlignmentNear);
        sfSeed.SetLineAlignment(StringAlignmentCenter);
        sfSeed.SetFormatFlags(sfSeed.GetFormatFlags() | StringFormatFlagsNoWrap);
        g.DrawString(L"Seed", -1, g_fModName,
                     RectF((REAL)(cbR.right + 8), (REAL)B.loSeedY,
                           (REAL)g_seedLabelLogicalW + 30.0f, (REAL)B.loSeedH),
                     &sfSeed, &seedLbl);

        // Combined chrome — text_box.png stretched across the full
        // input+arrow rect. Drawing once (instead of separately per
        // half) keeps the bronze frame continuous and avoids a visible
        // seam between the two halves.
        RECT combo = BodySeedComboRect(B);
        int cx = combo.left, cy = combo.top;
        int cw = combo.right - combo.left, ch = combo.bottom - combo.top;
        if (Gdiplus::Bitmap* tb = AssetImage(L"text_box.png")) {
            InterpolationMode prev = g.GetInterpolationMode();
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.DrawImage(tb, cx, cy, cw, ch);
            g.SetInterpolationMode(prev);
        } else {
            SolidBrush bg(Tok::BgDeep);
            g.FillRectangle(&bg, cx, cy, cw, ch);
            Pen border(Tok::Bronze, 1.0f);
            g.DrawRectangle(&border, cx, cy, cw - 1, ch - 1);
        }

        // Input text — current seedArg, with placeholder when empty
        // and unfocused. Colour follows the checkbox state so the
        // user can see at a glance whether the value will actually be
        // used.
        RECT inR = BodySeedInputRect(B);
        const wstring& val = g_modSettings.seedArg;
        bool showPlaceholder = val.empty() && !g_seedInputFocused;
        const wchar_t* shown = showPlaceholder ? L"enter seed…" : val.c_str();
        Color textColor = showPlaceholder
                            ? Tok::GoldDim
                            : (seedOn ? Tok::Gold : Tok::GoldDim);
        SolidBrush textBr(textColor);
        StringFormat sfIn;
        sfIn.SetAlignment(StringAlignmentNear);
        sfIn.SetLineAlignment(StringAlignmentCenter);
        sfIn.SetFormatFlags(sfIn.GetFormatFlags() | StringFormatFlagsNoWrap);
        REAL inTextX = (REAL)(inR.left + 6);
        REAL inTextY = (REAL)inR.top;
        REAL inTextW = (REAL)(inR.right - inR.left - 8);
        REAL inTextH = (REAL)(inR.bottom - inR.top);
        // Selection highlight — drawn BEFORE the text so the text reads
        // on top. We measure the prefix to each end of the selection
        // and fill a rect between them. Semi-transparent gold so the
        // underlying chrome still shows through, and so it reads as an
        // overlay rather than a separate field.
        if (g_seedInputFocused && SeedHasSelection() && !val.empty()) {
            int lo = SeedSelLo(), hi = SeedSelHi();
            REAL lx = inTextX, hx = inTextX;
            if (lo > 0) {
                RectF mr;
                g.MeasureString(val.substr(0, lo).c_str(), -1, g_fBtn,
                                PointF(inTextX, inTextY), &sfIn, &mr);
                lx = inTextX + mr.Width;
            }
            if (hi > 0) {
                RectF mr;
                g.MeasureString(val.substr(0, hi).c_str(), -1, g_fBtn,
                                PointF(inTextX, inTextY), &sfIn, &mr);
                hx = inTextX + mr.Width;
            }
            // Alpha-blended gold over the bronze chrome reads as a
            // recognizable selection highlight without inventing a new
            // theme color. Inset 3 px top/bottom so the highlight
            // doesn't touch the chrome border.
            SolidBrush selBr(Color(0x80, 0xC8, 0xA8, 0x4B));
            g.FillRectangle(&selBr,
                            lx, (REAL)(inR.top + 3),
                            hx - lx, (REAL)(inR.bottom - inR.top - 6));
        }

        g.DrawString(shown, -1, g_fBtn,
                     RectF(inTextX, inTextY, inTextW, inTextH),
                     &sfIn, &textBr);

        // Caret — only when focused and the blink state is "on". Position
        // by measuring the prefix string up to the caret index; an empty
        // prefix yields width 0 so the caret sits at the left padding.
        if (g_seedInputFocused && g_seedCaretVisible) {
            int caret = g_seedCaretPos;
            if (caret < 0) caret = 0;
            if (caret > (int)val.size()) caret = (int)val.size();
            REAL prefixW = 0.0f;
            if (caret > 0) {
                wstring prefix = val.substr(0, caret);
                RectF mr;
                g.MeasureString(prefix.c_str(), -1, g_fBtn,
                                PointF(inTextX, inTextY), &sfIn, &mr);
                prefixW = mr.Width;
            }
            // 1-px-thick vertical line, inset 4 px from top/bottom of
            // the input rect so it doesn't touch the chrome border.
            Pen caretPen(Tok::Gold, 1.5f);
            REAL caretX = inTextX + prefixW;
            g.DrawLine(&caretPen,
                       caretX, (REAL)(inR.top + 4),
                       caretX, (REAL)(inR.bottom - 4));
        }

        // Arrow button — separate visual slice on the right edge.
        // Vertical hairline divider distinguishes it from the input.
        RECT arR = BodySeedArrowRect(B);
        Pen div(Tok::BronzeDim, 1.0f);
        g.DrawLine(&div, (INT)arR.left, (INT)(arR.top + 4),
                         (INT)arR.left, (INT)(arR.bottom - 4));
        StringFormat sfA;
        sfA.SetAlignment(StringAlignmentCenter);
        sfA.SetLineAlignment(StringAlignmentCenter);
        SolidBrush arrowBr(Tok::Gold);
        g.DrawString(L"\u25BE", -1, g_fBtn,
                     RectF((REAL)arR.left, (REAL)arR.top,
                           (REAL)(arR.right - arR.left),
                           (REAL)(arR.bottom - arR.top)),
                     &sfA, &arrowBr);
    }

    // ── Cmd preview text bar ──────────────────────────────────────────────
    // Dynamic height: 1 / 2 / 3 wrapped lines all fit. To choose the
    // box height we wrap the args FIRST (need a Graphics for measurement,
    // which is why this lives at paint time rather than in ComputeBodyLayout),
    // count newlines, and pick a height row-per-row. Box shrinks when the
    // user disables flags and the wrapped form fits on fewer lines.
    wstring cmd = BuildLaunchArgs();
    if (cmd.empty()) cmd = L"(no mod selected)";
    StringFormat sfPrev;
    sfPrev.SetAlignment(StringAlignmentNear);
    sfPrev.SetLineAlignment(StringAlignmentNear);   // top-anchored when wrapped
    int px = B.loX + 12, py = B.loCmdPreviewY;
    int pw = B.loW - 24;
    REAL maxW = (REAL)(pw - 16);
    wstring wrapped = BreakLaunchArgsAtDash(g, cmd, g_fCmdArgs, &sfPrev, maxW);

    int lineCount = 1;
    for (wchar_t c : wrapped) if (c == L'\n') ++lineCount;
    if (lineCount < 1) lineCount = 1;
    if (lineCount > 3) lineCount = 3;
    // Logical box height per line count: 1 → 28, 2 → 44, 3 → 60.
    // Matches the previous static 44 at the 2-line case (the common one).
    int ph = 12 + lineCount * 16;

    if (Gdiplus::Bitmap* tb = AssetImage(L"text_box.png")) {
        InterpolationMode prev = g.GetInterpolationMode();
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(tb, px, py, pw, ph);
        g.SetInterpolationMode(prev);
    } else {
        SolidBrush previewBg(Tok::BgDeep);
        g.FillRectangle(&previewBg, px, py, pw, ph);
        Pen previewBorder(Tok::Bronze, 1.0f);
        g.DrawRectangle(&previewBorder, px, py, pw - 1, ph - 1);
    }

    SolidBrush previewText(Tok::GoldDim);
    g.DrawString(wrapped.c_str(), -1, g_fCmdArgs,
                 RectF((REAL)(px + 8), (REAL)(py + 4),
                       (REAL)(pw - 16), (REAL)(ph - 8)),
                 &sfPrev, &previewText);
}

static void PaintBottomPanel(Graphics& g, int W, int H) {
    using namespace LO;
    // bodyH matches Layout() — always the frame's native height. The
    // expansion panel sits below this line, not by shrinking the body.
    int frameNativeH = 1024;
    if (Gdiplus::Bitmap* fm = AssetImage(L"frame_main.png"))
        frameNativeH = (int)fm->GetHeight();
    int bodyH = frameNativeH;

    // The frame_main / frame_expand assets provide all the bottom chrome.
    // Nothing to draw if the panel is collapsed.
    if (!g_bottomExpanded) return;

    // Inset content by frame_expand.png's measured filigree (mirrors the
    // child-button positioning in Layout()).
    FrameInset fe = MeasureFrameInset(L"frame_expand.png");
    int feL = (fe.left  > 0 ? fe.left  : 16) + 8;
    int feR = (fe.right > 0 ? fe.right : 16) + 8;
    int feT = (fe.top   > 0 ? fe.top   : 22) + 4;

    int panelTop = bodyH + feT;
    int colGap   = EXP_COL_GAP;          // internal Tools-column gap (no divider)
    int secGap   = EXP_SEC_GAP;          // section gap (holds a divider)

    // Match Layout()'s section X positions. Sections 1 and 3 are EXP_BTN_W
    // wide (single column); section 2 (Tools) is two columns plus the
    // internal gap. Section gaps (secGap) are wider than the internal gap so
    // the dividers clear the button HWNDs.
    int sec1W = EXP_BTN_W;
    int sec3W = EXP_BTN_W;
    int sec2W = EXP_BTN_W * 2 + colGap;
    int framedW   = W - feL - feR;
    int totalBtnW = sec1W + sec2W + sec3W;
    int slack     = framedW - totalBtnW - secGap * 2;
    int leftPad   = slack / 2;
    int sec1X = feL + leftPad;
    int sec2X = sec1X + sec1W + secGap;
    int sec3X = sec2X + sec2W + secGap;

    // Divider centers, and column-centered X for the single-column sections
    // (mirror Layout()). References/Downloads are centered between the panel
    // edge and the nearest divider rather than anchored at sec1X/sec3X.
    int div1X = sec1X + sec1W + secGap / 2;
    int div2X = sec2X + sec2W + secGap / 2;
    int refBtnX = (feL + div1X - EXP_BTN_W) / 2;
    int dlBtnX  = (div2X + (W - feR) - EXP_BTN_W) / 2;

    StringFormat sfC;
    sfC.SetAlignment(StringAlignmentCenter);
    sfC.SetLineAlignment(StringAlignmentCenter);
    SolidBrush gold(Tok::Gold);

    // Smaller title font (g_fExpHdr, 18px) in the tightened header band.
    auto hdr = [&](const wchar_t* text, int x, int w) {
        g.DrawString(text, -1, g_fExpHdr,
                     RectF((REAL)x, (REAL)panelTop, (REAL)w, (REAL)EXP_HDR_H),
                     &sfC, &gold);
    };
    // References/Downloads titles centered over their (column-centered)
    // buttons; Tools stays centered over its two-column block.
    hdr(L"REFERENCES",         refBtnX, EXP_BTN_W);
    hdr(L"TOOLS AND PROGRAMS", sec2X,   sec2W);
    hdr(L"DOWNLOADS",          dlBtnX,  EXP_BTN_W);

    // ── Vertical dividers flanking the Tools column ──────────────────────
    // A mirrored pair centered in the two inter-section gaps, running the
    // full section height. Asset-driven; a thin bronze rule fades in as a
    // fallback if a PNG is missing.
    const int divTop = panelTop + LO::EXP_DIVIDER_TOP_PAD;
    auto divider = [&](const wchar_t* name, int gapCenter) {
        if (Gdiplus::Bitmap* dv = AssetImage(name)) {
            int dw = (int)dv->GetWidth();
            DrawAssetAt(g, dv, gapCenter - dw / 2, divTop);
        } else {
            // Fallback: a 2px bronze rule that fades at both ends so it
            // doesn't read as a hard bar.
            int h = LO::EXP_DIVIDER_FALLBACK_H;
            REAL x = (REAL)gapCenter - 1.0f;
            LinearGradientBrush rule(
                RectF(x, (REAL)divTop, 2.0f, (REAL)h),
                GPA(0,   0x8B, 0x5A, 0x2B),
                GPA(200, 0x8B, 0x5A, 0x2B),
                LinearGradientModeVertical);
            // Symmetric fade: transparent at top & bottom, solid mid.
            REAL pos[3]   = { 0.0f, 0.5f, 1.0f };
            Color cols[3] = { GPA(0, 0x8B,0x5A,0x2B),
                              GPA(210,0x8B,0x5A,0x2B),
                              GPA(0, 0x8B,0x5A,0x2B) };
            rule.SetInterpolationColors(cols, pos, 3);
            g.FillRectangle(&rule, RectF(x, (REAL)divTop, 2.0f, (REAL)h));
        }
    };
    divider(L"left_expand_divider.png",  div1X + LO::EXP_DIVIDER_X_NUDGE);
    divider(L"right_expand_divider.png", div2X + LO::EXP_DIVIDER_X_NUDGE);
}

// ═══════════════════════════════════════════════════════════════════════
//  TOP CENTERPIECE GEM ORNAMENT
// ═══════════════════════════════════════════════════════════════════════
//  A symmetric crown finial that sits over the top filigree band, centered
//  horizontally on the window. Asset-driven (ornament_gem.png); a
//  programmatic bronze-lozenge-plus-sapphire fallback renders when the PNG
//  is absent so the crest is never blank. Called from PaintBody right after
//  frame_main.png so it layers on top of the frame's top border.
static void PaintTopOrnament(Graphics& g, int W) {
    const int topY = LO::ORNAMENT_TOP_Y;

    // ── Asset path: drop the real PNG in at native size, centered ────────
    if (Gdiplus::Bitmap* orn = AssetImage(L"ornament_gem.png")) {
        int aw = (int)orn->GetWidth();
        DrawAssetAt(g, orn, (W - aw) / 2, topY);
        return;
    }

    // ── Programmatic fallback ────────────────────────────────────────────
    // Nominal footprint roughly matching the asset, centered on W/2.
    const REAL ow = 150.0f, oh = 86.0f;
    const REAL ox = (W - ow) * 0.5f;
    const REAL oy = (REAL)topY;
    const REAL cx = ox + ow * 0.5f;
    const REAL cy = oy + oh * 0.5f;

    SmoothingMode prevSmooth = g.GetSmoothingMode();
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    // Bronze setting: a wide horizontal lozenge behind the gem.
    {
        GraphicsPath lozenge;
        PointF pts[4] = {
            PointF(cx,            oy + 6.0f),       // top
            PointF(ox + ow - 6,   cy),              // right
            PointF(cx,            oy + oh - 6),     // bottom
            PointF(ox + 6,        cy),              // left
        };
        lozenge.AddPolygon(pts, 4);
        LinearGradientBrush bronze(
            RectF(ox, oy, ow, oh),
            Tok::BronzeBright, Tok::BronzeDim, LinearGradientModeVertical);
        g.FillPath(&bronze, &lozenge);
        Pen goldEdge(Tok::Gold, 2.0f);
        g.DrawPath(&goldEdge, &lozenge);
    }

    // Filigree wings: short tapering strokes flanking the setting.
    {
        Pen wing(Tok::BronzeBright, 3.0f);
        wing.SetStartCap(LineCapRound);
        wing.SetEndCap(LineCapRound);
        g.DrawLine(&wing, ox + 6,      cy, ox - 30,        cy - 10);
        g.DrawLine(&wing, ox + ow - 6, cy, ox + ow + 30,   cy - 10);
    }

    // Sapphire: a smaller diamond with a vertical blue gradient, gold rim,
    // and a specular highlight near the top-left facet.
    {
        const REAL gw = 30.0f, gh = 40.0f;
        GraphicsPath gem;
        PointF gp[4] = {
            PointF(cx,             cy - gh * 0.5f),
            PointF(cx + gw * 0.5f, cy),
            PointF(cx,             cy + gh * 0.5f),
            PointF(cx - gw * 0.5f, cy),
        };
        gem.AddPolygon(gp, 4);
        LinearGradientBrush blue(
            RectF(cx - gw, cy - gh, gw * 2, gh * 2),
            GP(0x6C, 0xA8, 0xFF), GP(0x10, 0x2A, 0x78),
            LinearGradientModeVertical);
        g.FillPath(&blue, &gem);
        Pen rim(Tok::GoldBright, 1.5f);
        g.DrawPath(&rim, &gem);

        GraphicsPath spec;
        PointF sp[3] = {
            PointF(cx,              cy - gh * 0.5f + 3),
            PointF(cx - gw * 0.28f, cy - 2),
            PointF(cx,              cy - 2),
        };
        spec.AddPolygon(sp, 3);
        SolidBrush hi(GPA(150, 0xFF, 0xFF, 0xFF));
        g.FillPath(&hi, &spec);
    }

    g.SetSmoothingMode(prevSmooth);
}

// ═══════════════════════════════════════════════════════════════════════
//  FRAME CORNER ACCENTS
// ═══════════════════════════════════════════════════════════════════════
//  Four bronze filigree L-brackets, one per corner of the main window
//  frame. Each piece's dense elbow anchors to its window corner; the arms
//  run inward along the two frame edges. Asset-driven (corner_tl / _tr /
//  _bl / _br .png); a minimal gold L-flourish fallback draws per corner
//  when a PNG is absent. Layered on frame_main.png from PaintBody.
static void PaintCornerAccents(Graphics& g, int W) {
    using namespace LO;
    int frameH = 1024;
    if (Gdiplus::Bitmap* fm = AssetImage(L"frame_main.png"))
        frameH = (int)fm->GetHeight();

    // Anchor to the inner edge of frame_main.png's border filigree so each
    // bracket nestles into the content-opening corner (right inside the
    // border, not on the absolute window corner). CORNER_ACCENT_INSET_* is an
    // additional nudge from that edge: positive pushes further into the
    // content, negative pulls back toward / onto the filigree.
    //
    // MeasureFrameInset keys off scan-line density. The top/bottom borders are
    // solid horizontal bands and measure correctly, but the left/right borders
    // are sparser vertical filigree that falls under the density threshold, so
    // it returns ~0 for left/right. The border is ~uniform thickness, so fall
    // back to the (reliable) vertical measurement when horizontal degenerates.
    FrameInset fi = MeasureFrameInset(L"frame_main.png");
    int hL = (fi.left  >= 8) ? fi.left  : fi.top;
    int hR = (fi.right >= 8) ? fi.right : fi.bottom;
    int li = hL        + CORNER_ACCENT_INSET_X;   // left   inner edge
    int ri = hR        + CORNER_ACCENT_INSET_X;   // right  inner edge
    int ti = fi.top    + CORNER_ACCENT_INSET_Y;   // top    inner edge
    int bi = fi.bottom + CORNER_ACCENT_INSET_Y;   // bottom inner edge

    struct Corner { const wchar_t* name; bool right; bool bottom; };
    const Corner corners[4] = {
        { L"corner_tl.png", false, false },
        { L"corner_tr.png", true,  false },
        { L"corner_bl.png", false, true  },
        { L"corner_br.png", true,  true  },
    };

    SmoothingMode prevSmooth = g.GetSmoothingMode();
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    for (const Corner& c : corners) {
        Gdiplus::Bitmap* bm = AssetImage(c.name);
        if (bm) {
            int bw = (int)bm->GetWidth();
            int bh = (int)bm->GetHeight();
            int x = c.right  ? (W - bw - ri)      : li;
            int y = c.bottom ? (frameH - bh - bi) : ti;
            DrawAssetAt(g, bm, x, y);
        } else {
            // Minimal fallback: a short gold L hugging the corner, with a
            // bronze diagonal accent — reads as a bracket without pretending
            // to reproduce the filigree.
            const REAL len = 44.0f, off = 8.0f;
            REAL px = c.right  ? (REAL)(W - ri - off)      : (REAL)(li + off);
            REAL py = c.bottom ? (REAL)(frameH - bi - off) : (REAL)(ti + off);
            REAL dx = c.right  ? -1.0f : 1.0f;
            REAL dy = c.bottom ? -1.0f : 1.0f;
            Pen gold(Tok::Gold, 2.0f);
            gold.SetStartCap(LineCapRound);
            gold.SetEndCap(LineCapRound);
            g.DrawLine(&gold, px, py, px + dx * len, py);
            g.DrawLine(&gold, px, py, px, py + dy * len);
            Pen bronze(Tok::BronzeBright, 1.5f);
            g.DrawLine(&bronze, px + dx * 4, py + dy * 4,
                       px + dx * (len * 0.5f), py + dy * (len * 0.5f));
        }
    }

    g.SetSmoothingMode(prevSmooth);
}

// Paint a single toolbar control (label on the left, value box on the
// right). Caller supplies the label width so each control packs its
// label/value with no wasted gap (the previous uniform "35% of row"
// gave Scale and Colour way too much label whitespace).
//
//  labelW            logical px reserved for the label (the value box
//                    starts at r.left + labelW + 1 and runs to r.right).
//  valueText         non-null = draw this string centered in the value
//                    box. Optionally rendered in valueFontFamily.
//  valueFontFamily   non-null = use this Gdiplus FontFamily name for
//                    the value text (Font dropdown renders "Style" in
//                    the currently selected font face).
//  valueFontStyle    FontStyle bits to use with valueFontFamily (e.g.
//                    FontStyleBold for Cinzel-Bold). Ignored when
//                    valueFontFamily is null.
//  swatch            non-null = draw a small square color chip,
//                    centered in the value box's content area.
//  showChevron       true = draw the dropdown chevron at the right edge.
//  cycleHint         true = draw a circular-arrow glyph at the right
//                    edge (the affordance for a cycling button — Scale
//                    advances through preset states instead of opening
//                    a popup, and needs a visual that doesn't promise
//                    a dropdown). Mutually exclusive with showChevron.
static void PaintToolbarControl(Graphics& g, const RECT& r,
                                const wchar_t* label, int labelW,
                                const wchar_t* valueText,
                                const wchar_t* valueFontFamily,
                                INT valueFontStyle,
                                const COLORREF* swatch,
                                bool showChevron,
                                bool cycleHint = false) {
    SolidBrush textBr(Tok::TextParchment);
    SolidBrush valBr(Tok::Gold);

    int rowH = r.bottom - r.top;

    // Label — right-aligned, NoWrap. Right-alignment puts the text flush
    // against the value box on its right with any unused label rect
    // sitting on the LEFT (which abuts the previous control or column
    // padding). Was previously left-aligned, which created a visible
    // gap between the title text and the textbox at every scale.
    //
    // Skip the label draw entirely when caller passes null/empty —
    // value-only callers (e.g. the On Launch textbox, whose label
    // "ON LAUNCH" is rendered as a separate header above the row) use
    // labelW=0 and don't want any text on the left.
    StringFormat sfL;
    sfL.SetAlignment(StringAlignmentFar);
    sfL.SetLineAlignment(StringAlignmentCenter);
    sfL.SetFormatFlags(sfL.GetFormatFlags() | StringFormatFlagsNoWrap);
    if (label && *label && labelW > 0) {
        g.DrawString(label, -1, g_fBtn,
                     RectF((REAL)r.left, (REAL)r.top,
                           (REAL)labelW, (REAL)rowH),
                     &sfL, &textBr);
    }

    // Value box: from r.left + labelW + 1 to r.right
    int bx = r.left + labelW + 1;
    int by = r.top + 2;
    int bw = r.right - bx;
    int bh = rowH - 4;
    if (bw <= 0) return;     // degenerate (caller picked too-narrow rect)
    if (Gdiplus::Bitmap* tb = AssetImage(L"text_box.png")) {
        InterpolationMode prev = g.GetInterpolationMode();
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(tb, bx, by, bw, bh);
        g.SetInterpolationMode(prev);
    } else {
        OPDrawBtnFrame(g, bx, by, bw, bh, false);
    }

    int chevronW = (showChevron || cycleHint) ? 22 : 0;
    int contentL = bx + 6;
    int contentR = bx + bw - chevronW - 4;

    if (swatch) {
        // Small square swatch centered in the content area.
        int swH = bh - 8;
        int swW = swH;       // square
        int swX = contentL + ((contentR - contentL) - swW) / 2;
        if (swX < contentL) swX = contentL;
        int swY = by + (bh - swH) / 2;
        SolidBrush sw(Color(GetRValue(*swatch),
                            GetGValue(*swatch),
                            GetBValue(*swatch)));
        g.FillRectangle(&sw, swX, swY, swW, swH);
        Pen swBorder(Tok::BronzeDim, 1.0f);
        g.DrawRectangle(&swBorder, swX, swY, swW, swH);
    } else if (valueText) {
        StringFormat sfC;
        sfC.SetAlignment(StringAlignmentCenter);
        sfC.SetLineAlignment(StringAlignmentCenter);
        sfC.SetFormatFlags(sfC.GetFormatFlags() | StringFormatFlagsNoWrap);

        // When a font family is specified, render the value in that
        // face (Font dropdown's "STYLE" preview). Falls back to g_fBtn
        // when the family lookup fails so the cell never goes blank.
        bool drewWithFamily = false;
        if (valueFontFamily && *valueFontFamily) {
            FontFamily fam(valueFontFamily);
            if (fam.GetLastStatus() == Ok) {
                REAL fontPx = (REAL)max(10, (int)(bh * 0.65f));
                Font tempFont(&fam, fontPx, valueFontStyle, UnitPixel);
                g.DrawString(valueText, -1, &tempFont,
                             RectF((REAL)contentL, (REAL)by,
                                   (REAL)(contentR - contentL), (REAL)bh),
                             &sfC, &valBr);
                drewWithFamily = true;
            }
        }
        if (!drewWithFamily) {
            g.DrawString(valueText, -1, g_fBtn,
                         RectF((REAL)contentL, (REAL)by,
                               (REAL)(contentR - contentL), (REAL)bh),
                         &sfC, &valBr);
        }
    }

    if (showChevron) {
        if (Gdiplus::Bitmap* ch = AssetImage(L"dropdown_chevron.png")) {
            int chW = (int)ch->GetWidth();
            int chH = (int)ch->GetHeight();
            int targetH = bh - 4;
            int targetW = chW * targetH / chH;
            int cx = bx + bw - targetW - 4;
            int cy = by + (bh - targetH) / 2;
            InterpolationMode prev = g.GetInterpolationMode();
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.DrawImage(ch, cx, cy, targetW, targetH);
            g.SetInterpolationMode(prev);
        } else {
            StringFormat sfR;
            sfR.SetAlignment(StringAlignmentCenter);
            sfR.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(L"\u25BE", -1, g_fBtn,
                         RectF((REAL)(bx + bw - 16), (REAL)by, 16.0f, (REAL)bh),
                         &sfR, &valBr);
        }
    }
    if (cycleHint) {
        // Circular arrow (U+21BB) — the visual affordance for a cycling
        // button. Drawn at the same right-edge slot the chevron uses on
        // dropdown controls, so the spacing matches. GDI+ font linking
        // substitutes a system font if Exocet doesn't ship the glyph.
        StringFormat sfR;
        sfR.SetAlignment(StringAlignmentCenter);
        sfR.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"\u21BB", -1, g_fBtn,
                     RectF((REAL)(bx + bw - 22), (REAL)by, 20.0f, (REAL)bh),
                     &sfR, &valBr);
    }
}

static void PaintBody(HDC hdc, int W, int H) {
    // W and H arrive in LOGICAL pixels (the caller converts via U() before
    // passing them in). The Graphics gets a ScaleTransform that maps every
    // logical coordinate to its physical pixel position, so the entire
    // paint body below can be written exactly as it was when g_scale was
    // implicitly 1.0 — frame_main, the stone slices, frame_panel_left/right,
    // the gem ornament, mod-list painting, the launch-options panel, the
    // logo, the title-bar buttons, everything inherits the scale.
    Graphics g(hdc);
    g.ScaleTransform((REAL)g_scale, (REAL)g_scale);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // ── Layered backdrop ────────────────────────────────────────────────
    //   1. Stone texture covers the full window (collapsed-state region).
    //      The expanded bottom area below WIN_H is intentionally left
    //      unframed for now — a separate frame_expand.png asset will land
    //      in a later batch.
    //   2. Frame overlay (ornate filigree, gem ornaments) on top.
    //   3. D2RLOADER logo at native size in the left rail.
    //   4. frame_panel_right.png (the right column's framed three-panel
    //      asset) drawn at native size, positioned by ComputeBodyLayout.
    // Any of these may be nullptr (asset missing); the helpers no-op then.
    if (Gdiplus::Bitmap* bgStone = AssetImage(L"bg_stone.png")) {
        // Stone covers the COLLAPSED window region (top 1024px). When the
        // bottom panel is expanded, that area is drawn separately by
        // bg_expand.png below — so we cap at the frame's native height.
        UINT sw = bgStone->GetWidth();
        UINT sh = bgStone->GetHeight();
        int collapsedH = 1024;
        if (Gdiplus::Bitmap* fm = AssetImage(L"frame_main.png"))
            collapsedH = (int)fm->GetHeight();
        int srcW = min((int)sw, W);
        int srcH = min((int)sh, collapsedH);
        g.DrawImage(bgStone,
                    Rect(0, 0, srcW, srcH),
                    0, 0, (REAL)srcW, (REAL)srcH,
                    UnitPixel);
    }
    // Bottom-expansion stone (only when the panel is open). Drawn at
    // native size, 1:1, positioned directly below the main frame.
    if (g_bottomExpanded) {
        if (Gdiplus::Bitmap* bgExp = AssetImage(L"bg_expand.png")) {
            int collapsedH = 1024;
            if (Gdiplus::Bitmap* fm = AssetImage(L"frame_main.png"))
                collapsedH = (int)fm->GetHeight();
            int sw = (int)bgExp->GetWidth();
            int sh = (int)bgExp->GetHeight();
            g.DrawImage(bgExp,
                        Rect(0, collapsedH, min(sw, W), sh),
                        0, 0, (REAL)min(sw, W), (REAL)sh,
                        UnitPixel);
        }
    }

    // ── Per-region stone slices ─────────────────────────────────────────
    // Each of the 7 UI regions (left rail, title band, mod list, mod
    // description, mod-list footer, launch options, play) gets its own
    // chunk of bg_stone.png sampled from a DIFFERENT non-(0,0) source
    // offset. Visually each section reads as its own piece of texture
    // instead of a single uniform background — matching the original
    // mockup's "scattered stone" feel. Drawn over the base bg_stone fill
    // (which still serves as the fallback under buttons and the seam
    // between regions) but BEFORE frame_main and the panel chrome, so the
    // bronze borders mask any overflow at the region edges.
    {
        BodyLayout B  = ComputeBodyLayout(W, H);
        LeftPanelGeom lg = ComputeLeftPanelGeom(B);
        auto stoneSlice = [&](int dx, int dy, int dw, int dh,
                              int sx, int sy) {
            if (dw <= 0 || dh <= 0) return;
            Gdiplus::Bitmap* bg = AssetImage(L"bg_stone.png");
            if (!bg) return;
            int sw = (int)bg->GetWidth(), sh = (int)bg->GetHeight();
            int srcW = min(dw, sw - sx);
            int srcH = min(dh, sh - sy);
            if (srcW <= 0 || srcH <= 0) return;
            g.DrawImage(bg,
                        Rect(dx, dy, srcW, srcH),
                        sx, sy, (REAL)srcW, (REAL)srcH,
                        UnitPixel);
        };
        // The seven offsets are chosen by hand to be distinct, not (0,0),
        // and to keep src+dim within bg_stone's 1536×1024 footprint.
        constexpr int LIST_BAND_H = 60;   // small upper band inside frame_panel_left for MODS+Refresh
        // 1. Left rail / left column interior
        stoneSlice(0, 0, lg.x, 986,                                  80,  30);
        // 2. Title band — inside frame_panel_left, just above the list
        stoneSlice(lg.x, lg.mainTop,  lg.w, LIST_BAND_H,             600, 800);
        // 3. Mod list interior — inside frame_panel_left main region
        stoneSlice(lg.x, lg.mainTop + LIST_BAND_H,
                   lg.w, lg.dividerY - (lg.mainTop + LIST_BAND_H),   300, 200);
        // 4. MOD DESCRIPTION interior (frame_panel_right top region)
        stoneSlice(B.descX, B.descY, B.descW, B.descH,               900, 100);
        // 5. Mod-list footer — inside frame_panel_left below the divider
        stoneSlice(lg.x, lg.dividerY, lg.w, lg.bottom - lg.dividerY, 800, 700);
        // 6. LAUNCH OPTIONS interior (frame_panel_right mid region)
        stoneSlice(B.loX, B.loY, B.loW, B.loH,                       200, 500);
        // 7. PLAY area — bottom region of frame_panel_right, from the
        //    bottom of LAUNCH OPTIONS down to the panel bottom.
        stoneSlice(B.rightX, B.loY + B.loH, B.rightW,
                   (B.rightY + B.rightH) - (B.loY + B.loH),          700, 850);
    }
    if (Gdiplus::Bitmap* frame = AssetImage(L"frame_main.png")) {
        // Draw at native size, top-left aligned. No stretch.
        int frameH = (int)frame->GetHeight();   // native 1024
        DrawAssetStretched(g, frame, 0, 0, W, frameH);
    }
    // Filigree corner accents on the four frame corners. (Asset-or-fallback.)
    PaintCornerAccents(g, W);
    // Bottom-expansion frame (only when panel is open). Native size, 1:1,
    // positioned immediately below the main frame.
    if (g_bottomExpanded) {
        if (Gdiplus::Bitmap* frExp = AssetImage(L"frame_expand.png")) {
            int collapsedH = 1024;
            if (Gdiplus::Bitmap* fm = AssetImage(L"frame_main.png"))
                collapsedH = (int)fm->GetHeight();
            DrawAssetAt(g, frExp, 0, collapsedH);
        }
    }
    if (Gdiplus::Bitmap* logo = AssetImage(L"logo_d2rloader.png")) {
        // The logo's visual weight is left-biased (the flame extends to the
        // image's left edge while the right side has white space after the
        // "™"). Naive center-the-bounding-box positioning makes it visually
        // crowded against the left filigree. A small rightward nudge brings
        // the visual center back into the middle of the rail.
        FrameInset fi2 = MeasureFrameInset(L"frame_main.png");
        constexpr int LOGO_VISUAL_OFFSET = 18;     // was 24; nudged 6 px left
        int lw = (int)logo->GetWidth();
        int lh = (int)logo->GetHeight();
        int lx = (fi2.left + 12) + (LO::LEFT_RAIL_W - lw) / 2 + LOGO_VISUAL_OFFSET;
        int ly = fi2.top + 16;
        DrawAssetAt(g, logo, lx, ly);

        // Version label, ~15 px below the bottom of the logo image.
        // Centered within the logo's horizontal span so it visually
        // hangs off the "Mod Launcher" subtitle baked into the logo.
        // Clickable ONLY when an update has been detected — see the
        // WM_LBUTTONDOWN / WM_SETCURSOR hit-tests, which both gate on
        // g_launcherUpdateAvailable so the label is inert (no hand
        // cursor, no click action) when nothing new is waiting on
        // GitHub. When an update IS pending, the label glows gold and
        // a click re-runs the check bypassing any Skip Version state.
        //
        // Font: g_fColHdrSm (16 pt) — ~25% larger than g_fBtn (13 pt),
        // built from the same display family + style so weight and
        // family selection track the user's UI font choice.
        Font* verFont = g_fColHdrSm ? g_fColHdrSm : g_fBtn;
        if (verFont) {
            wstring verText = wstring(L"v") + LAUNCHER_VERSION;
            int verW = lw;
            int verH = 24;
            int verX = lx;
            int verY = ly + lh - 10;

            // Remember the hit rect (logical coords) for click +
            // cursor handling. Stored every paint so it tracks any
            // layout shifts (DPI change, window resize, etc.).
            g_versionLabelRect.left   = verX;
            g_versionLabelRect.top    = verY;
            g_versionLabelRect.right  = verX + verW;
            g_versionLabelRect.bottom = verY + verH;

            StringFormat verFmt;
            verFmt.SetAlignment(StringAlignmentCenter);
            verFmt.SetLineAlignment(StringAlignmentNear);
            verFmt.SetFormatFlags(verFmt.GetFormatFlags()
                                  | StringFormatFlagsNoWrap);

            // Glow halo when an update is available — draw the text
            // 8 times at single-pixel offsets in semi-transparent
            // gold BEFORE the solid fill on top. The aliased seams
            // between offsets blend into a soft glow that hugs the
            // letterforms.
            if (g_launcherUpdateAvailable) {
                SolidBrush haloBr(Color(110, 255, 215, 90));
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        if (dx == 0 && dy == 0) continue;
                        RectF haloRect((REAL)(verX + dx),
                                       (REAL)(verY + dy),
                                       (REAL)verW, (REAL)verH);
                        g.DrawString(verText.c_str(), -1, verFont,
                                     haloRect, &verFmt, &haloBr);
                    }
                }
            }

            // Solid text fill — gold when an update is pending
            // (matches the halo color), parchment otherwise so the
            // label reads as ambient info rather than a call to
            // action.
            SolidBrush verFill(g_launcherUpdateAvailable
                               ? Tok::Gold
                               : Tok::TextParchment);
            RectF verRect((REAL)verX, (REAL)verY,
                          (REAL)verW, (REAL)verH);
            g.DrawString(verText.c_str(), -1, verFont,
                         verRect, &verFmt, &verFill);
        }
    }
    // Panel asset draws at native size, positioned per ComputeBodyLayout.
    // Drawn before the panel content so the bronze chrome sits behind text.
    if (Gdiplus::Bitmap* panel = AssetImage(L"frame_panel_right.png")) {
        BodyLayout B = ComputeBodyLayout(W, H);
        DrawAssetAt(g, panel, B.rightX, B.rightY);
    }
    // Center mod-list panel frame: drawn at NATIVE 1:1 (660×955), anchored at
    // (centerX, bodyTop + PANEL_LEFT_Y_NUDGE) so its top sits at the inner
    // edge of the frame_main top filigree.
    if (Gdiplus::Bitmap* lpanel = AssetImage(L"frame_panel_left.png")) {
        BodyLayout Bl = ComputeBodyLayout(W, H);
        LeftPanelGeom lg = ComputeLeftPanelGeom(Bl);
        DrawAssetAt(g, lpanel, lg.x, lg.y);
    }
    // Top centerpiece gem ornament — drawn AFTER frame_panel_left so the gem
    // layers on top where the panel's top extends up into the filigree band.
    PaintTopOrnament(g, W);

    PaintLeftRail(g, W, H);

    BodyLayout B = ComputeBodyLayout(W, H);
    PaintModDescription(g, B);
    PaintLaunchOptions(g, B);

    // Center column "MODS" header — sits under frame_panel_left's top
    // divider, in the same band as Refresh and the list. Inset from the
    // frame's left border by PANEL_LEFT_PAD. 24px g_fColHdrMed.
    using namespace LO;
    LeftPanelGeom lg = ComputeLeftPanelGeom(B);
    SolidBrush gold(Tok::Gold);
    StringFormat sfL;
    sfL.SetAlignment(StringAlignmentNear);
    sfL.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"MODS", -1, g_fColHdrMed,
                 RectF((REAL)(lg.x + PANEL_LEFT_PAD), (REAL)(lg.mainTop + 4),
                       (REAL)(lg.w - PANEL_LEFT_PAD * 2), 44.0f),
                 &sfL, &gold);

    // ── Center-column toolbar (Scale / Font / Colour) ───────────────────
    // Three controls sitting in the strip above the expand arrow. The
    // Scale control is a cycling button (clicking advances through 3
    // presets); Font shows "STYLE" rendered in the currently selected
    // face; Colour shows a small swatch chip. Per-control widths come
    // from TBL:: at file scope.
    {
        // Scale cycler — current preset's label (e.g. "85%"). No chevron.
        wchar_t scaleBuf[16] = L"--";
        for (auto& p : g_scalePresets) {
            if (p.mul == g_cfg.uiScale) {
                lstrcpynW(scaleBuf, p.label, 16);
                break;
            }
        }
        PaintToolbarControl(g, g_scaleDropdownRect,
                            L"Scale", TBL::SCALE_LABEL_W,
                            scaleBuf, nullptr, FontStyleRegular, nullptr,
                            /*showChevron*/ false,
                            /*cycleHint  */ false);

        // 3-state toggle slider beneath the Scale row. The asset family
        // is btn_toggle1.png / btn_toggle2.png / btn_toggle3.png — one
        // per state, picking which the slider knob appears at. Native
        // size is 411×182; we render at 53×23 logical (~12% — a small,
        // unobtrusive control). If the asset for the current state is
        // missing (user is still authoring the variants), fall back to
        // a programmatic track + marker so the control is still
        // clickable even when its art is incomplete.
        {
            int state = ScaleToggleState();
            const wchar_t* assetName =
                (state == 0) ? L"btn_toggle1.png" :
                (state == 1) ? L"btn_toggle2.png" :
                               L"btn_toggle3.png";
            Gdiplus::Bitmap* asset = AssetImage(assetName);
            const RECT& sr = g_scaleSliderRect;
            int sw = sr.right - sr.left;
            int sh = sr.bottom - sr.top;
            if (asset) {
                InterpolationMode prev = g.GetInterpolationMode();
                g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                g.DrawImage(asset, sr.left, sr.top, sw, sh);
                g.SetInterpolationMode(prev);
            } else {
                // Fallback: thin gold track with a circular marker at
                // the active position. Keeps the control discoverable
                // while btn_toggle2/3.png are still in production.
                int trackY = sr.top + sh / 2 - 1;
                Pen track(Tok::BronzeBright, 2.0f);
                g.DrawLine(&track,
                           (INT)(sr.left + 4),  trackY,
                           (INT)(sr.right - 4), trackY);
                int markerW = sh - 4;
                int slot0X = sr.left + 4;
                int slot2X = sr.right - 4 - markerW;
                int markerX = slot0X + ((slot2X - slot0X) * state) / 2;
                SolidBrush markerFill(Tok::GoldBright);
                g.FillEllipse(&markerFill, markerX, sr.top + 2, markerW, markerW);
                Pen markerBorder(Tok::Bronze, 1.0f);
                g.DrawEllipse(&markerBorder, markerX, sr.top + 2, markerW, markerW);
            }
        }

        // Font dropdown — show the abbreviated name of the currently
        // selected face (e.g. "Cin-Bol"). Rendering each face in its
        // own typography was scrapped: Cinzel-Regular and Cinzel-Bold
        // both report family "Cinzel" via TTF metadata, and even with
        // style-from-filename derivation the rendered weights came out
        // identical because the OS lookup didn't separate the two
        // variants under FR_PRIVATE. The abbreviated label is
        // unambiguous and fits in the value box.
        const wchar_t* fontAbbrev = nullptr;
        if (!g_cfg.fontName.empty()) {
            for (size_t i = 0; i < g_availableFonts.size(); ++i) {
                if (g_availableFonts[i] == g_cfg.fontName) {
                    if (i < g_availableAbbrevs.size()
                        && !g_availableAbbrevs[i].empty()) {
                        fontAbbrev = g_availableAbbrevs[i].c_str();
                    }
                    break;
                }
            }
        }
        PaintToolbarControl(g, g_fontDropdownRect,
                            L"Font", TBL::FONT_LABEL_W,
                            fontAbbrev,
                            nullptr,                  // no per-face render
                            FontStyleRegular,
                            nullptr,
                            /*showChevron*/ true);

        // Colour dropdown — current preset as a small square chip.
        if (g_cfg.fontColorIdx >= 0 &&
            g_cfg.fontColorIdx < (int)(sizeof(g_colorPresets)/sizeof(g_colorPresets[0]))) {
            const ColorPreset& cp = g_colorPresets[g_cfg.fontColorIdx];
            PaintToolbarControl(g, g_colorDropdownRect,
                                L"Colour", TBL::COLOR_LABEL_W,
                                nullptr, nullptr, FontStyleRegular, &cp.rgb,
                                /*showChevron*/ true);
        } else {
            // No selection yet — show the default Gold chip.
            COLORREF defGold = RGB(0xE8, 0xC2, 0x5E);
            PaintToolbarControl(g, g_colorDropdownRect,
                                L"Colour", TBL::COLOR_LABEL_W,
                                nullptr, nullptr, FontStyleRegular, &defGold,
                                /*showChevron*/ true);
        }

        // ── On Launch column ────────────────────────────────────────────
        // Header: just "ON LAUNCH" rendered as plain text (no chrome).
        // Same right-aligned style as the other toolbar labels for
        // consistency, except aligned LEFT here so it reads as a
        // column heading sitting above its textbox.
        {
            StringFormat sfHdr;
            sfHdr.SetAlignment(StringAlignmentNear);
            sfHdr.SetLineAlignment(StringAlignmentCenter);
            sfHdr.SetFormatFlags(sfHdr.GetFormatFlags() | StringFormatFlagsNoWrap);
            SolidBrush hdrBr(Tok::TextParchment);
            g.DrawString(L"On Launch", -1, g_fBtn,
                         RectF((REAL)g_onLaunchHeaderRect.left,
                               (REAL)g_onLaunchHeaderRect.top,
                               (REAL)(g_onLaunchHeaderRect.right
                                      - g_onLaunchHeaderRect.left),
                               (REAL)(g_onLaunchHeaderRect.bottom
                                      - g_onLaunchHeaderRect.top)),
                         &sfHdr, &hdrBr);
        }

        // Value-only textbox — no inline label (the "ON LAUNCH" header
        // above is the label). labelW=0 makes PaintToolbarControl skip
        // its label tier and use the full rect width for the value box.
        PaintToolbarControl(g, g_onLaunchRect,
                            L"", 0,
                            OnLaunchStateLabel(),
                            nullptr, FontStyleRegular, nullptr,
                            /*showChevron*/ false,
                            /*cycleHint  */ false);

        // On Launch slider — same asset family (btn_toggle1/2/3.png),
        // rendered at the same 53×23 logical size. Falls back to a
        // programmatic track + marker when the per-state asset is
        // missing (matches what the Scale slider does for the same
        // reason — keeps the control discoverable while assets are
        // still being authored).
        {
            int olState = OnLaunchSliderState();
            const wchar_t* olAssetName =
                (olState == 0) ? L"btn_toggle1.png" :
                (olState == 1) ? L"btn_toggle2.png" :
                                 L"btn_toggle3.png";
            Gdiplus::Bitmap* olAsset = AssetImage(olAssetName);
            const RECT& osr = g_onLaunchSliderRect;
            int osw = osr.right - osr.left;
            int osh = osr.bottom - osr.top;
            if (olAsset) {
                InterpolationMode prev = g.GetInterpolationMode();
                g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                g.DrawImage(olAsset, osr.left, osr.top, osw, osh);
                g.SetInterpolationMode(prev);
            } else {
                int trackY = osr.top + osh / 2 - 1;
                Pen track(Tok::BronzeBright, 2.0f);
                g.DrawLine(&track,
                           (INT)(osr.left + 4),  trackY,
                           (INT)(osr.right - 4), trackY);
                int markerW = osh - 4;
                int slot0X = osr.left + 4;
                int slot2X = osr.right - 4 - markerW;
                int markerX = slot0X + ((slot2X - slot0X) * olState) / 2;
                SolidBrush markerFill(Tok::GoldBright);
                g.FillEllipse(&markerFill, markerX, osr.top + 2, markerW, markerW);
                Pen markerBorder(Tok::Bronze, 1.0f);
                g.DrawEllipse(&markerBorder, markerX, osr.top + 2, markerW, markerW);
            }
        }
    }

    PaintBottomPanel(g, W, H);

    // ── Custom title-bar buttons (minimize + close) ─────────────────────
    // Drawn LAST so they paint over any panel fills that touch the top
    // filigree band. The MOD DESCRIPTION panel begins at insetT=38, which
    // overlaps the bottom of the buttons — without this final pass the
    // panel's solid fill would cover them. Single asset per button; the
    // hover-grows / click-shrinks effect is produced by a GDI+ transform
    // applied at paint time (matching the regular buttons).
    {
        const wchar_t* names[2] = {
            L"btn_minimize.png",
            L"btn_close.png",
        };
        for (int i = 0; i < 2; ++i) {
            Gdiplus::Bitmap* bm = AssetImage(names[i]);
            if (!bm) continue;
            int rightOfClose = W - TB_BTN_INSET_R;
            int closeX = rightOfClose - TB_BTN_W;
            int minX   = closeX - TB_BTN_GAP - TB_BTN_W;
            int x = (i == 0) ? minX : closeX;
            int y = TB_BTN_INSET_T;

            bool isPressed = (g_tbPressed == i);
            // Hover no longer scales — only the click-shrink applies. The
            // hover state itself is still tracked elsewhere (cursor, hit-
            // test) but produces no visible size change here.
            if (isPressed) {
                Gdiplus::Matrix prev;
                g.GetTransform(&prev);
                REAL cx = (REAL)(x + TB_BTN_W * 0.5f);
                REAL cy = (REAL)(y + TB_BTN_H * 0.5f);
                g.TranslateTransform(cx + 1.0f, cy + 1.0f);
                g.ScaleTransform(0.90f, 0.90f);
                g.TranslateTransform(-cx, -cy);
                DrawAssetAt(g, bm, x, y);
                g.SetTransform(&prev);
            } else {
                DrawAssetAt(g, bm, x, y);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────
//  ZIP INSTALL  (drag-and-drop mod extractor)
// ─────────────────────────────────────────────────────────────────────────
//
// Flow per dropped .zip:
//   1. Worker thread pops a path off g_zipQueue.
//   2. Extract zip to a unique temp directory using Windows' bundled
//      tar.exe (BSD tar, supports zip since Win10 1803).
//   3. BFS the temp tree looking for modinfo.json; the shallowest hit
//      defines the "mod root" — everything beneath it is the install
//      payload, matching what D2R / the launcher expect under
//      <D2R>\mods\<modname>\.
//   4. Read the "name" field from modinfo.json (fallback: the folder
//      name inside the zip). Sanitize for filesystem.
//   5. If <D2R>\mods\<name> already exists, SendMessage the main UI
//      thread to show a themed Update/Overwrite/Cancel dialog. The
//      worker blocks during the SendMessage round-trip; main thread
//      runs the modal locally.
//   6. Apply the user's chosen action; delete the temp tree; loop.
//   7. When the queue drains, post MSG_ZIP_QUEUE_DONE to trigger a
//      single RefreshMods + repaint.
//
// Multiple zips can be queued (drop several at once); each gets its own
// dialog if it collides. Cancel just skips that zip and moves to the next.

static atomic<bool> g_zipWorkerRunning(false);
static std::mutex   g_zipQueueMutex;
static vector<wstring> g_zipQueue;   // protected by g_zipQueueMutex
static HWND        g_progressDlg = nullptr;   // active progress dialog, or null
                                              // — declared here (not inside the
                                              // dialog block below) so MainProc's
                                              // message handlers can read it
                                              // when parenting modal child
                                              // dialogs to the progress window.

// Progress dialog content state. Used by the zip-install flow only —
// the launcher self-update has its own popup with its own state
// (see g_lupopup* further down).
static int     g_progressStage      = 0;
static int     g_progressZipIdx     = 0;     // 1-based
static int     g_progressZipTotal   = 0;
static wstring g_progressZipName;             // current archive filename
static wstring g_progressStageLabel;          // "Extracting archive...", etc.


// Strip filesystem-reserved characters from a mod name. Spaces and dots
// are allowed mid-string but trimmed off the trailing edge (Windows
// rejects directory names ending in either).
static wstring SanitizeModName(const wstring& name) {
    static const wchar_t* bad = L"<>:\"/\\|?*";
    wstring out;
    out.reserve(name.size());
    for (wchar_t c : name) {
        if (c < 0x20) continue;
        bool isBad = false;
        for (const wchar_t* p = bad; *p; ++p) if (*p == c) { isBad = true; break; }
        if (!isBad) out.push_back(c);
    }
    while (!out.empty() && (out.back() == L' ' || out.back() == L'.')) out.pop_back();
    return out;
}

static bool ZI_DirExists(const wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool ZI_FileExists(const wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// %TEMP%\angiris_install_<GUID-ish>. Unique per call so concurrent zip
// installs (if we ever go there) don't collide.
static wstring MakeTempInstallDir() {
    wchar_t base[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, base);
    if (n == 0 || n >= MAX_PATH) return L"";
    GUID g; CoCreateGuid(&g);
    wchar_t out[MAX_PATH];
    int len = swprintf_s(out, MAX_PATH,
                         L"%sangiris_install_%08lX_%04hX",
                         base, g.Data1, g.Data2);
    if (len <= 0) return L"";
    if (!CreateDirectoryW(out, nullptr)) return L"";
    return out;
}

// Recursive delete of a folder via SHFileOperation. Used for both temp
// cleanup and for the "Overwrite" branch (where we wipe and reinstall).
// The double-null-terminated pFrom is mandatory — SHFileOp won't accept
// the path otherwise.
static void DeleteFolderRecursive(const wstring& path) {
    if (path.empty()) return;
    vector<wchar_t> from(path.begin(), path.end());
    from.push_back(0);
    from.push_back(0);
    SHFILEOPSTRUCTW op = {};
    op.wFunc  = FO_DELETE;
    op.pFrom  = from.data();
    op.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    SHFileOperationW(&op);
}

// Invoke tar.exe to extract a zip. Hidden window (CREATE_NO_WINDOW), wait
// for completion, return true on exit code 0. Failure cases: tar missing,
// zip corrupt, destination unwritable.
// Runs the Windows-bundled bsdtar to extract `zipPath` into `destDir`.
// Optional `excludePattern` (e.g. L"*.ttf") is passed straight through
// as bsdtar's --exclude flag so matching entries are skipped at the
// extraction stage rather than copied and then ignored. Returns true
// iff tar exited zero.
static bool RunTarExtract(const wstring& zipPath, const wstring& destDir,
                          const wchar_t* excludePattern = nullptr) {
    wchar_t winDir[MAX_PATH];
    if (GetWindowsDirectoryW(winDir, MAX_PATH) == 0) return false;
    wstring tarPath = wstring(winDir) + L"\\System32\\tar.exe";
    if (!ZI_FileExists(tarPath)) return false;

    wstring cmd = L"\"" + tarPath + L"\" -xf \"" + zipPath
                + L"\" -C \"" + destDir + L"\"";
    if (excludePattern && *excludePattern) {
        // bsdtar accepts --exclude=PATTERN as a single argv; CreateProcessW
        // doesn't glob-expand so `*.ttf` is passed literally to bsdtar,
        // which then matches it against each archive entry's path.
        cmd += L" \"--exclude=";
        cmd += excludePattern;
        cmd += L"\"";
    }

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    vector<wchar_t> cmdLine(cmd.begin(), cmd.end());
    cmdLine.push_back(0);

    if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &si, &pi)) {
        return false;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exitCode == 0;
}

// BFS for the shallowest modinfo.json in a tree. Picking shallowest
// handles archives that nest the mod folder one level deep (a very
// common layout — the zip contains MyMod/modinfo.json, MyMod/data/...).
static wstring FindModinfoJson(const wstring& root) {
    vector<wstring> queue;
    queue.push_back(root);
    size_t qi = 0;
    while (qi < queue.size()) {
        wstring dir = queue[qi++];
        WIN32_FIND_DATAW fd;
        wstring pattern = dir + L"\\*";
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        wstring foundFile;
        do {
            if (wcscmp(fd.cFileName, L".")  == 0) continue;
            if (wcscmp(fd.cFileName, L"..") == 0) continue;
            wstring full = dir + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                queue.push_back(full);
            } else if (_wcsicmp(fd.cFileName, L"modinfo.json") == 0) {
                foundFile = full;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        if (!foundFile.empty()) return foundFile;
    }
    return L"";
}

// Quick read of "name" (then "title" as fallback) from a UTF-8 modinfo.json.
static wstring ReadModNameFromInfo(const wstring& modinfoPath) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, modinfoPath.c_str(), L"rb") != 0 || !f) return L"";
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 10 * 1024 * 1024) { fclose(f); return L""; }
    string utf8(sz, 0);
    size_t got = fread(utf8.data(), 1, sz, f);
    fclose(f);
    utf8.resize(got);
    int wn = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                                 nullptr, 0);
    if (wn <= 0) return L"";
    wstring j(wn, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                        j.data(), wn);
    wstring name = JsonStr(j, L"name");
    if (name.empty()) name = JsonStr(j, L"title");
    return name;
}

// Recursive tree copy with two modes:
//   addMissing=true   → standard recursive copy (mkdir as needed, copy
//                       every file). Used by Overwrite (after wipe) and
//                       by fresh-install.
//   addMissing=false  → "Update" mode: only copy files whose same
//                       relative path EXISTS in dst. Doesn't create new
//                       subdirectories; doesn't add new files. The strict
//                       reading of "Update only overwrites files found in
//                       the archive that are also found in the folder".
static bool CopyTreeInto(const wstring& src, const wstring& dst, bool addMissing) {
    WIN32_FIND_DATAW fd;
    wstring pattern = src + L"\\*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;   // empty src is OK
    bool ok = true;
    do {
        if (wcscmp(fd.cFileName, L".")  == 0) continue;
        if (wcscmp(fd.cFileName, L"..") == 0) continue;
        wstring s = src + L"\\" + fd.cFileName;
        wstring d = dst + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            bool dstHas = ZI_DirExists(d);
            if (!dstHas) {
                if (addMissing) {
                    CreateDirectoryW(d.c_str(), nullptr);
                    if (!CopyTreeInto(s, d, true)) ok = false;
                }
                // else (Update mode): destination doesn't have this dir,
                // skip its entire subtree.
            } else {
                if (!CopyTreeInto(s, d, addMissing)) ok = false;
            }
        } else {
            if (addMissing || ZI_FileExists(d)) {
                // CopyFileW with FALSE = overwrite existing
                if (!CopyFileW(s.c_str(), d.c_str(), FALSE)) ok = false;
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────
//  SAVE BACKUP
// ─────────────────────────────────────────────────────────────────────────
//
// D2R writes each mod's character + stash data to
//   %USERPROFILE%\Saved Games\Diablo II Resurrected\Mods\<savepath>\
// where <savepath> is the value of the "savepath" field in the mod's
// modinfo.json (required by Blizzard's schema). A backup snapshot is a
// full copy of that folder into:
//   <savefolder>\backups\<YYYY-MM-DD_HHMMSS>\
// keeping the backups co-located with the data they protect. The
// `backups\` subfolder itself is excluded from the snapshot so we
// don't recurse into the backup tree we're building.
//
// Triggered manually (right-click context menu) and automatically
// before any Overwrite install (where the destination's existing mod
// folder is about to be wiped). Rotation keeps the most recent five
// snapshots — older ones are deleted as new ones are created.

// Variant of CopyTreeInto that skips entries named `excludeName` at the
// SOURCE ROOT only (deeper occurrences are copied normally). Used to
// snapshot a save folder while excluding its own `backups\` subfolder.
static bool CopyTreeExcept(const wstring& src, const wstring& dst,
                           const wchar_t* excludeName) {
    WIN32_FIND_DATAW fd;
    wstring pattern = src + L"\\*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;
    bool ok = true;
    do {
        if (wcscmp(fd.cFileName, L".")  == 0) continue;
        if (wcscmp(fd.cFileName, L"..") == 0) continue;
        if (excludeName &&
            _wcsicmp(fd.cFileName, excludeName) == 0) continue;
        wstring s = src + L"\\" + fd.cFileName;
        wstring d = dst + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CreateDirectoryW(d.c_str(), nullptr);
            // Recurse with the existing copy helper — exclusion only
            // applies at the top level, deeper trees copy normally.
            if (!CopyTreeInto(s, d, true)) ok = false;
        } else {
            if (!CopyFileW(s.c_str(), d.c_str(), FALSE)) ok = false;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

// Keep the most recent `keep` timestamped subfolders in `backupsRoot`;
// delete the rest. Names follow the YYYY-MM-DD_HHMMSS pattern which
// sorts correctly as plain strings, so a lexicographic sort puts the
// oldest at the front of the list.
static void RotateBackups(const wstring& backupsRoot, int keep) {
    vector<wstring> entries;
    WIN32_FIND_DATAW fd;
    wstring pattern = backupsRoot + L"\\*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".")  == 0) continue;
        if (wcscmp(fd.cFileName, L"..") == 0) continue;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        entries.emplace_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(entries.begin(), entries.end());
    while ((int)entries.size() > keep) {
        wstring victim = backupsRoot + L"\\" + entries.front();
        DeleteFolderRecursive(victim);
        entries.erase(entries.begin());
    }
}

// Resolve <savedGames>\Diablo II Resurrected\Mods\<savePath> for the
// given savepath. Uses %USERPROFILE%\Saved Games as the base — that's
// where D2R itself writes, and matches the location across every
// Windows version we care about. Returns empty if USERPROFILE isn't
// set (shouldn't happen on a real Windows session).
static wstring ResolveModSaveFolder(const wstring& savePath) {
    if (savePath.empty()) return L"";
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    return wstring(buf) + L"\\Saved Games\\Diablo II Resurrected\\Mods\\"
                        + savePath;
}

// Create a timestamped snapshot of the mod's save folder. Returns true
// + writes the new backup folder path into *outBackupDir on success;
// returns false if the save folder doesn't exist (nothing to back up)
// or if the copy failed. Rotates so only the 5 most recent snapshots
// survive.
static bool BackupModSavesByPath(const wstring& savePath,
                                 wstring* outBackupDir) {
    if (outBackupDir) outBackupDir->clear();
    wstring saveDir = ResolveModSaveFolder(savePath);
    if (saveDir.empty()) return false;
    if (!ZI_DirExists(saveDir)) return false;   // no saves to back up

    wstring backupsRoot = saveDir + L"\\backups";
    if (!ZI_DirExists(backupsRoot)) {
        if (!CreateDirectoryW(backupsRoot.c_str(), nullptr)) return false;
    }

    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t ts[64];
    swprintf_s(ts, 64, L"%04d-%02d-%02d_%02d%02d%02d",
               t.wYear, t.wMonth, t.wDay,
               t.wHour, t.wMinute, t.wSecond);
    wstring backupDir = backupsRoot + L"\\" + ts;
    if (!CreateDirectoryW(backupDir.c_str(), nullptr)) {
        // Almost certainly a duplicate timestamp (two backups within
        // the same second). Fall back by appending an index suffix.
        for (int i = 1; i < 100; ++i) {
            wchar_t suffix[16]; swprintf_s(suffix, 16, L"_%02d", i);
            backupDir = backupsRoot + L"\\" + ts + suffix;
            if (CreateDirectoryW(backupDir.c_str(), nullptr)) goto made;
        }
        return false;
    made:;
    }

    bool ok = CopyTreeExcept(saveDir, backupDir, L"backups");
    RotateBackups(backupsRoot, 5);
    if (ok && outBackupDir) *outBackupDir = backupDir;
    return ok;
}

// Convenience wrapper: read savepath out of a modinfo.json on disk
// and back up that mod's saves. Used by the zip installer's
// auto-backup-before-Overwrite path, where the existing mod is about
// to be deleted and we want its saves protected first.
static bool BackupModSavesFromModinfo(const wstring& modinfoPath,
                                      wstring* outBackupDir) {
    if (outBackupDir) outBackupDir->clear();
    wstring json = ReadTextFile(modinfoPath);
    if (json.empty()) return false;
    wstring sp = JsonStr(json, L"savepath");
    if (sp.empty()) return false;
    return BackupModSavesByPath(sp, outBackupDir);
}

// Forward decls — defined after MainProc so they can directly call into
// the existing WM_DRAWITEM handler for the dialogs' owner-drawn buttons.
// MkStdBtn lives even further down (CONTROL CREATION section), so we
// forward-declare it too — the dialogs create their buttons through it.
// Defaults stay on the definition only (C++ only allows defaults to be
// specified once per signature); dialog call sites pass all 9 args.
static HWND MkStdBtn(HWND parent, const wchar_t* lbl, int id,
                     int x, int y, int w, int h, bool visible,
                     ButtonKind kind);
static int  ShowConflictDialog(HWND parent, const wstring& modName);
static int  ShowSetPathDialog(HWND parent);
static void ShowNoModInfoDialog(HWND parent, const wstring& zipName);
static int  ShowUninstallConfirmDialog(HWND parent, const wstring& modName);
static HWND ShowProgressDialog(HWND parent);
static void UpdateProgressDialog(const ProgressUpdate& p);
static void HideProgressDialog();

// Extract the filename portion of a path — used for the progress
// dialog body and the no-modinfo error message.
static wstring BaseName(const wstring& path) {
    size_t s = path.find_last_of(L"\\/");
    return (s == wstring::npos) ? path : path.substr(s + 1);
}

// Synchronous progress push to the UI thread. Wraps the SendMessage so
// the worker's call sites stay readable. The UI thread updates the
// progress globals + repaints + returns; the SendMessage blocks until
// the new stage is on screen.
static void PushProgress(int stage, int zipIdx, int zipTotal,
                         const wstring& zipName,
                         const wchar_t* stageLabel) {
    if (!g_hwMain) return;
    ProgressUpdate p;
    p.stage      = stage;
    p.zipIdx     = zipIdx;
    p.zipTotal   = zipTotal;
    p.zipName    = zipName;
    p.stageLabel = stageLabel ? stageLabel : L"";
    SendMessageW(g_hwMain, MSG_ZIP_PROGRESS_UPDATE, 0, (LPARAM)&p);
}

// ─────────────────────────────────────────────────────────────────────────
//  LAUNCHER SELF-UPDATE
// ─────────────────────────────────────────────────────────────────────────
//
// Flow:
//   1. Startup kicks off LauncherUpdateCheckWorker on a background
//      thread. The worker hits GitHub's releases-latest API, extracts
//      `tag_name` and the first `.zip` asset URL, parks them in
//      g_launcherUpdate* globals, and PostMessage's MainProc.
//   2. MainProc compares the tag against LAUNCHER_VERSION and
//      g_cfg.skippedLauncherVersion. If newer and not skipped, shows
//      the themed update dialog (Update / Skip Version / Ignore).
//   3. On Update, StartLauncherUpdateInstall runs the download +
//      extract + self-rename on a worker thread, advancing the
//      progress dialog through stages 0/2/4. At stage 4 the user
//      clicks Restart to close the running launcher and spawn the
//      newly-installed exe.
//   4. On Skip Version, we save the tag to cfg so it doesn't prompt
//      again until something even newer drops.
//   5. On Ignore, we do nothing — the user gets re-prompted next launch.
//
// The whole feature is silent on any failure (network down, GitHub
// unreachable, malformed release, etc.) so a flaky internet
// connection at startup doesn't ever pop a scary error dialog.
//
// State globals (g_launcherUpdate*, g_forceUpdatePrompt,
// g_versionLabelRect) live near the LAUNCHER_VERSION / GitHub
// constants up top — PaintBody references some of them, and it sits
// before this block in the file, so forward declarations would need
// to land there anyway.

// Forward decl — the dialog proc lives down with the other themed
// dialogs (after MainProc), but MainProc's handler needs to call it.
static int ShowLauncherUpdateDialog(HWND parent, const wstring& latestTag);
// Kicks off the install: shows the progress dialog at stage 0 and
// spawns LauncherUpdateInstallWorker on a background thread. Returns
// immediately so the UI stays responsive while the install runs.
static void StartLauncherUpdateInstall(HWND parent);

// Scan the GitHub API response body for a release asset URL that
// looks like a release zip. Strategy: walk every
// "browser_download_url" field in the JSON; first one whose value
// ends in .zip wins. If no .zip is found, fall back to the first
// URL we saw. Crude but robust enough for a single-asset release
// flow — we're not parsing the array structure, just substring
// matching, which keeps us free of nested-object parsing.
static wstring FindReleaseZipUrl(const wstring& json) {
    const wstring key = L"\"browser_download_url\"";
    wstring firstAny;
    size_t pos = 0;
    while (true) {
        size_t k = json.find(key, pos);
        if (k == wstring::npos) break;
        size_t colon = json.find(L':',  k);
        if (colon == wstring::npos) break;
        size_t q1    = json.find(L'"',  colon);
        if (q1 == wstring::npos) break;
        size_t q2    = json.find(L'"',  q1 + 1);
        if (q2 == wstring::npos) break;
        wstring url = json.substr(q1 + 1, q2 - q1 - 1);
        if (firstAny.empty()) firstAny = url;
        if (url.size() >= 4
            && _wcsicmp(url.c_str() + url.size() - 4, L".zip") == 0) {
            return url;
        }
        pos = q2 + 1;
    }
    return firstAny;
}

// Wakes once at startup, fires off the API request, posts the
// "update available" message if the server confirmed a release. Pure
// network code — no UI, no g_cfg writes — runs entirely on its own
// thread so the launcher window comes up instantly even on a slow
// network.
//
// Posts the message whenever the API returned a tag, regardless of
// whether a .zip asset was attached. The download URL may legitimately
// be missing — release tagged but no binary uploaded yet, or only
// non-zip assets attached — and we want the user to see the dialog
// anyway. StartLauncherUpdateInstall handles the missing-URL case by
// opening the GitHub releases page in their browser, so the Update
// button always has a fallback action.
//
// On every run, writes a diagnostic to <appdir>\assets\last_update_check.log
// covering HTTP status, parsed tag, parsed download URL, and (when the
// response is small enough) the raw body. Open that file to debug why
// the dialog didn't appear or why the Update button fell back to the
// browser.
static DWORD WINAPI LauncherUpdateCheckWorker(LPVOID) {
    wstring apiUrl = wstring(L"https://api.github.com/repos/")
                   + LAUNCHER_GITHUB_OWNER + L"/" + LAUNCHER_GITHUB_REPO
                   + L"/releases/latest";
    HttpResult r = HttpGet(apiUrl, 8000);

    wstring tag, url;
    if (r.status == 200 && !r.body.empty()) {
        tag = JsonStr(r.body, L"tag_name");
        url = FindReleaseZipUrl(r.body);
        if (!tag.empty()) {
            g_launcherUpdateLatestTag   = tag;
            g_launcherUpdateDownloadUrl = url;
            if (g_hwMain)
                PostMessageW(g_hwMain, MSG_LAUNCHER_UPDATE_AVAILABLE, 0, 0);
        }
    }

    // Diagnostic log — overwritten every run, so it always reflects
    // the most recent check. Lives at <appdir>\assets\last_update_check.log.
    {
        wstring logPath = AppDir() + L"\\assets\\last_update_check.log";
        FILE* f = nullptr;
        _wfopen_s(&f, logPath.c_str(), L"w, ccs=UTF-8");
        if (f) {
            time_t t = time(nullptr);
            struct tm tm_;
            localtime_s(&tm_, &t);
            wchar_t timeStr[64];
            wcsftime(timeStr, 64, L"%Y-%m-%d %H:%M:%S", &tm_);

            fwprintf(f, L"=== Launcher update check ===\n");
            fwprintf(f, L"Time:           %ls\n", timeStr);
            fwprintf(f, L"Current ver:    %ls\n", LAUNCHER_VERSION);
            fwprintf(f, L"API URL:        %ls\n", apiUrl.c_str());
            fwprintf(f, L"HTTP status:    %d\n", r.status);
            fwprintf(f, L"Timed out:      %ls\n", r.timedOut ? L"yes" : L"no");
            fwprintf(f, L"Body length:    %zu chars\n", r.body.size());
            fwprintf(f, L"Parsed tag:     %ls\n",
                     tag.empty() ? L"(none — JSON parse failed or no tag_name)" : tag.c_str());
            fwprintf(f, L"Parsed URL:     %ls\n",
                     url.empty() ? L"(none — no asset with .zip extension found)" : url.c_str());
            fwprintf(f, L"\n");
            if (r.body.size() <= 32768) {
                fwprintf(f, L"--- Response body ---\n");
                fwprintf(f, L"%ls\n", r.body.c_str());
            } else {
                fwprintf(f, L"--- Response body (truncated, first 32KB) ---\n");
                fwprintf(f, L"%.*s\n", 32768, r.body.c_str());
            }
            fclose(f);
        }
    }

    g_launcherUpdateCheckRunning = false;
    return 0;
}

static void KickoffLauncherUpdateCheck() {
    if (g_launcherUpdateCheckRunning.exchange(true)) return;
    HANDLE h = CreateThread(nullptr, 0, LauncherUpdateCheckWorker,
                            nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    else   g_launcherUpdateCheckRunning = false;
}

// Tries once to delete Angiris.exe.old (cheap no-op when there isn't one).
// If the file exists but can't be deleted yet — typically because the
// prior launcher process is still shutting down and Windows still has
// its image file mapped — populates g_pendingOldExeDelete so the main
// window's WM_TIMER handler can keep retrying every 2s for ~30s total.
//
// Called once at wWinMain entry. The deferred retry is then armed by
// StartDeferredOldExeCleanup() once g_hwMain exists.
static void CleanupLauncherOldExe() {
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return;
    wstring oldExe = wstring(exePath) + L".old";

    // Fast path: check existence first so the common "no .old here"
    // case doesn't even attempt a delete.
    DWORD attrs = GetFileAttributesW(oldExe.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return;   // nothing to clean

    // Try a quick delete. If the file was orphaned from a long-ago
    // session (no prior process is holding it), this succeeds
    // immediately.
    if (DeleteFileW(oldExe.c_str())) return;
    DWORD err = GetLastError();
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return;

    // A few quick retries — covers the case where the prior process
    // just exited and the OS needs a beat. After ~600ms total, give up
    // and hand off to the deferred timer.
    for (int i = 0; i < 3; ++i) {
        Sleep(200);
        if (DeleteFileW(oldExe.c_str())) return;
        err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return;
    }

    // Still locked. Queue for the deferred retry — the main window
    // timer will keep at it for ~30 more seconds while the prior
    // launcher (which is almost certainly the holder) finishes
    // shutting down.
    g_pendingOldExeDelete   = oldExe;
    g_cleanupOldExeAttempts = 0;
}

// Arm the deferred .old cleanup timer if a pending delete is queued.
// Called from wWinMain after g_hwMain is created (timers need a window
// to deliver WM_TIMER to). No-op when CleanupLauncherOldExe already
// succeeded synchronously or there was no .old to begin with.
static void StartDeferredOldExeCleanup() {
    if (g_pendingOldExeDelete.empty()) return;
    if (!g_hwMain) return;
    SetTimer(g_hwMain, IDT_CLEANUP_OLD_EXE, 2000, nullptr);
}

// Writes a one-line entry to assets\last_update_install.log explaining
// why the deferred cleanup gave up. Called only when MoveFileEx is the
// last resort.
static void LogCleanupOldExeGaveUp(const wstring& oldExe, DWORD lastErr) {
    wstring logPath = AppDir() + L"\\assets\\last_update_install.log";
    FILE* f = nullptr;
    _wfopen_s(&f, logPath.c_str(), L"a, ccs=UTF-8");
    if (!f) return;
    time_t t = time(nullptr);
    struct tm tm_;
    localtime_s(&tm_, &t);
    wchar_t ts[32];
    wcsftime(ts, 32, L"%Y-%m-%d %H:%M:%S", &tm_);
    fwprintf(f,
        L"[%ls] CleanupLauncherOldExe: deferred retry exhausted for %ls "
        L"(err=%lu). Scheduled for delete on next reboot via MoveFileEx.\n",
        ts, oldExe.c_str(), lastErr);
    fclose(f);
}

// BFS for the shallowest Angiris.exe in the extracted release. Mirrors
// FindModinfoJson — release zips may wrap the payload in a
// "Angiris-vX.Y" folder, so we don't assume a flat layout.
static wstring FindAngirisExeInTree(const wstring& root) {
    vector<wstring> queue;
    queue.push_back(root);
    size_t qi = 0;
    while (qi < queue.size()) {
        wstring dir = queue[qi++];
        WIN32_FIND_DATAW fd;
        wstring pattern = dir + L"\\*";
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        wstring foundFile;
        do {
            if (wcscmp(fd.cFileName, L".")  == 0) continue;
            if (wcscmp(fd.cFileName, L"..") == 0) continue;
            wstring full = dir + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                queue.push_back(full);
            } else if (_wcsicmp(fd.cFileName, L"Angiris.exe") == 0) {
                foundFile = full;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        if (!foundFile.empty()) return foundFile;
    }
    return L"";
}

// The in-place update sequence — the core trick that makes this work
// without a separate updater binary:
//
//   1. Make temp dir, download zip, extract via tar.exe.
//   2. BFS for Angiris.exe in the extracted tree to determine the
//      release root (parent of the new exe). Handles releases that
//      wrap payload in a "Angiris-vX.Y" subfolder.
//   3. Get the running exe's path via GetModuleFileNameW. Its parent
//      is the install dir.
//   4. RENAME the running exe to "<path>.exe.old". Windows allows
//      renaming a running executable even though it forbids deleting
//      or overwriting one. After the rename, the original path is
//      free for new content.
//   5. CopyTreeInto(releaseRoot → installDir, overwriting). The new
//      Angiris.exe lands at the original path; updated asset files
//      overwrite their old versions (asset files aren't held open).
//   6. CreateProcess the new exe with the install dir as CWD.
//   7. PostQuitMessage so the old process exits, letting the new one
//      take over. On its next startup it'll find the .old file and
//      DeleteFileW it via CleanupLauncherOldExe.
//
// Returns true if we got far enough to launch the new exe. On failure
// at any earlier step the in-place state is rolled back as best we
// can (move the .old back to .exe if the rename succeeded) and the
// function returns false. The caller doesn't display errors — failed
// updates are silent so a corrupt release doesn't blow up the user's
// next launcher session.
// ═══════════════════════════════════════════════════════════════════════
//  LAUNCHER UPDATE POPUP (simple text-only window)
// ═══════════════════════════════════════════════════════════════════════
//
// Why a separate window instead of reusing the zip-install progress
// dialog: the launcher self-update needs to overwrite files (PNG
// assets, fonts, the .exe) that are *locked* by the running launcher
// process. GDI+ holds a file handle for every cached Bitmap, and the
// font collection holds handles to every font file we loaded. The
// only way to free those handles is to tear down the cache and the
// font collection — but if we do that while the main GUI is still
// using them, the next paint crashes.
//
// So the install flow now does this:
//
//   1. User clicks Update.
//   2. The main window is hidden (ShowWindow SW_HIDE) — it won't
//      paint again, so its Font*/Bitmap* references are dormant.
//   3. The asset cache and font collection are torn down to release
//      all the file handles.
//   4. A NEW minimal popup is shown. It uses only system resources
//      (Segoe UI, GDI primitives) so it doesn't depend on anything
//      we just released.
//   5. The worker runs the actual install — file copies succeed
//      because nothing is locked anymore.
//   6. The worker sleeps 1 second after install (per UX request, to
//      let the filesystem settle) before signalling "Complete".
//   7. The Restart button enables ~500 ms after "Complete" appears
//      (timer-driven; prevents accidental immediate clicks).
//   8. Restart button click: spawn new exe + PostMessage WM_CLOSE
//      on the (hidden) main window to exit cleanly.

static HWND  g_lupopupWnd          = nullptr;
static HWND  g_lupopupRestartBtn   = nullptr;
static int   g_lupopupStatus       = 0;       // 0=init, 1=DL, 2=updating, 3=complete
static DWORD g_lupopupCompleteTick = 0;       // GetTickCount() when status hit 3
static HFONT g_lupopupTitleFont    = nullptr;
static HFONT g_lupopupBodyFont     = nullptr;
static HFONT g_lupopupBtnFont      = nullptr;

static LRESULT CALLBACK LauncherUpdatePopupProc(HWND hw, UINT msg,
                                                WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        RECT cr; GetClientRect(hw, &cr);

        // Solid dark background — no PNG dependency.
        HBRUSH bgBrush = CreateSolidBrush(RGB(28, 24, 20));
        FillRect(hdc, &cr, bgBrush);
        DeleteObject(bgBrush);

        // Gold border (2px rectangle) so the popup reads as themed
        // even without the parchment texture.
        HPEN borderPen = CreatePen(PS_SOLID, 2, RGB(200, 150, 60));
        HPEN   oldPen   = (HPEN)SelectObject(hdc, borderPen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, 0, 0, cr.right, cr.bottom);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(borderPen);

        SetBkMode(hdc, TRANSPARENT);
        int padX = (int)(24 * g_dpiScale);

        // Title
        HFONT oldFont = (HFONT)SelectObject(hdc, g_lupopupTitleFont);
        SetTextColor(hdc, RGB(220, 180, 100));
        RECT titleRc = { padX, (int)(24 * g_dpiScale),
                         cr.right - padX, (int)(64 * g_dpiScale) };
        DrawTextW(hdc, L"Updating Angiris Launcher", -1, &titleRc,
                  DT_CENTER | DT_TOP | DT_SINGLELINE);

        // Three stacked status lines. Color + decoration depends on
        // whether each status is past, current, or upcoming.
        SelectObject(hdc, g_lupopupBodyFont);
        const wchar_t* labels[3] = { L"Downloading", L"Updating", L"Complete" };
        int yStart = (int)(96  * g_dpiScale);
        int lineH  = (int)(40 * g_dpiScale);
        for (int i = 0; i < 3; ++i) {
            int statusIdx = i + 1;  // 1, 2, 3
            wstring text;
            COLORREF color;
            if (g_lupopupStatus > statusIdx) {
                // Done — checkmark + green
                text  = wstring(L"\u2713  ") + labels[i];  // ✓
                color = RGB(170, 215, 165);
            } else if (g_lupopupStatus == statusIdx) {
                // Active — ellipsis + gold
                text  = wstring(L"\u25B6  ") + labels[i] + L"\u2026";  // ▶ ...
                color = RGB(255, 220, 120);
            } else {
                // Pending — bullet + dim gray
                text  = wstring(L"\u00B7  ") + labels[i];  // ·
                color = RGB(130, 120, 110);
            }
            SetTextColor(hdc, color);
            RECT lineRc = { padX * 2, yStart + i * lineH,
                            cr.right - padX, yStart + (i + 1) * lineH };
            DrawTextW(hdc, text.c_str(), -1, &lineRc,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(hdc, oldFont);

        EndPaint(hw, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
        if (!dis || dis->CtlID != IDC_LAUNCHER_RESTART_BTN) return FALSE;
        bool enabled = IsWindowEnabled(dis->hwndItem) != FALSE;
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;

        COLORREF bg, fg, border;
        if (!enabled) {
            bg     = RGB(38, 34, 30);
            fg     = RGB(95, 88, 80);
            border = RGB(70, 62, 54);
        } else if (pressed) {
            bg     = RGB(80, 60, 28);
            fg     = RGB(255, 230, 150);
            border = RGB(220, 180, 100);
        } else {
            bg     = RGB(58, 48, 34);
            fg     = RGB(230, 200, 140);
            border = RGB(180, 140, 80);
        }
        HBRUSH bgBrush = CreateSolidBrush(bg);
        FillRect(dis->hDC, &dis->rcItem, bgBrush);
        DeleteObject(bgBrush);
        HPEN bp = CreatePen(PS_SOLID, 2, border);
        HPEN op = (HPEN)SelectObject(dis->hDC, bp);
        HBRUSH ob = (HBRUSH)SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
        Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top,
                  dis->rcItem.right, dis->rcItem.bottom);
        SelectObject(dis->hDC, op);
        SelectObject(dis->hDC, ob);
        DeleteObject(bp);

        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, fg);
        HFONT oldF = (HFONT)SelectObject(dis->hDC, g_lupopupBtnFont);
        wchar_t txt[64] = L"";
        GetWindowTextW(dis->hwndItem, txt, 64);
        DrawTextW(dis->hDC, txt, -1, &dis->rcItem,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dis->hDC, oldF);
        return TRUE;
    }

    case WM_TIMER: {
        if (wp == 1) {
            // Drive the Restart-button enabled state. Enabled iff
            // status has reached 3 AND ≥500 ms have elapsed since
            // the Complete tick was set. (Prevents accidental
            // immediate clicks.)
            if (g_lupopupRestartBtn) {
                bool wantEnabled =
                    (g_lupopupStatus >= 3) &&
                    (g_lupopupCompleteTick != 0) &&
                    ((GetTickCount() - g_lupopupCompleteTick) >= 500);
                BOOL nowEnabled = IsWindowEnabled(g_lupopupRestartBtn);
                if (wantEnabled && !nowEnabled) {
                    EnableWindow(g_lupopupRestartBtn, TRUE);
                    InvalidateRect(g_lupopupRestartBtn, nullptr, TRUE);
                }
            }
        }
        return 0;
    }

    case WM_COMMAND: {
        if (LOWORD(wp) != IDC_LAUNCHER_RESTART_BTN) return 0;
        if (!g_lupopupRestartBtn) return 0;
        if (!IsWindowEnabled(g_lupopupRestartBtn)) return 0;

        // Spawn the new launcher. Best-effort — if it fails, we
        // still tear down and exit so the user can manually relaunch.
        wchar_t curExe[MAX_PATH];
        if (GetModuleFileNameW(nullptr, curExe, MAX_PATH) != 0) {
            wstring curExeStr = curExe;
            size_t slashIdx = curExeStr.find_last_of(L"\\/");
            wstring installDir = (slashIdx != wstring::npos)
                               ? curExeStr.substr(0, slashIdx) : L"";
            STARTUPINFOW si = { sizeof(si) };
            PROCESS_INFORMATION pi = {};
            wstring quoted = L"\"" + curExeStr + L"\"";
            vector<wchar_t> cmdLine(quoted.begin(), quoted.end());
            cmdLine.push_back(0);
            if (CreateProcessW(curExeStr.c_str(), cmdLine.data(),
                               nullptr, nullptr, FALSE, 0, nullptr,
                               installDir.empty() ? nullptr
                                                  : installDir.c_str(),
                               &si, &pi)) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
        }

        DestroyWindow(hw);
        // Main window is hidden but still alive — close it so the
        // process exits cleanly. PostQuitMessage as belt-and-suspenders
        // in case g_hwMain somehow doesn't respond.
        if (g_hwMain && IsWindow(g_hwMain)) {
            PostMessageW(g_hwMain, WM_CLOSE, 0, 0);
        } else {
            PostQuitMessage(0);
        }
        return 0;
    }

    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC:
        // Make Windows happy if it asks for a background brush.
        return (LRESULT)GetStockObject(BLACK_BRUSH);

    case WM_CLOSE:
        // Suppress — user must click Restart (or kill from Task Manager).
        return 0;

    case WM_DESTROY:
        KillTimer(hw, 1);
        if (g_lupopupTitleFont) { DeleteObject(g_lupopupTitleFont); g_lupopupTitleFont = nullptr; }
        if (g_lupopupBodyFont)  { DeleteObject(g_lupopupBodyFont);  g_lupopupBodyFont  = nullptr; }
        if (g_lupopupBtnFont)   { DeleteObject(g_lupopupBtnFont);   g_lupopupBtnFont   = nullptr; }
        if (g_lupopupWnd == hw) g_lupopupWnd = nullptr;
        g_lupopupRestartBtn = nullptr;
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

// Creates and shows the popup. Lazily registers the window class on
// first call. Uses system Segoe UI throughout — no dependency on the
// launcher's font collection or PNG cache.
static void ShowLauncherUpdatePopup() {
    if (g_lupopupWnd) return;

    static bool classReg = false;
    if (!classReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = LauncherUpdatePopupProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"AngirisLauncherUpdatePopup";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        classReg = true;
    }

    int w  = (int)(440 * g_dpiScale);
    int h  = (int)(340 * g_dpiScale);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x  = (sw - w) / 2;
    int y  = (sh - h) / 2;

    HWND wnd = CreateWindowExW(
        WS_EX_TOPMOST,
        L"AngirisLauncherUpdatePopup",
        L"Updating Angiris Launcher",
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, w, h,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!wnd) return;
    g_lupopupWnd = wnd;

    // System fonts — no PrivateFontCollection involvement. Negative
    // heights are point-size-ish in logical units.
    int titlePx = -(int)(20 * g_dpiScale);
    int bodyPx  = -(int)(16 * g_dpiScale);
    int btnPx   = -(int)(15 * g_dpiScale);
    auto mkFont = [&](int px, int weight) {
        return CreateFontW(px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    };
    g_lupopupTitleFont = mkFont(titlePx, FW_BOLD);
    g_lupopupBodyFont  = mkFont(bodyPx,  FW_NORMAL);
    g_lupopupBtnFont   = mkFont(btnPx,   FW_BOLD);

    // Restart button — owner-drawn so we can theme it without
    // dependencies, always present but starts disabled.
    int btnW = (int)(180 * g_dpiScale);
    int btnH = (int)(50  * g_dpiScale);
    int btnX = (w - btnW) / 2;
    int btnY = h - btnH - (int)(28 * g_dpiScale);
    g_lupopupRestartBtn = CreateWindowExW(
        0, L"BUTTON", L"Restart",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        btnX, btnY, btnW, btnH,
        wnd, (HMENU)(INT_PTR)IDC_LAUNCHER_RESTART_BTN,
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g_lupopupRestartBtn, WM_SETFONT,
                 (WPARAM)g_lupopupBtnFont, TRUE);
    EnableWindow(g_lupopupRestartBtn, FALSE);   // enabled later by timer

    // 100ms timer drives the "enable Restart after 500ms post-complete"
    // logic. Cheap, runs only while the popup exists.
    SetTimer(wnd, 1, 100, nullptr);

    ShowWindow(wnd, SW_SHOW);
    UpdateWindow(wnd);
    SetForegroundWindow(wnd);
}

// Worker thread for the launcher self-update install. Sends three
// MSG_LUPOPUP_STATUS messages to drive the popup forward (1, 2, 3).
// File overwrites are safe by the time this runs because
// StartLauncherUpdateInstall has already torn down the asset cache
// and font collection on the UI thread.
// Variant of CopyTreeInto that logs every CopyFileW call (success or
// failure) and tallies failures into *failCount. Used exclusively by
// the launcher self-update worker so we get full visibility into
// which files are locked when something goes wrong. Returns true iff
// every file copied successfully.
static bool CopyTreeIntoLogged(const wstring& src, const wstring& dst,
                               FILE* logF, int* failCount) {
    auto WLOG = [&](const wchar_t* fmt, ...) {
        if (!logF) return;
        va_list ap;
        va_start(ap, fmt);
        vfwprintf(logF, fmt, ap);
        va_end(ap);
        fwprintf(logF, L"\n");
        fflush(logF);
    };

    WIN32_FIND_DATAW fd;
    wstring pattern = src + L"\\*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;   // empty src is OK
    bool ok = true;
    do {
        if (wcscmp(fd.cFileName, L".")  == 0) continue;
        if (wcscmp(fd.cFileName, L"..") == 0) continue;
        wstring s = src + L"\\" + fd.cFileName;
        wstring d = dst + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!ZI_DirExists(d)) CreateDirectoryW(d.c_str(), nullptr);
            if (!CopyTreeIntoLogged(s, d, logF, failCount)) ok = false;
        } else {
            if (CopyFileW(s.c_str(), d.c_str(), FALSE)) {
                WLOG(L"  COPY OK   : %ls", d.c_str());
            } else {
                DWORD err = GetLastError();
                WLOG(L"  COPY FAIL : %ls  (err=%lu)", d.c_str(), err);
                if (failCount) (*failCount)++;
                ok = false;
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

static DWORD WINAPI LauncherUpdateInstallWorker(LPVOID lp) {
    (void)lp;

    // Diagnostic log appended each run.
    wstring logPath = AppDir() + L"\\assets\\last_update_install.log";
    FILE* logF = nullptr;
    _wfopen_s(&logF, logPath.c_str(), L"a, ccs=UTF-8");
    auto LOG = [&](const wchar_t* fmt, ...) {
        if (!logF) return;
        time_t t = time(nullptr);
        struct tm tm_;
        localtime_s(&tm_, &t);
        wchar_t ts[32];
        wcsftime(ts, 32, L"%H:%M:%S", &tm_);
        fwprintf(logF, L"[%ls] ", ts);
        va_list ap;
        va_start(ap, fmt);
        vfwprintf(logF, fmt, ap);
        va_end(ap);
        fwprintf(logF, L"\n");
        fflush(logF);
    };
    auto CLOSELOG = [&]() { if (logF) { fclose(logF); logF = nullptr; } };

    auto sendStatus = [&](int s) {
        SendMessageW(g_hwMain, MSG_LUPOPUP_STATUS, (WPARAM)s, 0);
    };

    LOG(L"=== LauncherUpdateInstallWorker (popup-driven) ===");
    LOG(L"Download URL: %ls", g_launcherUpdateDownloadUrl.c_str());

    sendStatus(1);   // Downloading

    wstring tmpDir = MakeTempInstallDir();
    LOG(L"Temp dir: %ls", tmpDir.c_str());
    if (tmpDir.empty()) { LOG(L"FAIL: MakeTempInstallDir"); CLOSELOG(); return 1; }

    wstring zipPath = tmpDir + L"\\release.zip";
    int dlResult = HttpDownloadFile(g_launcherUpdateDownloadUrl,
                                    zipPath, 60000);
    LOG(L"Download result: %d (0=ok)", dlResult);
    if (dlResult != 0) {
        DeleteFolderRecursive(tmpDir);
        CLOSELOG(); return 1;
    }

    sendStatus(2);   // Updating

    wstring extractDir = tmpDir + L"\\extracted";
    CreateDirectoryW(extractDir.c_str(), nullptr);
    // Skip *.ttf at extraction. Windows holds an internal mapping for
    // any font that's been used to rasterize glyphs in the current
    // process, which RemoveFontResourceEx doesn't tear down — so the
    // active font's .ttf always fails CopyFileW with err=1224 during
    // a self-update. Fonts essentially never change between launcher
    // releases anyway; skipping them gives clean install logs and a
    // PARTIAL result becomes OK. Excluded at extraction so the files
    // never even land in temp.
    bool tarOk = RunTarExtract(zipPath, extractDir, L"*.ttf");
    LOG(L"Extract: %ls (TTFs excluded)", tarOk ? L"OK" : L"FAILED");
    if (!tarOk) { DeleteFolderRecursive(tmpDir); CLOSELOG(); return 1; }

    wstring newExePath = FindAngirisExeInTree(extractDir);
    LOG(L"New exe found: %ls",
        newExePath.empty() ? L"(NONE)" : newExePath.c_str());
    if (newExePath.empty()) {
        DeleteFolderRecursive(tmpDir); CLOSELOG(); return 1;
    }
    size_t lastSlash = newExePath.find_last_of(L"\\/");
    if (lastSlash == wstring::npos) {
        DeleteFolderRecursive(tmpDir); CLOSELOG(); return 1;
    }
    wstring releaseRoot = newExePath.substr(0, lastSlash);
    LOG(L"Release root: %ls", releaseRoot.c_str());

    wchar_t curExe[MAX_PATH];
    if (GetModuleFileNameW(nullptr, curExe, MAX_PATH) == 0) {
        LOG(L"FAIL: GetModuleFileNameW");
        DeleteFolderRecursive(tmpDir); CLOSELOG(); return 1;
    }
    wstring curExeStr   = curExe;
    size_t  curLastSlash = curExeStr.find_last_of(L"\\/");
    if (curLastSlash == wstring::npos) {
        DeleteFolderRecursive(tmpDir); CLOSELOG(); return 1;
    }
    wstring installDir = curExeStr.substr(0, curLastSlash);
    LOG(L"Install dir: %ls", installDir.c_str());

    wstring oldExe = curExeStr + L".old";
    DeleteFileW(oldExe.c_str());
    BOOL renameOk = MoveFileW(curExeStr.c_str(), oldExe.c_str());
    LOG(L"Rename current → .old: %ls (err=%lu)",
        renameOk ? L"OK" : L"FAILED",
        renameOk ? 0 : GetLastError());
    if (!renameOk) { DeleteFolderRecursive(tmpDir); CLOSELOG(); return 1; }

    // The big payoff: by this point the main window is hidden, the
    // asset cache is destroyed, and the font collection is unloaded.
    // PNG/font files are no longer locked and CopyFileW can overwrite
    // them without ACCESS_DENIED. If any file is STILL locked despite
    // all that, the per-file log shows which one — and we proceed to
    // status 3 anyway so the popup completes and the user can click
    // Restart. A partial copy where Angiris.exe succeeded is much
    // better UX than hanging on "Updating" forever.
    int failCount = 0;
    LOG(L"--- Begin copy ---");
    bool copyOk = CopyTreeIntoLogged(releaseRoot, installDir,
                                     logF, &failCount);
    LOG(L"--- End copy ---  result=%ls  failures=%d",
        copyOk ? L"OK" : L"PARTIAL", failCount);

    DeleteFolderRecursive(tmpDir);
    LOG(L"Install %ls — waiting 1s before signalling Complete",
        copyOk ? L"complete" : L"partial (see fails above)");

    // Per UX spec: 1-second beat between actual finish and showing
    // "Complete" — gives the filesystem and any background indexers
    // a moment to settle before the user can act on the new files.
    Sleep(1000);

    sendStatus(3);   // Complete

    LOG(L"=== Worker done ===");
    CLOSELOG();
    return 0;
}

// Kickoff: hide the main GUI, free its locked file handles, show the
// popup, spawn the worker. Returns immediately. Runs on UI thread.
static void StartLauncherUpdateInstall(HWND parent) {
    if (g_launcherUpdateDownloadUrl.empty()) {
        // No downloadable .zip on the release — fall back to opening
        // the GitHub releases page in the browser. Don't tear down
        // anything in this branch since we're not actually installing.
        wstring releasesUrl = wstring(L"https://github.com/")
                            + LAUNCHER_GITHUB_OWNER + L"/"
                            + LAUNCHER_GITHUB_REPO
                            + L"/releases/latest";
        ShellExecuteW(nullptr, L"open", releasesUrl.c_str(),
                      nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }

    // Reset popup state for a fresh run.
    g_lupopupStatus       = 0;
    g_lupopupCompleteTick = 0;

    // Hide the main GUI. Its child windows + paint code won't fire
    // anymore, so the asset/font teardown below is safe.
    if (g_hwMain && IsWindow(g_hwMain)) {
        ShowWindow(g_hwMain, SW_HIDE);
    }

    // Release everything the installer would otherwise have to fight.
    // GDI+ Bitmap destructors release their underlying file handles;
    // PrivateFontCollection destructor releases the .ttf handles.
    DestroyAssetCache();
    UnloadFonts();

    // Show the popup *after* the asset/font teardown, since it uses
    // only system resources (Segoe UI + GDI) and is therefore not
    // affected by what we just released.
    ShowLauncherUpdatePopup();

    HANDLE t = CreateThread(nullptr, 0,
                            LauncherUpdateInstallWorker,
                            parent, 0, nullptr);
    if (t) CloseHandle(t);
}

// Process a single zip end-to-end. Runs on the worker thread. Synchronously
// calls SendMessage to show the conflict / set-path / no-modinfo dialogs
// (which run on the UI thread); the worker blocks until the user picks.
static void ProcessOneZip(const wstring& zipPath, int zipIdx, int zipTotal) {
    wstring zipBase = BaseName(zipPath);

    // Stage 0 — starting. Renders an empty progress bar.
    PushProgress(0, zipIdx, zipTotal, zipBase, L"Starting...");

    // If D2R path isn't set, ask the user. They can either set it now
    // (worker continues with this same zip) or cancel — in which case
    // we clear the rest of the queue too. With no D2R path, every
    // remaining zip would just hit the same dialog; asking again
    // n − 1 times for the same answer would feel hostile.
    if (g_cfg.d2rPath.empty()) {
        int got = (int)SendMessageW(g_hwMain, MSG_ZIP_NEED_PATH, 0, 0);
        if (got != 1) {
            std::lock_guard<std::mutex> lk(g_zipQueueMutex);
            g_zipQueue.clear();
            return;
        }
        // If we get here, g_cfg.d2rPath is populated.
    }
    if (!ZI_DirExists(g_cfg.d2rPath + L"\\mods")) {
        CreateDirectoryW((g_cfg.d2rPath + L"\\mods").c_str(), nullptr);
    }

    PushProgress(1, zipIdx, zipTotal, zipBase, L"Extracting archive...");

    wstring tmp = MakeTempInstallDir();
    if (tmp.empty()) return;

    if (!RunTarExtract(zipPath, tmp)) {
        DeleteFolderRecursive(tmp);
        return;
    }

    PushProgress(2, zipIdx, zipTotal, zipBase, L"Locating mod info...");

    wstring modinfoPath = FindModinfoJson(tmp);
    if (modinfoPath.empty()) {
        // No modinfo.json — surface the error so the user knows what
        // went wrong. SendMessage so it's modal over the progress
        // dialog. Worker continues with the next zip when the user
        // clicks OK.
        SendMessageW(g_hwMain, MSG_ZIP_NO_MODINFO, 0, (LPARAM)&zipBase);
        DeleteFolderRecursive(tmp);
        return;
    }

    size_t slash = modinfoPath.find_last_of(L"\\/");
    wstring modRoot = (slash == wstring::npos) ? tmp
                                               : modinfoPath.substr(0, slash);

    wstring modName = ReadModNameFromInfo(modinfoPath);
    if (modName.empty()) {
        size_t s2 = modRoot.find_last_of(L"\\/");
        if (s2 != wstring::npos) modName = modRoot.substr(s2 + 1);
    }
    modName = SanitizeModName(modName);
    if (modName.empty()) {
        DeleteFolderRecursive(tmp);
        return;
    }

    wstring destDir = g_cfg.d2rPath + L"\\mods\\" + modName;

    PushProgress(3, zipIdx, zipTotal, zipBase, L"Installing files...");

    if (ZI_DirExists(destDir)) {
        ConflictDialogParam p;
        p.modName = modName;
        p.choice  = 0;
        SendMessageW(g_hwMain, MSG_ZIP_CONFLICT_DIALOG, 0, (LPARAM)&p);

        if (p.choice == 0) {
            DeleteFolderRecursive(tmp);
            return;
        }
        if (p.choice == 2) {
            // Overwrite — wipe and reinstall. But first: back up the
            // existing mod's save folder so the user doesn't lose their
            // characters / stash / configs to a one-click oops. We
            // read savepath from the OLD mod's modinfo.json on disk
            // (it's about to be deleted, so this is the last chance).
            // Silent on failure — backup is a safety net, not a
            // blocker; if it can't run (no save folder yet, no
            // permissions, etc.) the user-requested overwrite still
            // proceeds.
            wstring oldModinfo = destDir + L"\\modinfo.json";
            if (ZI_FileExists(oldModinfo)) {
                wstring unused;
                BackupModSavesFromModinfo(oldModinfo, &unused);
            }
            DeleteFolderRecursive(destDir);
            CreateDirectoryW(destDir.c_str(), nullptr);
            CopyTreeInto(modRoot, destDir, /*addMissing*/ true);
        } else {
            // Update — overwrite same-named files AND add new files,
            // preserving folder-only files (saves, user configs).
            CopyTreeInto(modRoot, destDir, /*addMissing*/ true);
        }
    } else {
        CreateDirectoryW(destDir.c_str(), nullptr);
        CopyTreeInto(modRoot, destDir, /*addMissing*/ true);
    }

    DeleteFolderRecursive(tmp);

    PushProgress(4, zipIdx, zipTotal, zipBase, L"Done");
}

// Worker thread entry. Pops zips off the queue, processes one at a time.
// Drives the progress dialog from start to finish — shown before the
// first stage update, hidden after the queue drains.
static DWORD WINAPI ZipInstallWorker(LPVOID) {
    // Snapshot the queue length for the "N of M" display, then loop.
    // New drops that arrive mid-run are appended to the queue and get
    // picked up below; we extend total dynamically so the label stays
    // accurate.
    int processed = 0;
    int total = 0;
    {
        std::lock_guard<std::mutex> lk(g_zipQueueMutex);
        total = (int)g_zipQueue.size();
    }
    if (g_hwMain && total > 0) {
        SendMessageW(g_hwMain, MSG_ZIP_PROGRESS_SHOW, 0, 0);
    }
    for (;;) {
        wstring zip;
        {
            std::lock_guard<std::mutex> lk(g_zipQueueMutex);
            if (g_zipQueue.empty()) {
                g_zipWorkerRunning = false;
                break;
            }
            zip = g_zipQueue.front();
            g_zipQueue.erase(g_zipQueue.begin());
            // Recompute total in case more were appended mid-run.
            int remaining = (int)g_zipQueue.size();
            int newTotal = processed + 1 + remaining;
            if (newTotal > total) total = newTotal;
        }
        ++processed;
        ProcessOneZip(zip, processed, total);
    }
    if (g_hwMain) PostMessageW(g_hwMain, MSG_ZIP_PROGRESS_HIDE, 0, 0);
    if (g_hwMain) PostMessageW(g_hwMain, MSG_ZIP_QUEUE_DONE, 0, 0);
    return 0;
}

// Append zip paths to the queue, starting the worker if it's idle.
// Called from the WM_DROPFILES handler. Returns count of files actually
// queued (filtered to .zip).
static int EnqueueZipsForInstall(const vector<wstring>& paths) {
    int added = 0;
    {
        std::lock_guard<std::mutex> lk(g_zipQueueMutex);
        for (const auto& p : paths) {
            size_t dot = p.find_last_of(L'.');
            if (dot == wstring::npos) continue;
            wstring ext = p.substr(dot);
            if (_wcsicmp(ext.c_str(), L".zip") != 0) continue;
            g_zipQueue.push_back(p);
            ++added;
        }
    }
    if (added > 0) {
        bool expected = false;
        if (g_zipWorkerRunning.compare_exchange_strong(expected, true)) {
            HANDLE h = CreateThread(nullptr, 0, ZipInstallWorker,
                                    nullptr, 0, nullptr);
            if (h) CloseHandle(h);
            else   g_zipWorkerRunning = false;
        }
    }
    return added;
}

// Show the D2R folder picker (same SHBrowseForFolder used for the
// Loader Directory "..." button). Returns true if the user picked a
// folder and the cfg was saved. Used both by the rail button and by
// the zip installer's "Set Path" dialog when a drop happens before
// the D2R path is configured.
static bool PromptForD2RPath(HWND parent) {
    BROWSEINFOW bi = {};
    bi.hwndOwner = parent;
    bi.lpszTitle = L"Select D2R Install Folder";
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    wchar_t path[MAX_PATH] = {};
    bool ok = (SHGetPathFromIDListW(pidl, path) != FALSE);
    CoTaskMemFree(pidl);
    if (!ok || !*path) return false;
    g_cfg.d2rPath = path;
    SaveCfg();
    if (g_hwMain) {
        InvalidateRectL(g_hwMain, &g_loaderDirRect, FALSE);
    }
    LoadLoaderOpts();
    RefreshMods();
    StartModsWatcher();
    return true;
}

static LRESULT CALLBACK MainProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_ERASEBKGND:
        // Suppress: the double-buffered WM_PAINT fully covers its dirty
        // rect, so erasing first would just cause a flash.
        return TRUE;

    case WM_SETCURSOR: {
        // Hand cursor over the clickable version label below the logo.
        // WM_SETCURSOR fires when the mouse moves over a window (or a
        // child claims the cursor with HTCLIENT). For client-area hits
        // we hit-test in LOGICAL coords against g_versionLabelRect
        // (set during paint). For non-client hits or anything else,
        // fall through to default processing so the title bar's
        // resize cursors continue to work.
        //
        // Only show the hand cursor when the label is actually
        // clickable — when no update is pending, the click handler
        // bails too, so a hand cursor would be a lie.
        if (LOWORD(lp) == HTCLIENT && g_launcherUpdateAvailable) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hw, &pt);
            POINT lpt = { U(pt.x), U(pt.y) };   // physical → logical
            if (PtInRect(&g_versionLabelRect, lpt)) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        return DefWindowProcW(hw, msg, wp, lp);
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        RECT r; GetClientRect(hw, &r);

        // Double-buffer: paint into an offscreen DC sized to the dirty
        // rect, then blit once. Eliminates the flicker we'd otherwise see
        // when the bottom panel expands/collapses (and on any partial
        // invalidate). MemDC blits its region back on destruction.
        int dx = ps.rcPaint.left;
        int dy = ps.rcPaint.top;
        int dw = ps.rcPaint.right  - ps.rcPaint.left;
        int dh = ps.rcPaint.bottom - ps.rcPaint.top;
        if (dw > 0 && dh > 0) {
            MemDC m(hdc, dx, dy, dw, dh);
            // Backdrop
            FillSolid(m.dc, dx, dy, dw, dh, Tok::crBgDeep);
            // PaintBody draws in LOGICAL coordinates — convert the physical
            // client size to logical here so the body math reads in logical
            // units (the Graphics inside PaintBody has a ScaleTransform that
            // maps logical → physical at the GDI+ output stage).
            PaintBody(m.dc, U(r.right), U(r.bottom));
        }
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, Tok::crBgPanel);
        SetTextColor(hdc, Tok::crText);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    // ── Owner-drawn menu items: measure pass ─────────────────────────
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* m = (MEASUREITEMSTRUCT*)lp;
        if (m->CtlType == ODT_MENU) {
            // Item dimensions come from g_menuCtx (set by the caller
            // right before TrackPopupMenu). Default ctx is sized for the
            // IntValue dropdowns (loader-options style); the toolbar
            // Font/Color dropdowns set wider items for their labels.
            m->itemWidth  = S(g_menuCtx.itemWidth);
            m->itemHeight = S(g_menuCtx.itemHeight);
            return TRUE;
        }
        return FALSE;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;

        // ── Owner-drawn menu items (loader options dropdown popup) ──────
        // We paint our own stone/bronze look so the popup doesn't fall
        // back to Windows' white/black/blue system theme. The popup
        // window border itself is still drawn by the OS — only the items
        // inside are owner-draw.
        if (d->CtlType == ODT_MENU) {
            Graphics g(d->hDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

            // Pre-cast the RECT members to plain int. The GDI+ DrawLine /
            // DrawRectangle / FillRectangle / FillEllipse overloads come
            // in (REAL, REAL, REAL, REAL) and (INT, INT, INT, INT) flavors;
            // mixing LONG (from RECT) with int (from local arithmetic)
            // leaves the compiler unable to pick between the two.
            int rl = (int)d->rcItem.left;
            int rt = (int)d->rcItem.top;
            int rR = (int)d->rcItem.right;
            int rB = (int)d->rcItem.bottom;
            int rw = rR - rl;
            int rh = rB - rt;
            bool selected = (d->itemState & ODS_SELECTED) != 0;
            bool checked  = (d->itemState & ODS_CHECKED)  != 0;

            // Background: dark stone-toned fill, slightly brighter on hover.
            SolidBrush bg(selected ? Tok::BgPanel2 : Tok::BgPanel);
            g.FillRectangle(&bg, rl, rt, rw, rh);

            // Bronze separator at the bottom of each item (last item's
            // separator is masked by the OS-drawn popup border).
            Pen sep(Tok::BronzeDim, 1.0f);
            g.DrawLine(&sep, rl, rB - 1, rR, rB - 1);

            // Selection frame: a thin bronze inset rectangle.
            if (selected) {
                Pen glow(Tok::Gold, 1.0f);
                g.DrawRectangle(&glow, rl + 1, rt + 1, rw - 3, rh - 3);
            }

            // Current-value indicator: a small gold disc in the left margin.
            if (checked) {
                int d_ = S(6);
                SolidBrush dot(Tok::GoldBright);
                g.FillEllipse(&dot,
                              rl + S(8),
                              rt + (rh - d_) / 2,
                              d_, d_);
            }

            // Value/label rendering depends on the popup's kind. Callers
            // populate g_menuCtx (labels[], colors[], kind) before calling
            // TrackPopupMenu — see the popMenu helpers in WM_LBUTTONDOWN.
            int  itemIdx = (int)d->itemID - 1;   // we shifted by +1 in the popMenu insert
            StringFormat sfC;
            sfC.SetAlignment(StringAlignmentCenter);
            sfC.SetLineAlignment(StringAlignmentCenter);
            SolidBrush txt(selected ? Tok::GoldBright : Tok::Gold);

            switch (g_menuCtx.kind) {
                case MenuKind::IntValue: {
                    wchar_t buf[16];
                    swprintf(buf, 16, L"%d", itemIdx);
                    g.DrawString(buf, -1, g_fBtn,
                                 RectF((REAL)rl, (REAL)rt, (REAL)rw, (REAL)rh),
                                 &sfC, &txt);
                    break;
                }
                case MenuKind::StringList: {
                    const wchar_t* label = (itemIdx >= 0 && itemIdx < (int)g_menuCtx.labels.size())
                                           ? g_menuCtx.labels[itemIdx].c_str()
                                           : L"?";
                    g.DrawString(label, -1, g_fBtn,
                                 RectF((REAL)rl, (REAL)rt, (REAL)rw, (REAL)rh),
                                 &sfC, &txt);
                    break;
                }
                case MenuKind::ColorSwatch: {
                    // Centered square swatch — matches the idle box's
                    // swatch style (PaintToolbarControl renders the
                    // chip as a square of height bh-8). Width-set in
                    // popMenu so each item matches the idle textbox
                    // size (TBL::COLOR_VALUE_W), not a huge rectangle.
                    int swH = rh - S(8);
                    int swW = swH;            // square
                    if (swW > rw - S(8)) swW = rw - S(8);
                    int swX = rl + (rw - swW) / 2;
                    int swY = rt + (rh - swH) / 2;
                    if (itemIdx >= 0 && itemIdx < (int)g_menuCtx.colors.size()) {
                        COLORREF c = g_menuCtx.colors[itemIdx];
                        SolidBrush sw(Color(GetRValue(c), GetGValue(c), GetBValue(c)));
                        g.FillRectangle(&sw, swX, swY, swW, swH);
                        Pen swBorder(Tok::BronzeDim, 1.0f);
                        g.DrawRectangle(&swBorder, swX, swY, swW, swH);
                    }
                    break;
                }
                case MenuKind::FontPreview: {
                    // Render the word "STYLE" using the font family AND
                    // style flags associated with this item (the label
                    // here stores the family name as resolved at
                    // LoadFonts time; g_availableStyles stores the
                    // FontStyle bits derived from filename suffix). The
                    // style is the critical bit when multiple files
                    // share a family — Cinzel-Regular.ttf and Cinzel-
                    // Bold.ttf both report family "Cinzel" but render
                    // identically without explicit FontStyleBold.
                    const wchar_t* famName =
                        (itemIdx >= 0 && itemIdx < (int)g_menuCtx.labels.size())
                            ? g_menuCtx.labels[itemIdx].c_str()
                            : L"";
                    INT styleBits = (itemIdx >= 0
                                     && itemIdx < (int)g_availableStyles.size())
                                    ? g_availableStyles[itemIdx]
                                    : (INT)FontStyleRegular;
                    FontFamily fam(famName);
                    REAL fontPx = (REAL)max(12, (int)(rh * 0.55f));
                    if (fam.GetLastStatus() == Ok) {
                        Font tempFont(&fam, fontPx, styleBits, UnitPixel);
                        g.DrawString(L"STYLE", -1, &tempFont,
                                     RectF((REAL)rl, (REAL)rt, (REAL)rw, (REAL)rh),
                                     &sfC, &txt);
                    } else {
                        g.DrawString(L"STYLE", -1, g_fBtn,
                                     RectF((REAL)rl, (REAL)rt, (REAL)rw, (REAL)rh),
                                     &sfC, &txt);
                    }
                    break;
                }
            }
            return TRUE;
        }

        HWND ctl = d->hwndItem;
        auto it = g_btnStates.find(ctl);
        if (it == g_btnStates.end())
            return DefWindowProc(hw, msg, wp, lp);
        const ButtonState& st = it->second;

        int W = d->rcItem.right  - d->rcItem.left;
        int H = d->rcItem.bottom - d->rcItem.top;
        bool pressed  = (d->itemState & ODS_SELECTED) != 0;
        bool disabled = (d->itemState & ODS_DISABLED) != 0;
        bool hover    = st.hover;
        // Refresh button has a special "stale" highlight when g_modsDirty
        // — keep the existing behavior.
        bool dirty    = (ctl == g_hwRefresh) && g_modsDirty;

        // The HWND is enlarged by BTN_OVERFLOW_PAD on each side (set up in
        // Layout()) so hover scale-ups have room to render. The VISIBLE art
        // occupies a centered rect within the HWND; everything (backdrop
        // composite, asset draw, label draw, transform center) is anchored
        // to this art rect, not the full HWND rect.
        //
        // EXCEPTIONS — these kinds have hover-grow disabled and their HWND
        // is sized exactly to the visible art, so the art rect IS the HWND
        // rect. Insetting by P here would draw the art at (HWND − 2·P),
        // which squashes the asset (e.g. 138×52 → 114×28 for Refresh).
        int P = LO::BTN_OVERFLOW_PAD;
        if (st.kind == ButtonKind::Ellipse        ||
            st.kind == ButtonKind::Arrow          ||
            st.kind == ButtonKind::Refresh        ||
            st.kind == ButtonKind::ModLink        ||
            st.kind == ButtonKind::ModLinkDocs    ||
            st.kind == ButtonKind::ModLinkDiscord ||
            st.kind == ButtonKind::ModLinkWebsite)
            P = 0;
        int artX = d->rcItem.left + P;
        int artY = d->rcItem.top  + P;
        int artW = W - 2 * P;
        int artH = H - 2 * P;

        // Legacy expansion-panel detection. NavSm was the kind originally
        // used for the bottom expansion panel buttons; the active code now
        // uses NexusUpdate for those, so this flag is effectively always
        // false. Kept so the NavSm path still works if a future build
        // re-registers any buttons as NavSm (e.g. for 9-slice compression
        // or the smaller g_fNavSm label font).
        bool inExpansion = (st.kind == ButtonKind::NavSm);

        // ── Asset path: choose the asset name for this kind/state ───────
        // ── Asset lookup ────────────────────────────────────────────────
        // One asset per kind; the state (idle/hover/click) is reproduced
        // via a GDI+ transform applied at paint time so both the art and
        // the label move/scale together.
        const wchar_t* assetName = AssetNameFor(st.kind);
        Gdiplus::Bitmap* assetBM = assetName ? AssetImage(assetName) : nullptr;

        // ModLink* fallback: when the per-button square asset (btn_docs,
        // btn_discord, btn_website) is missing, route to the shared
        // btn_nexus_update.png and 9-slice it to the button's square
        // dimensions. The single-letter label (D/X/W) then renders on
        // top because the primary asset didn't draw (skipLabel logic
        // below leaves the label visible in this case).
        bool modLinkFallback = false;
        if (!assetBM && (st.kind == ButtonKind::ModLinkDocs
                      || st.kind == ButtonKind::ModLinkDiscord
                      || st.kind == ButtonKind::ModLinkWebsite)) {
            assetBM = AssetImage(L"btn_nexus_update.png");
            modLinkFallback = (assetBM != nullptr);
        }

        // ── Backdrop ────────────────────────────────────────────────────
        // When the styled asset is present, composite the parent window's
        // background (stone + frame) into the button rect first so the
        // asset's transparent edges show what's behind. When the asset
        // is missing, fall back to a solid fill so OPDrawBtnFrame reads.
        Graphics g(d->hDC);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

        if (assetBM) {
            // Composite parent's stone into our rect, mapped by where we
            // sit in the parent. Buttons in the expansion area (y > 1024
            // logical) are backed by bg_expand.png; everything else by
            // bg_stone.png.
            //
            // Two coordinate systems are in play. MapWindowPoints returns
            // PHYSICAL pixels (Win32 doesn't see g_scale), but the parent
            // paints bg_stone/bg_expand under a ScaleTransform so their
            // native pixels correspond to LOGICAL parent coords. To pick
            // the right source pixels we convert the parent position to
            // logical via U(), and we ask for U(W) × U(H) source pixels
            // (which then get stretched back up to W × H physical when
            // drawn onto our physical-pixel button surface — that stretch
            // factor is g_scale, matching the parent's visual scaling).
            POINT origin = { 0, 0 };
            MapWindowPoints(ctl, GetParent(ctl), &origin, 1);
            int parentX = origin.x + d->rcItem.left;
            int parentY = origin.y + d->rcItem.top;
            int lParentX = U(parentX);
            int lParentY = U(parentY);
            int lW = U(W);
            int lH = U(H);

            int collapsedH = 1024;
            if (Gdiplus::Bitmap* fm = AssetImage(L"frame_main.png"))
                collapsedH = (int)fm->GetHeight();

            const wchar_t* bgName = (lParentY >= collapsedH)
                ? L"bg_expand.png"
                : L"bg_stone.png";
            int srcOffsetY = (lParentY >= collapsedH)
                ? (lParentY - collapsedH)     // bg_expand starts at y=collapsedH (logical)
                : lParentY;

            if (Gdiplus::Bitmap* bg = AssetImage(bgName)) {
                g.DrawImage(bg,
                    Rect(d->rcItem.left, d->rcItem.top, W, H),
                    (REAL)lParentX, (REAL)srcOffsetY, (REAL)lW, (REAL)lH,
                    UnitPixel);
            } else {
                // No background asset available — solid fill fallback
                FillSolid(d->hDC, d->rcItem.left, d->rcItem.top, W, H,
                          Tok::crBgDeep);
            }
            // Note: we deliberately skip painting frame_main.png on top
            // of the stone here. The button rects sit inside the frame's
            // interior so the frame filigree wouldn't intersect them
            // anyway in the typical case.
        } else {
            // Programmatic fallback path — solid fill so OPDrawBtnFrame
            // is clearly visible.
            FillSolid(d->hDC, d->rcItem.left, d->rcItem.top, W, H,
                      Tok::crBgDeep);
        }

        // ── Asset + label, drawn under a shared state transform ─────────
        // Both the asset image AND its label text scale/offset together,
        // so the button reads as one piece (instead of the art moving
        // independently from the text). The backdrop composite above
        // is already done at the full HWND rect — it stays unaffected
        // by the transform, so the stone fills the entire HWND cleanly
        // even when the foreground shrinks on click.
        //
        // Everything below operates on the ART rect (centered inset), so
        // the visible button stays in the right position regardless of
        // the overflow padding.
        bool drewAsset = false;
        ButtonStateTransform xf = StateTransformFor(st.kind);
        bool applyTransform = (hover || pressed) && assetBM;

        Gdiplus::Matrix prevMatrix;
        if (applyTransform) {
            g.GetTransform(&prevMatrix);
            REAL cx = (REAL)(artX + artW * 0.5f);
            REAL cy = (REAL)(artY + artH * 0.5f);
            REAL scale = pressed ? xf.scaleClick : xf.scaleHover;
            REAL dx    = pressed ? xf.offsetXClick : 0.0f;
            REAL dy    = pressed ? xf.offsetYClick : 0.0f;
            // Translate to center, scale, translate back — offset by the
            // press shift so the whole button moves down-right when pressed.
            g.TranslateTransform(cx + dx, cy + dy);
            g.ScaleTransform(scale, scale);
            g.TranslateTransform(-cx, -cy);
        }

        // Draw the asset image (or the programmatic fallback frame).
        if (assetBM) {
            // Nav buttons in the BOTTOM EXPANSION PANEL (inExpansion,
            // computed earlier) render shorter (58px vs 76px native) to
            // fit inside the 300px expansion frame. Straight DrawImage
            // would squash the gem corners, so we route them through
            // DrawButton9Slice — the corners stay pixel-perfect and only
            // the middle compresses.

            // Arrow asset is rotated 180° when the bottom panel is expanded
            // so the chevron direction matches "click to collapse" vs "click
            // to expand". The rotation composes with the state transform.
            if (st.kind == ButtonKind::Arrow && g_bottomExpanded) {
                Gdiplus::Matrix beforeRotate;
                g.GetTransform(&beforeRotate);
                REAL cx = (REAL)(artX + artW * 0.5f);
                REAL cy = (REAL)(artY + artH * 0.5f);
                g.TranslateTransform(cx, cy);
                g.RotateTransform(180.0f);
                g.TranslateTransform(-cx, -cy);
                InterpolationMode prevIM = g.GetInterpolationMode();
                g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                g.DrawImage(assetBM, artX, artY, artW, artH);
                g.SetInterpolationMode(prevIM);
                g.SetTransform(&beforeRotate);
            } else if (inExpansion) {
                // 9-slice with 14px corners (matches the gem corner art).
                DrawButton9Slice(g, assetBM, artX, artY, artW, artH, 14);
            } else if (modLinkFallback) {
                // Square 85×85 button reusing the 254×54 nexus/update art.
                // The art's corner ornaments are ~14px; 9-slicing with that
                // inset keeps them pixel-perfect while the middle stretches
                // both horizontally and vertically to fill the square.
                DrawButton9Slice(g, assetBM, artX, artY, artW, artH, 14);
            } else {
                InterpolationMode prevIM = g.GetInterpolationMode();
                g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                g.DrawImage(assetBM, artX, artY, artW, artH);
                g.SetInterpolationMode(prevIM);
            }
            drewAsset = true;
        } else {
            // Programmatic fallback (current look). Doesn't get the
            // hover/click transform — the state is conveyed by the
            // OPDrawBtnFrame "highlight" parameter instead.
            if (applyTransform) g.SetTransform(&prevMatrix);
            applyTransform = false;
            bool highlight = hover || pressed || dirty;
            OPDrawBtnFrame(g, artX, artY, artW, artH, highlight);
        }

        // ── Label text ──────────────────────────────────────────────────
        // Some kinds have their label baked into the art (Refresh's text,
        // Ellipse's "...", the chevron direction in Arrow). Skip live text
        // rendering only when the asset actually rendered — if the asset
        // is missing we still draw the fallback caption (▼/▲, "Refresh",
        // "...") so the button stays identifiable.
        //
        // The three ModLink* kinds (Docs/Discord/Website) work the
        // opposite way: their per-button assets carry the icon directly
        // and the HWND's text is just a single-letter ID for the
        // fallback path. So we suppress the label when the per-button
        // asset drew, and SHOW it when the fallback 9-sliced
        // btn_nexus_update was used instead.
        bool isModLinkKind = (st.kind == ButtonKind::ModLinkDocs
                           || st.kind == ButtonKind::ModLinkDiscord
                           || st.kind == ButtonKind::ModLinkWebsite);
        bool skipLabel = drewAsset && (st.kind == ButtonKind::Refresh
                                    || st.kind == ButtonKind::Ellipse
                                    || st.kind == ButtonKind::Arrow
                                    || (isModLinkKind && !modLinkFallback));
        if (!skipLabel) {
            wchar_t label[128] = {};
            GetWindowText(ctl, label, 127);

            StringFormat sfC;
            sfC.SetAlignment(StringAlignmentCenter);
            sfC.SetLineAlignment(StringAlignmentCenter);
            bool highlight = hover || pressed || dirty;
            SolidBrush lbl(disabled ? Tok::TextDim
                                    : (highlight ? Tok::GoldBright : Tok::Gold));
            Font* labelFont = g_fBtn;
            if      (st.kind == ButtonKind::Play)        labelFont = g_fBtnLaunch;
            else if (inExpansion)                        labelFont = g_fNavSm;
            else if (st.kind == ButtonKind::Nav)         labelFont = g_fNav;
            else if (st.kind == ButtonKind::NexusUpdate) labelFont = g_fNavSm;   // +40% over g_fBtn
            else if (isModLinkKind)                      labelFont = g_fBtnLaunch;   // single-letter fallback — go big

            // Auto-shrink: if the label is wider than the button's interior,
            // step down to a smaller font. This handles long expansion-panel
            // labels (e.g. "AFJ Pro Text Editor") in 310px-wide Nav buttons
            // without manual per-kind sizing.
            const REAL labelPad = 24.0f;       // horizontal padding inside button
            RectF measureRect;
            g.MeasureString(label, -1, labelFont,
                            RectF(0, 0, 4096, 4096), &sfC, &measureRect);
            if (measureRect.Width > (REAL)artW - labelPad) {
                // Try a smaller font: Nav 26 → 20, Play 20 → 16, else 13 → no change
                if (labelFont == g_fNav)              labelFont = g_fBtnLaunch;  // 20px
                else if (labelFont == g_fBtnLaunch)   labelFont = g_fBtn;        // 13px
            }
            // Second pass — if still too wide, last resort to g_fBtn.
            g.MeasureString(label, -1, labelFont,
                            RectF(0, 0, 4096, 4096), &sfC, &measureRect);
            if (measureRect.Width > (REAL)artW - labelPad && labelFont != g_fBtn) {
                labelFont = g_fBtn;
            }

            // Label renders centered in the art rect (not the full HWND
            // rect), under the same transform as the asset.
            g.DrawString(label, -1, labelFont,
                         RectF((REAL)artX, (REAL)artY,
                               (REAL)artW, (REAL)artH),
                         &sfC, &lbl);
        }

        // Restore the prior transform (if we set one).
        if (applyTransform) g.SetTransform(&prevMatrix);
        return TRUE;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wp), ev = HIWORD(wp);
        (void)ev;

        // ── Left rail nav (hyperlink-style: opens external paths/files) ──
        if (id == IDC_NAV_MODS) {
            wstring p = g_cfg.d2rPath + L"\\mods";
            CreateDirectoryW(p.c_str(), nullptr);
            ShellExecute(hw, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        else if (id == IDC_NAV_OPTIONS) {
            wstring p = g_cfg.d2rPath + L"\\D2RLoader.ini";
            ShellExecute(hw, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        else if (id == IDC_NAV_LOGS) {
            wstring p = g_cfg.d2rPath + L"\\logs";
            CreateDirectoryW(p.c_str(), nullptr);
            ShellExecute(hw, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        else if (id == IDC_NAV_HELP) {
            wstring p = AppDir() + L"\\FAQ.txt";
            ShellExecute(hw, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        else if (id == IDC_NAV_ABOUT) {
            wstring p = AppDir() + L"\\README.txt";
            ShellExecute(hw, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        else if (id == IDC_NAV_EXIT) {
            PostMessage(hw, WM_CLOSE, 0, 0);
            return 0;
        }

        // ── Loader Directory "..." button → folder picker ────────────────
        else if (id == IDC_LOADER_DIR_BTN) {
            PromptForD2RPath(hw);
            return 0;
        }

        // ── Mod list right-click context menu commands ─────────────────────
        // All three need a valid g_selMod (the WM_CONTEXTMENU handler
        // already gated on this before showing the menu, but commands
        // can theoretically arrive from accelerator routes too, so
        // double-check).
        else if (id == IDM_MOD_OPEN_FOLDER && g_selMod >= 0
                 && g_selMod < (int)g_mods.size()) {
            ShellExecuteW(hw, L"explore",
                          g_mods[g_selMod].dir.c_str(),
                          nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }

        else if (id == IDM_MOD_BACKUP_SAVES && g_selMod >= 0
                 && g_selMod < (int)g_mods.size()) {
            // Snapshot the mod's save folder. On success, pop Explorer
            // open on the new timestamped backup so the user sees what
            // was created and can verify the snapshot. On failure
            // (no save folder yet — user hasn't played this mod since
            // installing it), silent no-op; there's literally nothing
            // to back up.
            const ModInfo& m = g_mods[g_selMod];
            wstring backupDir;
            if (BackupModSavesByPath(m.savePath, &backupDir)
                && !backupDir.empty()) {
                ShellExecuteW(hw, L"open", backupDir.c_str(),
                              nullptr, nullptr, SW_SHOWNORMAL);
            }
            return 0;
        }

        else if (id == IDM_MOD_REZIP && g_selMod >= 0
                 && g_selMod < (int)g_mods.size()) {
            // Build the default save filename: <modfolder>_<version>.zip
            // (or just <modfolder>.zip if no version was set in modinfo).
            const ModInfo& m = g_mods[g_selMod];
            wstring defaultName = m.folder;
            if (!m.version.empty()) {
                defaultName += L"_" + m.version;
            }
            // Sanitize — version strings can contain anything; strip
            // filesystem-reserved characters so the default name doesn't
            // confuse the save dialog.
            for (wchar_t& c : defaultName) {
                if (c == L'<' || c == L'>' || c == L':' || c == L'"' ||
                    c == L'/' || c == L'\\' || c == L'|' || c == L'?' ||
                    c == L'*') c = L'_';
            }
            defaultName += L".zip";

            wchar_t fileBuf[MAX_PATH * 2] = {};
            wcsncpy_s(fileBuf, MAX_PATH * 2,
                      defaultName.c_str(), _TRUNCATE);

            OPENFILENAMEW ofn = { sizeof(ofn) };
            ofn.hwndOwner   = hw;
            ofn.lpstrFilter = L"Zip Archive (*.zip)\0*.zip\0All Files\0*.*\0";
            ofn.lpstrFile   = fileBuf;
            ofn.nMaxFile    = MAX_PATH * 2;
            ofn.lpstrTitle  = L"Re-zip Mod";
            ofn.lpstrDefExt = L"zip";
            ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            if (!GetSaveFileNameW(&ofn)) return 0;

            // Split the mod's full dir into parent + folder name so
            // tar can be told "from this parent, archive that folder"
            // — the resulting zip then has <folder>/... at its root,
            // which is what the launcher's drag-drop installer expects.
            wstring modDir = m.dir;
            size_t slash = modDir.find_last_of(L"\\/");
            wstring parentDir, folderName;
            if (slash == wstring::npos) {
                parentDir  = L".";
                folderName = modDir;
            } else {
                parentDir  = modDir.substr(0, slash);
                folderName = modDir.substr(slash + 1);
            }

            // tar.exe -a -cf <output> -C <parent> <folder>
            //   -a   auto-detect archive format from output extension
            //   -cf  create archive at the named file
            //   -C   chdir before adding entries (so the entries are
            //        relative to <parent>, not absolute)
            wchar_t winDir[MAX_PATH];
            GetWindowsDirectoryW(winDir, MAX_PATH);
            wstring tarPath = wstring(winDir) + L"\\System32\\tar.exe";

            wstring cmd = L"\"" + tarPath + L"\" -a -cf \""
                        + wstring(fileBuf) + L"\" -C \""
                        + parentDir + L"\" \"" + folderName + L"\"";

            STARTUPINFOW si = { sizeof(si) };
            si.dwFlags     = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi = {};
            vector<wchar_t> cmdLine(cmd.begin(), cmd.end());
            cmdLine.push_back(0);
            if (CreateProcessW(nullptr, cmdLine.data(),
                               nullptr, nullptr, FALSE,
                               CREATE_NO_WINDOW, nullptr, nullptr,
                               &si, &pi)) {
                WaitForSingleObject(pi.hProcess, INFINITE);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                // Open Explorer focused on the new zip so the user can
                // immediately grab it. /select, highlights the file
                // rather than just opening its parent folder.
                wstring args = L"/select,\"" + wstring(fileBuf) + L"\"";
                ShellExecuteW(hw, L"open", L"explorer.exe",
                              args.c_str(), nullptr, SW_SHOWNORMAL);
            }
            return 0;
        }

        else if (id == IDM_MOD_UNINSTALL && g_selMod >= 0
                 && g_selMod < (int)g_mods.size()) {
            // Themed confirm dialog. Returns 1 if the user clicks
            // Delete, 0 otherwise (Cancel / Esc / X). Defined alongside
            // the other themed dialogs after MainProc.
            const ModInfo& m = g_mods[g_selMod];
            wstring modName = m.title.empty() ? m.folder : m.title;
            wstring modDir  = m.dir;   // copy — RefreshMods below
                                       // invalidates g_mods indices
            if (ShowUninstallConfirmDialog(hw, modName) == 1) {
                DeleteFolderRecursive(modDir);
                RefreshMods();
                if (g_hwList) InvalidateRect(g_hwList, nullptr, FALSE);
                InvalidateRect(hw, nullptr, FALSE);
            }
            return 0;
        }

        // ── Center column: Nexus Mod Directory + Update Selected Mod ──────
        else if (id == IDC_BROWSE_MODS) {
            ShellExecute(hw, L"open",
                         L"https://www.nexusmods.com/diablo2resurrected",
                         nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        else if (id == IDC_UPDATE_MOD && g_selMod >= 0) {
            // Open the mod's source URL — user downloads + unpacks manually
            // (no auto-install for liability reasons).
            auto it = g_updateInfo.find(g_mods[g_selMod].folder);
            if (it != g_updateInfo.end() && !it->second.sourceUrl.empty()) {
                ShellExecute(hw, L"open", it->second.sourceUrl.c_str(),
                             nullptr, nullptr, SW_SHOWNORMAL);
            }
            return 0;
        }

        // ── Bottom expansion toggle ──────────────────────────────────────
        else if (id == IDC_EXPAND_TOGGLE) {
            g_bottomExpanded = !g_bottomExpanded;
            if (g_hwExpandToggle)
                SetWindowText(g_hwExpandToggle, g_bottomExpanded ? L"\u25B2" : L"\u25BC");
            // Show/hide the bottom-panel buttons en masse
            int sw = g_bottomExpanded ? SW_SHOW : SW_HIDE;
            for (HWND h : g_hwBottomTools) if (h) ShowWindow(h, sw);
            for (HWND h : g_hwBottomRefs)  if (h) ShowWindow(h, sw);
            for (HWND h : g_hwBottomDls)   if (h) ShowWindow(h, sw);
            RepositionForExpansion();        // resize → WM_SIZE → Layout
            // One clean invalidate. Painting is double-buffered and the
            // window class no longer has CS_*REDRAW, so this repaints to
            // the back buffer and blits once — no flicker.
            InvalidateRect(hw, nullptr, FALSE);
            return 0;
        }

        // ── Mod Description per-mod link buttons ─────────────────────────
        else if (id == IDC_MOD_DISCORD && g_selMod >= 0) {
            wstring t = ResolveModLink(g_mods[g_selMod].discordUrl, g_mods[g_selMod].dir);
            ShellExecute(hw, L"open", t.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        else if (id == IDC_MOD_DOCS && g_selMod >= 0) {
            wstring t = ResolveModLink(g_mods[g_selMod].docsUrl, g_mods[g_selMod].dir);
            ShellExecute(hw, L"open", t.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        else if (id == IDC_MOD_WEBSITE && g_selMod >= 0) {
            wstring t = ResolveModLink(g_mods[g_selMod].websiteUrl, g_mods[g_selMod].dir);
            ShellExecute(hw, L"open", t.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }

        // ── Bottom panel: tool launchers ─────────────────────────────────
        else if (id >= IDC_TOOL_FIRST && id < IDC_TOOL_FIRST + 6) {
            int slot = id - IDC_TOOL_FIRST;
            wstring* paths[6] = {
                &g_cfg.toolExcel,     // "Edit TXT Files"
                &g_cfg.toolSprite,    // "Edit Sprite Files"
                &g_cfg.toolStrings,   // "Edit JSON Files"
                &g_cfg.toolModels,
                &g_cfg.toolTextures,
                &g_cfg.toolParticles,
            };
            const wchar_t* hintExe[6] = {
                L"AFJ Sheet Editor Pro.exe",
                L"Eez's Sprite Editor.exe",
                L"Code.exe",                 // VS Code
                L"Blender.exe",
                L"paint.net.exe",
                L"Particles.exe",
            };
            const wchar_t* friendly[6] = {
                L"TXT/Excel editor",
                L"Sprite editor",
                L"JSON/text editor",
                L"Models editor",
                L"Textures editor",
                L"Particles editor",
            };
            LaunchTool(hw, *paths[slot], hintExe[slot], friendly[slot]);
            return 0;
        }

        // ── Bottom panel: references (open URLs) ─────────────────────────
        else if (id >= IDC_REF_FIRST && id < IDC_REF_FIRST + 3) {
            int slot = id - IDC_REF_FIRST;
            const wchar_t* urls[3] = {
                L"https://eezstreet.github.io/d2rdoc/index.html",                  // Eez's File Guides
                L"https://d2mods.info/home.php",                                   // Phrozen Keep
                L"https://www.theamazonbasin.com/wiki/index.php/Diablo_II",        // Amazon Basin
            };
            ShellExecute(hw, L"open", urls[slot], nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }

        // ── Bottom panel: downloads (open URLs) ──────────────────────────
        else if (id >= IDC_DL_FIRST && id < IDC_DL_FIRST + 3) {
            int slot = id - IDC_DL_FIRST;
            const wchar_t* urls[3] = {
                L"https://www.afjsoftware.com/",
                L"https://d2mods.info/eezstreams/",
                L"https://code.visualstudio.com/",
            };
            ShellExecute(hw, L"open", urls[slot], nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }

        if (id == IDC_REFRESH_BTN) {
            // Manual mod-list rescan. Clears the "dirty" flag (set by the
            // watcher thread on directory changes) and triggers a fresh
            // scan. Repainting just the refresh button avoids flickering
            // the rest of the UI when nothing has actually changed.
            bool wasDirty = g_modsDirty;
            g_modsDirty = false;
            RefreshMods();
            KickUpdateChecks(true);    // force-refetch all opted-in mods
            if (g_hwRefresh && wasDirty)
                InvalidateRect(g_hwRefresh, nullptr, FALSE);
        }
        else if (id == IDC_LAUNCH_BTN && g_selMod >= 0) {
            // Capture the click tick so the post-launch poll timer can
            // be scheduled "10 seconds from THIS click" — not "10 seconds
            // from after WaitForInputIdle returns". WaitForInputIdle can
            // burn anywhere from 0 to 5000 ms; without anchoring on the
            // click time, the first poll could fire as early as t=5s.
            // The current D2RLoader has trouble during its initial
            // launch phase (the loader process state oscillates while
            // it spawns and attaches to D2R.exe), so we deliberately
            // hold off the first poll until ~10 s after click.
            DWORD launchClickTick = GetTickCount();

            // D2RLoader.exe is the bootstrap shim that injects mod hooks
            // before D2R.exe starts. Mod-aware launches should always go
            // through the loader, not the bare game executable.
            wstring exe  = g_cfg.d2rPath + L"\\D2RLoader.exe";
            wstring cmd  = L"\"" + exe + L"\" " + BuildLaunchArgs();

            // If we already have a previous launch tracked, stop polling
            // it before starting a new one — only one tracked launch at
            // a time. (We no longer hold the loader handle, so there's
            // nothing to close — just clear the flags and kill the
            // timer.)
            if (g_d2rTracking) {
                g_d2rTracking   = false;
                g_d2rEverSeen   = false;
                g_pollFirstShot = false;
                KillTimer(hw, IDT_D2R_POLL);
            }

            STARTUPINFO si = { sizeof(si) };
            PROCESS_INFORMATION pi = {};
            wstring cmdMut = cmd;
            if (CreateProcess(nullptr, &cmdMut[0], nullptr, nullptr, FALSE, 0,
                              nullptr, g_cfg.d2rPath.c_str(), &si, &pi)) {
                // Close the thread handle immediately; KEEP the process
                // handle so we can poll exit status.
                CloseHandle(pi.hThread);

                // Confirm D2R actually started (wait briefly for it to
                // become ready). WaitForInputIdle returns when the
                // process has finished initial startup OR when it exits.
                WaitForInputIdle(pi.hProcess, 5000);

                // Branch on the user's chosen behavior
                switch (g_cfg.launchBehavior) {
                case LB_CLOSE:
                    // Close the launcher entirely; D2R keeps running on its own
                    CloseHandle(pi.hProcess);
                    PostMessage(hw, WM_CLOSE, 0, 0);
                    break;

                case LB_MINIMIZE:
                    // Drop the D2RLoader handle — we don't track by
                    // handle anymore. The loader is a shim that exits
                    // shortly after spawning D2R.exe, and polling its
                    // handle would falsely fire "D2R exited" while
                    // D2R was still loading. Instead we poll the
                    // process table by name; the launcher restores
                    // only when D2R.exe itself is gone.
                    CloseHandle(pi.hProcess);
                    g_d2rTracking      = true;
                    g_d2rEverSeen      = false;
                    g_d2rLaunchTick    = launchClickTick;
                    g_d2rGameStartTick = 0;   // set on first poll where
                                              //   the game is visible
                    // Snapshot the launching mod's folder so the
                    // playtime accumulator can be credited to the right
                    // entry even if the user changes the list
                    // selection while the game is up.
                    if (g_selMod >= 0 && g_selMod < (int)g_mods.size()) {
                        g_d2rGameModFolder = g_mods[g_selMod].folder;
                    } else {
                        g_d2rGameModFolder.clear();
                    }

                    // SW_SHOWMINNOACTIVE rather than SW_MINIMIZE: by the
                    // time we get here, WaitForInputIdle has yielded for
                    // up to 5 seconds while D2R initialized, and D2R
                    // has typically grabbed foreground already.
                    // SW_MINIMIZE's "minimize + activate next in Z-order"
                    // semantics aren't reliable for a WS_POPUP window
                    // when the calling thread no longer owns the
                    // foreground — the launcher just sits in front of
                    // the game instead of actually minimizing.
                    // SW_SHOWMINNOACTIVE skips the activation step
                    // entirely and minimizes regardless of foreground
                    // ownership, which is what we want here (D2R is
                    // already foreground; we don't need to activate
                    // anything else).
                    ShowWindow(hw, SW_SHOWMINNOACTIVE);
                    // First poll fires ~10 s after the click — not 1 s
                    // after this SetTimer. The loader's initial launch
                    // phase + D2R's startup can take several seconds,
                    // and polling too early gives a brief window where
                    // ProcessExistsByName(L"D2R.exe") returns false
                    // before the game has actually spawned. The 10 s
                    // grace lets D2R reliably appear in the process
                    // table before the first check; the poll handler
                    // resets the timer to the normal 1 s cadence on
                    // its first fire.
                    {
                        DWORD elapsed = GetTickCount() - launchClickTick;
                        DWORD initialMs = (elapsed >= 10000)
                                          ? 1000
                                          : (10000 - elapsed);
                        g_pollFirstShot = true;
                        SetTimer(hw, IDT_D2R_POLL, initialMs, nullptr);
                    }
                    break;

                case LB_STAY:
                default:
                    // Don't track; let D2R run independently
                    CloseHandle(pi.hProcess);
                    break;
                }
            } else {
                MessageBox(hw, (L"Failed to launch D2RLoader.exe at:\n" + exe).c_str(),
                           L"Launch Failed", MB_OK | MB_ICONERROR);
            }
        }
        return 0;
    }

    case WM_USER + 3: {
        // ML_RESCAN — empty-state "Restart Search" link clicked, or the
        // settings dialog committed a new D2R path.
        RefreshMods();
        StartModsWatcher();      // restart in case the d2r path changed
        return 0;
    }

    case MSG_MODS_DIRTY: {
        // Watcher thread saw a directory change. Instead of auto-rescanning
        // (which would flicker the hero panel as the whole UI repaints),
        // just flag the state and invalidate the refresh button so it can
        // show a "stale" indicator. The user clicks the button to actually
        // refresh — that gives them control over when the redraw happens.
        if (!g_modsDirty) {
            g_modsDirty = true;
            if (g_hwRefresh) InvalidateRect(g_hwRefresh, nullptr, FALSE);
        }
        return 0;
    }

    case MSG_UPDATE_CHECK_DONE: {
        // A background update-fetch worker finished. Repaint the mod list
        // (gold ↑ badge) and let the right-column Mod Description panel
        // refresh its bottom "Update available" strip when it's rebuilt.
        if (g_hwList) InvalidateRect(g_hwList, nullptr, FALSE);
        return 0;
    }

    case WM_DROPFILES: {
        // User dropped one or more files onto the launcher. Pull the paths,
        // filter to .zip, enqueue for the install worker. Non-zip files
        // are silently ignored (drag-and-drop is a low-friction action; a
        // popup for "this isn't a zip" would feel scoldy). The worker
        // thread reports back via MSG_ZIP_CONFLICT_DIALOG (per-zip
        // collision question) and MSG_ZIP_QUEUE_DONE (queue drained).
        HDROP hDrop = (HDROP)wp;
        UINT n = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        vector<wstring> paths;
        paths.reserve(n);
        for (UINT i = 0; i < n; ++i) {
            wchar_t p[MAX_PATH * 2];
            UINT len = DragQueryFileW(hDrop, i, p, MAX_PATH * 2);
            if (len > 0) paths.emplace_back(p);
        }
        DragFinish(hDrop);
        EnqueueZipsForInstall(paths);
        return 0;
    }

    case MSG_ZIP_CONFLICT_DIALOG: {
        // Worker thread is asking how to handle a folder collision.
        // SendMessage blocks the worker until we return, so just show
        // the modal dialog inline and write the choice back through lp.
        ConflictDialogParam* p = (ConflictDialogParam*)lp;
        // Parent the dialog to the progress dialog if it's up — that
        // way the conflict modal opens centered over progress and the
        // EnableWindow toggling gates input correctly.
        HWND parent = g_progressDlg ? g_progressDlg : hw;
        if (p) p->choice = ShowConflictDialog(parent, p->modName);
        return 0;
    }

    case MSG_ZIP_NEED_PATH: {
        // Worker says g_cfg.d2rPath is empty and there's a zip waiting.
        // Show the themed "Set Path / Cancel" dialog; if Set Path, the
        // dialog itself runs SHBrowseForFolder and populates g_cfg before
        // returning. Result: 1 = path set (worker continues with zip),
        // 0 = cancel (worker skips zip).
        HWND parent = g_progressDlg ? g_progressDlg : hw;
        return ShowSetPathDialog(parent);
    }

    case MSG_ZIP_NO_MODINFO: {
        // Worker found a zip with no modinfo.json. Surface the error to
        // the user (modal OK) so they know which archive was rejected
        // and can contact the mod author.
        const wstring* zipName = (const wstring*)lp;
        HWND parent = g_progressDlg ? g_progressDlg : hw;
        if (zipName) ShowNoModInfoDialog(parent, *zipName);
        return 0;
    }

    case MSG_ZIP_PROGRESS_SHOW: {
        // Worker has work to do — bring up the progress dialog and pause
        // the main UI. The dialog stays up until MSG_ZIP_PROGRESS_HIDE.
        ShowProgressDialog(hw);
        return 0;
    }

    case MSG_ZIP_PROGRESS_UPDATE: {
        // Worker advancing through stages. Synchronous update so the
        // worker can rely on the new state being painted before it
        // proceeds (matters for the "Done" stage right before cleanup —
        // user sees the full bar briefly).
        ProgressUpdate* p = (ProgressUpdate*)lp;
        if (p) UpdateProgressDialog(*p);
        return 0;
    }

    case MSG_ZIP_PROGRESS_HIDE: {
        // Worker finished — tear down the progress dialog and re-enable
        // the main UI.
        HideProgressDialog();
        return 0;
    }

    case MSG_ZIP_QUEUE_DONE: {
        // All queued zips processed — refresh the mod list so newly
        // installed mods appear (and updated ones get their version
        // re-read from disk).
        RefreshMods();
        InvalidateRect(hw, nullptr, FALSE);
        return 0;
    }

    case MSG_LUPOPUP_STATUS: {
        // Launcher self-update worker is advancing through statuses.
        // wp = 1 (Downloading) / 2 (Updating) / 3 (Complete). When
        // status hits 3, stamp the tick so the timer-driven enable
        // logic for the Restart button can fire 500 ms later.
        int newStatus = (int)wp;
        g_lupopupStatus = newStatus;
        if (newStatus >= 3 && g_lupopupCompleteTick == 0) {
            g_lupopupCompleteTick = GetTickCount();
        }
        if (g_lupopupWnd) {
            InvalidateRect(g_lupopupWnd, nullptr, TRUE);
            UpdateWindow(g_lupopupWnd);
        }
        return 0;
    }

    case MSG_LAUNCHER_UPDATE_AVAILABLE: {
        // Worker thread says a newer release was found on GitHub.
        // Verify it's actually newer than us — if so, set the glow
        // flag and repaint the version label area so the visual
        // indicator appears. THEN check the skipped-version gate
        // for the dialog: glow is unconditional (so the user has a
        // visual cue), but the modal dialog respects Skip unless
        // the user just clicked the label to force a re-prompt.
        const wstring& tag = g_launcherUpdateLatestTag;
        if (tag.empty()) return 0;
        if (CompareVersions(tag, LAUNCHER_VERSION) <= 0) return 0;

        if (!g_launcherUpdateAvailable) {
            g_launcherUpdateAvailable = true;
            // Repaint the version label so the glow appears.
            // Invalidating the whole window is overkill but cheap.
            InvalidateRect(hw, nullptr, FALSE);
        }

        // Dialog gate: skipped tag suppresses unless force flag set.
        if (!g_forceUpdatePrompt) {
            const wstring& skipped = g_cfg.skippedLauncherVersion;
            if (!skipped.empty()
                && CompareVersions(tag, skipped) <= 0) return 0;
        }
        g_forceUpdatePrompt = false;   // consume the one-shot flag

        int choice = ShowLauncherUpdateDialog(hw, tag);
        if (choice == 1) {
            // Update — kick off the in-place self-replacement on a
            // worker thread. Shows the progress dialog (stage 0) and
            // returns immediately; the worker advances through stages
            // 0 → 2 → 4. At stage 4 a Restart button appears on the
            // progress dialog, and clicking that triggers the actual
            // launcher relaunch.
            StartLauncherUpdateInstall(hw);
        } else if (choice == 2) {
            // Skip Version — remember this exact tag so we don't
            // re-prompt for it on subsequent launches.
            g_cfg.skippedLauncherVersion = tag;
            SaveCfg();
        }
        // choice == 0 (Ignore): no persistent state change; we'll
        // re-prompt on next launch when the worker fires again.
        return 0;
    }

    case ML_NOTIFY_SELECT: {
        // Selection-change notification from the mod list.
        // wp = new index, lp = unused.
        int s = (int)wp;
        if (s >= 0 && s < (int)g_mods.size() && s != g_selMod) {
            int prevSel = g_selMod;
            g_selMod = s;
            g_cfg.lastMod = g_mods[s].folder;
            SaveCfg();
            LoadModSettings(g_mods[s]);
            // Reset seed-input transient state — the new mod has its
            // own useSeed / seedArg, and a stale caret position from
            // the previous mod's value would point past the end of
            // the new value (or into the wrong characters).
            if (g_seedInputFocused) {
                g_seedInputFocused = false;
                KillTimer(hw, IDT_SEED_CARET);
            }
            if (g_seedDragging) {
                g_seedDragging = false;
                ReleaseCapture();
            }
            g_seedCaretPos     = (int)g_modSettings.seedArg.size();
            g_seedSelStart     = -1;
            g_seedCaretVisible = true;
            RefreshModDescriptionLinks();
            {
                RECT rc; GetClientRect(hw, &rc);
                Layout(rc.right, rc.bottom);
            }
            // Mod list: only the two rows that visually change need
            // repainting (old loses selection, new gains it). The rest
            // of the list is identical.
            if (g_hwList) {
                RECT r;
                if (prevSel >= 0 && MLRowClientRect(g_hwList, prevSel, &r))
                    InvalidateRectL(g_hwList, &r, FALSE);
                if (MLRowClientRect(g_hwList, s, &r))
                    InvalidateRectL(g_hwList, &r, FALSE);
            }
            // Main window: only the right column changes (description text,
            // links, flag states for the new mod, cmd preview). Left rail,
            // mod list area, and bottom panel are unchanged.
            {
                RECT rc; GetClientRect(hw, &rc);
                BodyLayout B = ComputeBodyLayout(U(rc.right), U(rc.bottom));
                // B.rightX is logical; build the invalidate rect in
                // logical coords too and let InvalidateRectL scale it
                // for the Win32 call.
                RECT right = { B.rightX, 0, U(rc.right), U(rc.bottom) };
                InvalidateRectL(hw, &right, FALSE);
            }
            if (g_hwLaunch) EnableWindow(g_hwLaunch, TRUE);
        }
        return 0;
    }

    case OPT_NOTIFY_CHANGED: {
        // Live cmd preview lives in the right column (rebuilt Commit 5).
        InvalidateRect(hw, nullptr, FALSE);
        return 0;
    }

    // ── Custom title bar: drag region (WM_POPUP, no system caption) ──
    case WM_NCHITTEST: {
        // Convert screen to client (Win32 gives physical), then to logical
        // so the title-bar hit-tests below run in the same coord system
        // as the painted UI.
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hw, &p);
        p.x = U(p.x);
        p.y = U(p.y);
        // Buttons should NOT be HTCAPTION (else Windows steals the click).
        if (TBHitTest(hw, p.x, p.y) >= 0) return HTCLIENT;
        // Drag band: anywhere in the top filigree that isn't a button.
        if (TBPointInDragBand(hw, p.x, p.y)) return HTCAPTION;
        return HTCLIENT;
    }

    // ── Body-region clicks (flag checkboxes + loader-options dropdowns) ──
    case WM_LBUTTONDOWN: {
        // Mouse coords arrive in PHYSICAL pixels (Win32 doesn't know
        // about g_scale). Convert once at entry so every hit-test below
        // compares against LOGICAL rects directly — title-bar buttons,
        // flag checkboxes, dropdown rects, and so on all live in logical
        // space (the same one as Layout/LO::*).
        int x = U(GET_X_LPARAM(lp)), y = U(GET_Y_LPARAM(lp));

        // Title-bar button press
        int tbHit = TBHitTest(hw, x, y);
        if (tbHit >= 0) {
            g_tbPressed = tbHit;
            SetCapture(hw);
            // Repaint the title-bar button area (covers idle→click swap).
            RECT inv;
            inv.left   = 0;
            inv.top    = 0;
            inv.bottom = TB_BTN_INSET_T + TB_BTN_H + 8;
            RECT cr; GetClientRect(hw, &cr);
            inv.right  = U(cr.right);   // logical width to match inv.bottom
            InvalidateRectL(hw, &inv, FALSE);
            return 0;
        }

        // Version label (below the logo). Clicking forces a fresh
        // update check that bypasses the skipped-version gate, so a
        // user who picked Skip can still get prompted to update at
        // will. ONLY active when an update has been detected — until
        // the initial startup check confirms a newer release exists,
        // the label is inert (matches the cursor gate in WM_SETCURSOR).
        // If a worker is already in flight, KickoffLauncherUpdateCheck
        // silently no-ops — they'll see the result of the running
        // check.
        if (g_launcherUpdateAvailable) {
            POINT pt = { x, y };
            if (PtInRect(&g_versionLabelRect, pt)) {
                g_forceUpdatePrompt = true;
                KickoffLauncherUpdateCheck();
                return 0;
            }
        }

        RECT rc; GetClientRect(hw, &rc);

        // Flag checkboxes in the Launch Options panel. Hit rect is JUST
        // the 27×28 checkbox glyph (not the whole row), so clicks on the
        // label text don't toggle.
        BodyLayout B = ComputeBodyLayout(U(rc.right), U(rc.bottom));
        int flagCount = (int)(sizeof(FLAGS) / sizeof(FLAGS[0]));
        for (int i = 0; i < flagCount; ++i) {
            RECT fr = BodyFlagRect(B, i);
            constexpr int CB_W = 27, CB_H = 28;
            int cbY = fr.top + ((fr.bottom - fr.top) - CB_H) / 2;
            RECT cb = { fr.left, cbY, fr.left + CB_W, cbY + CB_H };
            if (x >= cb.left && x < cb.right && y >= cb.top && y < cb.bottom) {
                const FlagDef& f = FLAGS[i];
                if (!f.isLocked) {
                    g_modSettings.*(f.member) = !(g_modSettings.*(f.member));
                    if (g_selMod >= 0 && g_selMod < (int)g_mods.size())
                        SaveModSettings(g_mods[g_selMod]);
                    InvalidateRectL(hw, &fr, FALSE);
                    // Invalidate from the seed row down through the cmd
                    // preview: the seed row itself may not change visually
                    // here, but the cmd preview's wrapped-line count (and
                    // therefore its height) can shift when a flag toggle
                    // adds/removes a token from the args string.
                    RECT below = {
                        B.loX + 12, B.loSeedY,
                        B.loX + B.loW - 12,
                        B.loCmdPreviewY + 80     // generous — covers max cmd box height
                    };
                    InvalidateRectL(hw, &below, FALSE);
                }
                return 0;
            }
        }

        // Seed checkbox — toggles whether -seed is appended. Independent
        // of the typed value: unchecking preserves seedArg (so re-checking
        // doesn't lose what was typed), and typing into the input while
        // unchecked is fine (the value waits until you toggle on).
        {
            RECT scb = BodySeedCheckRect(B);
            if (x >= scb.left && x < scb.right && y >= scb.top && y < scb.bottom) {
                g_modSettings.useSeed = !g_modSettings.useSeed;
                if (g_selMod >= 0 && g_selMod < (int)g_mods.size())
                    SaveModSettings(g_mods[g_selMod]);
                RECT below = {
                    B.loX + 12, B.loSeedY,
                    B.loX + B.loW - 12,
                    B.loCmdPreviewY + 80
                };
                InvalidateRectL(hw, &below, FALSE);
                return 0;
            }
        }

        // Seed text input — clicking inside the input area focuses it
        // (if not already focused), positions the caret at the click
        // X, and arms drag-selection. Subsequent clicks reposition the
        // caret too (matches the EDIT control). The caret-position
        // hit-test runs through SeedXToCaretIndex which mirrors paint's
        // ScaleTransform so the index is exact at every UI scale.
        // The actual keyboard handling lives in the main window's
        // WM_CHAR / WM_KEYDOWN cases below.
        {
            RECT inR = BodySeedInputRect(B);
            if (x >= inR.left && x < inR.right && y >= inR.top && y < inR.bottom) {
                if (!g_seedInputFocused) {
                    g_seedInputFocused = true;
                    SetFocus(hw);
                    SetTimer(hw, IDT_SEED_CARET, SEED_CARET_BLINK_MS, nullptr);
                }
                REAL inTextX = (REAL)(inR.left + 6);
                REAL inTextY = (REAL)inR.top;
                int idx = SeedXToCaretIndex(x, inTextX, inTextY);
                g_seedCaretPos     = idx;
                g_seedSelStart     = idx;       // anchor at click; selection collapses initially
                g_seedCaretVisible = true;
                g_seedDragging     = true;
                SetCapture(hw);                  // route WM_MOUSEMOVE / WM_LBUTTONUP here
                InvalidateRectL(hw, &inR, FALSE);
                return 0;
            }
        }

        // Click anywhere ELSE inside the launch-options area while the
        // seed input is focused: blur it. This is the analog of "click
        // outside an EDIT control to commit". The actual blur logic
        // (commit + recents) lives in a helper used here and on Enter.
        // We do this BEFORE the flag loop and the arrow popup hit so a
        // click on either of those still applies, just with the input
        // un-focused first.
        if (g_seedInputFocused) {
            RECT inR = BodySeedInputRect(B);
            bool insideInput = (x >= inR.left && x < inR.right
                              && y >= inR.top && y < inR.bottom);
            if (!insideInput) {
                g_seedInputFocused = false;
                g_seedSelStart     = -1;
                KillTimer(hw, IDT_SEED_CARET);
                CommitTypedSeedToRecents(g_modSettings.seedArg);
                if (g_selMod >= 0 && g_selMod < (int)g_mods.size())
                    SaveModSettings(g_mods[g_selMod]);
                RECT below = {
                    B.loX + 12, B.loSeedY,
                    B.loX + B.loW - 12,
                    B.loCmdPreviewY + 80
                };
                InvalidateRectL(hw, &below, FALSE);
                // Fall through — the click may still hit another control.
            }
        }

        // Update-bar "[Details]" hotspot in the Mod Description panel
        if (g_selMod >= 0 && g_selMod < (int)g_mods.size()) {
            auto it = g_updateInfo.find(g_mods[g_selMod].folder);
            if (it != g_updateInfo.end() && it->second.available) {
                int by = B.descUpdateBarY, bh = B.descUpdateBarH;
                int bxR = B.descX + B.descW - 8;
                int detailsW = 70;
                if (x >= bxR - detailsW && x < bxR && y >= by && y < by + bh) {
                    if (!it->second.sourceUrl.empty()) {
                        ShellExecute(hw, L"open", it->second.sourceUrl.c_str(),
                                     nullptr, nullptr, SW_SHOWNORMAL);
                    }
                    return 0;
                }
            }
        }

        // Loader Options dropdowns (left rail). On click, pop an owner-drawn
        // chooser via TrackPopupMenu so the items match the stone/bronze
        // theme (a dedicated custom popup-window class would give us full
        // control over the popup chrome too; this is a lighter solution that
        // re-skins each menu item via WM_MEASUREITEM/WM_DRAWITEM and lets
        // the OS handle the popup border).
        LoaderOptHits L = ComputeLoaderOptRects();
        // Generic owner-drawn popup. The caller fills a MenuRenderCtx
        // (kind + labels + optional colors + sizing) and passes the rect
        // of the on-screen "value box" — the popup anchors below its
        // bottom-left so the chooser opens flush under the box. cmd ids
        // are shifted by +1 so 0 doesn't get confused with "cancel".
        auto popMenu = [&](const RECT& anchorBox, MenuKind kind,
                           const std::vector<std::wstring>& labels,
                           const std::vector<COLORREF>&     colors,
                           int curIdx, int itemWidth,
                           void (*setter)(int)) {
            g_menuCtx.kind       = kind;
            g_menuCtx.itemWidth  = itemWidth;
            // Item height varies by kind. Number/string lists are fine
            // at the existing 28-px row; font previews need taller rows
            // to render the "STYLE" sample readably; color swatches want
            // a thicker stripe to be eye-catching.
            switch (kind) {
                case MenuKind::FontPreview: g_menuCtx.itemHeight = 44; break;
                case MenuKind::ColorSwatch: g_menuCtx.itemHeight = 32; break;
                default:                    g_menuCtx.itemHeight = 28; break;
            }
            g_menuCtx.labels     = labels;
            g_menuCtx.colors     = colors;

            HMENU menu = CreatePopupMenu();
            for (int v = 0; v < (int)labels.size(); ++v) {
                MENUITEMINFOW mii = { sizeof(mii) };
                mii.fMask  = MIIM_FTYPE | MIIM_ID | MIIM_STATE;
                mii.fType  = MFT_OWNERDRAW;
                mii.fState = (v == curIdx) ? MFS_CHECKED : MFS_UNCHECKED;
                mii.wID    = (UINT)(v + 1);
                InsertMenuItemW(menu, (UINT)v, TRUE, &mii);
            }
            // anchorBox is in LOGICAL coords (set by Layout); ClientToScreen
            // expects PHYSICAL — apply S() at the boundary.
            POINT pt = { S(anchorBox.left), S(anchorBox.bottom) };
            ClientToScreen(hw, &pt);
            int chosen = TrackPopupMenu(menu,
                                        TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN,
                                        pt.x, pt.y, 0, hw, nullptr);
            DestroyMenu(menu);
            g_menuCtx = MenuRenderCtx{};   // reset to defaults for safety
            if (chosen > 0) {
                setter(chosen - 1);
                InvalidateRect(hw, nullptr, FALSE);
            }
        };
        // Helper: anchor box for the loader-options dropdowns. Their
        // Helper: anchor box for the loader-option popups. Must match
        // drawDD's value box geometry (60 px in from row right edge,
        // 70 wide) so the popup opens flush below the visible chrome.
        auto loValueBox = [](const RECT& r) -> RECT {
            return RECT{ r.right - 130, r.top + 2, r.right - 60, r.bottom - 2 };
        };
        // Helper: anchor box for the toolbar dropdowns. The popup
        // opens flush below the value box (right of the label). Caller
        // supplies labelW to match the per-control layout in
        // PaintToolbarControl.
        auto tbValueBox = [](const RECT& r, int labelW) -> RECT {
            return RECT{ r.left + labelW + 1, r.top + 2,
                         r.right - 1,         r.bottom - 2 };
        };

        if (x >= L.stash.left && x < L.stash.right
            && y >= L.stash.top && y < L.stash.bottom) {
            std::vector<std::wstring> labels;
            for (int v = 0; v <= 16; ++v) {
                wchar_t b[8]; swprintf(b, 8, L"%d", v);
                labels.emplace_back(b);
            }
            popMenu(loValueBox(L.stash), MenuKind::IntValue,
                    labels, {}, g_loaderOpts.extraSharedTabs, 80,
                    +[](int v) {
                        g_loaderOpts.extraSharedTabs = v;
                        SaveLoaderOptStashTabs(v);
                    });
            return 0;
        }
        if (x >= L.dmg.left && x < L.dmg.right
            && y >= L.dmg.top && y < L.dmg.bottom) {
            std::vector<std::wstring> labels;
            for (int v = 0; v <= 2; ++v) {
                wchar_t b[8]; swprintf(b, 8, L"%d", v);
                labels.emplace_back(b);
            }
            popMenu(loValueBox(L.dmg), MenuKind::IntValue,
                    labels, {}, g_loaderOpts.damageIndicator, 80,
                    +[](int v) {
                        g_loaderOpts.damageIndicator = v;
                        SaveLoaderOptDamageIndicator(v);
                    });
            return 0;
        }

        // Seed dropdown — clicking the arrow opens a popup combining the
        // 3-slot recents (newest first) with the author-defined presets.
        // Picking sets seedArg to the value and auto-enables useSeed
        // (matches the "click affordance → it activates" pattern). The
        // popup labels are built in the same order as the indices passed
        // to the setter, so the setter can dispatch by index.
        {
            RECT sar = BodySeedArrowRect(B);
            if (x >= sar.left && x < sar.right && y >= sar.top && y < sar.bottom) {
                // Defocus the input first — clicking the arrow shouldn't
                // leave a phantom caret behind.
                if (g_seedInputFocused) {
                    g_seedInputFocused = false;
                    g_seedSelStart     = -1;
                    KillTimer(hw, IDT_SEED_CARET);
                    CommitTypedSeedToRecents(g_modSettings.seedArg);
                }

                std::vector<wstring> labels;
                std::vector<wstring> values;
                // Recents first, newest at top — iterate g_recentSeeds
                // from back (newest = Recent3) to front (oldest = Recent1).
                // Label uses the Recent<n> name per the spec; n = position
                // in the stored array + 1.
                for (int i = (int)g_recentSeeds.size() - 1; i >= 0; --i) {
                    wchar_t nm[24];
                    swprintf(nm, 24, L"Recent%d  (%.10s)", i + 1, g_recentSeeds[i].c_str());
                    labels.emplace_back(nm);
                    values.push_back(g_recentSeeds[i]);
                }
                // Then presets.
                for (size_t i = 0; i < g_seedNames.size(); ++i) {
                    labels.push_back(g_seedNames[i]);
                    values.push_back(g_seedValues[i]);
                }
                if (labels.empty()) return 0;

                int curIdx = -1;
                for (int i = 0; i < (int)values.size(); ++i)
                    if (values[i] == g_modSettings.seedArg) { curIdx = i; break; }
                // Stash the parallel `values` array on a static so the
                // C-style setter (which can't capture) can read it.
                static std::vector<wstring> s_popupValues;
                s_popupValues = values;
                popMenu(sar, MenuKind::StringList,
                        labels, {}, curIdx, SEED_COMBO_W,
                        +[](int v) {
                            if (v < 0 || v >= (int)s_popupValues.size()) return;
                            g_modSettings.seedArg = s_popupValues[v];
                            g_modSettings.useSeed = true;
                            if (g_selMod >= 0 && g_selMod < (int)g_mods.size())
                                SaveModSettings(g_mods[g_selMod]);
                        });
                return 0;
            }
        }

        // ── Toolbar: Scale slider / Font / Colour ────────────────────────
        // Scale's top-row textbox is a read-only display; the click
        // target is the slider beneath it (g_scaleSliderRect). Clicking
        // advances through the three presets active at this DPI.
        if (x >= g_scaleSliderRect.left && x < g_scaleSliderRect.right
            && y >= g_scaleSliderRect.top && y < g_scaleSliderRect.bottom) {
            int a, b, c;
            ActiveScalePresets(a, b, c);
            int order[3] = { a, b, c };
            int cur = ScaleToggleState();
            int next = (cur + 1) % 3;
            ApplyScaleChange(g_scalePresets[order[next]].mul);
            return 0;
        }
        // On Launch slider — clicking advances Minimize → Close →
        // Stay Open → Minimize. No window resize / font reload needed
        // (this just changes a flag the PLAY-click handler reads), so
        // unlike ApplyScaleChange we only need to save the cfg and
        // repaint the toolbar block. Invalidate a tight rect that
        // covers the textbox + slider so the rest of the body doesn't
        // flicker.
        if (x >= g_onLaunchSliderRect.left && x < g_onLaunchSliderRect.right
            && y >= g_onLaunchSliderRect.top && y < g_onLaunchSliderRect.bottom) {
            int cur  = OnLaunchSliderState();
            int next = (cur + 1) % 3;
            g_cfg.launchBehavior = OnLaunchSliderStateToBehavior(next);
            SaveCfg();
            // Invalidate the union of textbox + slider rects (they have
            // different X spans in the new column layout — textbox is
            // wider, slider is centered under it). MUST use
            // InvalidateRectL so the LOGICAL rect coordinates get
            // scaled to physical pixels — plain InvalidateRect with
            // logical coords lands the invalidated region in the wrong
            // place at any non-1.0 scale, leaving the slider's old
            // frame and the stale "Min/Close/Stay" value text visible
            // until some other event triggers a full repaint.
            RECT inv = {
                min(g_onLaunchRect.left,  g_onLaunchSliderRect.left),
                g_onLaunchRect.top,
                max(g_onLaunchRect.right, g_onLaunchSliderRect.right),
                g_onLaunchSliderRect.bottom
            };
            InvalidateRectL(hw, &inv, FALSE);
            return 0;
        }
        // Font dropdown — hit only the value box (label area is a
        // static title, not clickable). tbValueBox is the same rect
        // used as the popup anchor below, so the hit-target lines up
        // with the visible chrome.
        {
            RECT fvb = tbValueBox(g_fontDropdownRect, TBL::FONT_LABEL_W);
            if (x >= fvb.left && x < fvb.right
                && y >= fvb.top && y < fvb.bottom) {
                std::vector<std::wstring> labels;
                int curIdx = -1;
                for (int i = 0; i < (int)g_availableAbbrevs.size(); ++i) {
                    labels.push_back(g_availableAbbrevs[i]);
                    if (i < (int)g_availableFonts.size()
                        && g_availableFonts[i] == g_cfg.fontName) curIdx = i;
                }
                if (labels.empty()) return 0;     // no fonts found
                popMenu(fvb, MenuKind::StringList,
                        labels, {}, curIdx, TBL::FONT_VALUE_W,
                        +[](int v) {
                            if (v < 0 || v >= (int)g_availableFonts.size()) return;
                            g_cfg.fontName = g_availableFonts[v];
                            SaveCfg();
                            ApplyFontChange();
                        });
                return 0;
            }
        }
        // Colour dropdown — same value-box-only hit-test as Font.
        {
            RECT cvb = tbValueBox(g_colorDropdownRect, TBL::COLOR_LABEL_W);
            if (x >= cvb.left && x < cvb.right
                && y >= cvb.top && y < cvb.bottom) {
                std::vector<std::wstring> labels;   // unused for swatch-only items
                std::vector<COLORREF>     colors;
                for (auto& cp : g_colorPresets) {
                    labels.emplace_back(L"");       // placeholder — never drawn
                    colors.push_back(cp.rgb);
                }
                popMenu(cvb, MenuKind::ColorSwatch,
                        labels, colors, g_cfg.fontColorIdx, TBL::COLOR_VALUE_W,
                        +[](int v) {
                            if (v < 0 || v >= (int)(sizeof(g_colorPresets)/sizeof(g_colorPresets[0])))
                                return;
                            g_cfg.fontColorIdx = v;
                            SaveCfg();
                            ApplyColorChange();
                        });
                return 0;
            }
        }
        return 0;
    }

    // ── Title-bar button release ─────────────────────────────────────────
    case WM_LBUTTONUP: {
        // Seed drag-select release. We always end the drag, but only
        // release capture if the title-bar press path below isn't also
        // holding it (it doesn't share capture; the two drags are
        // mutually exclusive since they start from different rects).
        // If the anchor and caret coincide, no selection formed —
        // collapse the anchor to -1 so SeedHasSelection() returns false.
        if (g_seedDragging) {
            g_seedDragging = false;
            ReleaseCapture();
            if (g_seedSelStart == g_seedCaretPos) g_seedSelStart = -1;
            RECT rc; GetClientRect(hw, &rc);
            BodyLayout B = ComputeBodyLayout(U(rc.right), U(rc.bottom));
            RECT inR = BodySeedInputRect(B);
            InvalidateRectL(hw, &inR, FALSE);
            return 0;
        }

        if (g_tbPressed >= 0) {
            int x = U(GET_X_LPARAM(lp)), y = U(GET_Y_LPARAM(lp));
            int pressed = g_tbPressed;
            g_tbPressed = -1;
            ReleaseCapture();
            // Only fire if release happened over the same button (standard
            // click behavior — dragging off cancels)
            if (TBHitTest(hw, x, y) == pressed) {
                if (pressed == 0)      ShowWindow(hw, SW_MINIMIZE);
                else if (pressed == 1) PostMessage(hw, WM_CLOSE, 0, 0);
            }
            RECT inv;
            inv.left   = 0;
            inv.top    = 0;
            inv.bottom = TB_BTN_INSET_T + TB_BTN_H + 8;
            RECT cr; GetClientRect(hw, &cr);
            inv.right  = U(cr.right);
            InvalidateRectL(hw, &inv, FALSE);
        }
        return 0;
    }

    // ── Title-bar button hover tracking ──────────────────────────────────
    case WM_MOUSEMOVE: {
        int x = U(GET_X_LPARAM(lp)), y = U(GET_Y_LPARAM(lp));

        // Seed input drag-select. SetCapture in WM_LBUTTONDOWN routes
        // every WM_MOUSEMOVE here while the button is held — including
        // moves OUTSIDE the input rect or even outside the window. The
        // caret tracks the mouse X (clamped to the input bounds by
        // SeedXToCaretIndex's edge handling), and the anchor stays
        // where the click started so the selection grows/shrinks.
        if (g_seedDragging) {
            RECT rc; GetClientRect(hw, &rc);
            BodyLayout B = ComputeBodyLayout(U(rc.right), U(rc.bottom));
            RECT inR = BodySeedInputRect(B);
            REAL inTextX = (REAL)(inR.left + 6);
            REAL inTextY = (REAL)inR.top;
            int idx = SeedXToCaretIndex(x, inTextX, inTextY);
            if (idx != g_seedCaretPos) {
                g_seedCaretPos = idx;
                g_seedCaretVisible = true;
                InvalidateRectL(hw, &inR, FALSE);
            }
            return 0;
        }

        int newHover = TBHitTest(hw, x, y);
        if (newHover != g_tbHover) {
            g_tbHover = newHover;
            // Invalidate the rect covering both title-bar buttons.
            // (Buttons sit at TB_BTN_INSET_T .. TB_BTN_INSET_T+TB_BTN_H.)
            RECT inv;
            inv.left   = 0;
            inv.top    = 0;
            inv.right  = -1;     // full width — set below
            inv.bottom = TB_BTN_INSET_T + TB_BTN_H + 8;
            RECT cr; GetClientRect(hw, &cr);
            inv.right  = U(cr.right);
            InvalidateRectL(hw, &inv, FALSE);
        }
        // Track mouse leave so we can clear hover when cursor exits the window
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hw, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE: {
        if (g_tbHover != -1) {
            g_tbHover = -1;
            RECT inv;
            inv.left   = 0;
            inv.top    = 0;
            inv.bottom = TB_BTN_INSET_T + TB_BTN_H + 8;
            RECT cr; GetClientRect(hw, &cr);
            inv.right  = U(cr.right);
            InvalidateRectL(hw, &inv, FALSE);
        }
        return 0;
    }

    case WM_CHAR: {
        // Routed to the seed text input when it has virtual focus.
        // STRICT digit-only filter — only the ten ASCII digits 0-9 and
        // backspace (0x08) are consumed. Letters, separators, symbols,
        // whitespace, and control characters are dropped silently.
        // Ctrl+V doesn't pass through here; it's caught in WM_KEYDOWN
        // (where the modifier is reliably available) and goes through
        // the same digit filter when reading clipboard text.
        if (!g_seedInputFocused) return DefWindowProc(hw, msg, wp, lp);
        wchar_t ch = (wchar_t)wp;
        wstring& s = g_modSettings.seedArg;
        bool changed = false;
        if (ch >= L'0' && ch <= L'9') {
            // Typing replaces any active selection first, then inserts
            // at the (now-collapsed) caret.
            if (SeedHasSelection()) DeleteSeedSelection();
            if ((int)s.size() < SEED_MAX_DIGITS) {
                int caret = g_seedCaretPos;
                if (caret < 0) caret = 0;
                if (caret > (int)s.size()) caret = (int)s.size();
                s.insert(s.begin() + caret, ch);
                g_seedCaretPos = caret + 1;
                changed = true;
            }
        } else if (ch == 0x08) {       // backspace
            if (SeedHasSelection()) {
                // Backspace deletes the selection rather than peeling
                // a character off its left edge — same behavior as
                // every other text input the user has ever touched.
                DeleteSeedSelection();
                changed = true;
            } else if (g_seedCaretPos > 0 && !s.empty()) {
                s.erase(s.begin() + (g_seedCaretPos - 1));
                g_seedCaretPos--;
                changed = true;
            }
        }
        if (changed) {
            g_seedCaretVisible = true;     // keep caret visible while typing
            RECT rc; GetClientRect(hw, &rc);
            BodyLayout B = ComputeBodyLayout(U(rc.right), U(rc.bottom));
            // Repaint both the input (caret/text changed) and below the
            // seed row (cmd preview reflects the new value live, since
            // BuildLaunchArgs reads g_modSettings.seedArg directly when
            // useSeed is true).
            RECT below = {
                B.loX + 12, B.loSeedY,
                B.loX + B.loW - 12,
                B.loCmdPreviewY + 80
            };
            InvalidateRectL(hw, &below, FALSE);
        }
        return 0;
    }

    case WM_KEYDOWN: {
        if (!g_seedInputFocused) return DefWindowProc(hw, msg, wp, lp);
        wstring& s = g_modSettings.seedArg;
        bool handled = false;
        bool blur    = false;

        bool ctrlDown  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shiftDown = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;

        // Ctrl+V — paste from clipboard, filtered to digits only. We
        // check this BEFORE the switch (rather than threading a VK_V
        // case through the same path) because the Ctrl+V combination
        // doesn't always reach WM_CHAR consistently across keyboards
        // and IMEs, and we want one canonical paste path.
        if (ctrlDown && wp == 'V') {
            if (OpenClipboard(hw)) {
                HANDLE h = GetClipboardData(CF_UNICODETEXT);
                if (h) {
                    auto* clip = (const wchar_t*)GlobalLock(h);
                    if (clip) {
                        // Replace any active selection first so the
                        // pasted digits land where the selection was.
                        if (SeedHasSelection()) DeleteSeedSelection();
                        // Strict digit-only filter — every other character
                        // in the clipboard is dropped silently (separators,
                        // letters, whitespace, sign, decimal). Truncate at
                        // the per-input cap so an overlong paste can't blow
                        // through SEED_MAX_DIGITS.
                        int room = SEED_MAX_DIGITS - (int)s.size();
                        wstring digits;
                        for (size_t i = 0; clip[i] && (int)digits.size() < room; ++i) {
                            if (clip[i] >= L'0' && clip[i] <= L'9')
                                digits += clip[i];
                        }
                        if (!digits.empty()) {
                            int caret = g_seedCaretPos;
                            if (caret < 0) caret = 0;
                            if (caret > (int)s.size()) caret = (int)s.size();
                            s.insert(caret, digits);
                            g_seedCaretPos = caret + (int)digits.size();
                            handled = true;
                        }
                        GlobalUnlock(h);
                    }
                }
                CloseClipboard();
            }
            if (!handled) {
                g_seedCaretVisible = true;
                return 0;
            }
        }
        // Ctrl+C — copy current selection (or whole value if no sel)
        // to clipboard. Doesn't modify the input, doesn't blur.
        else if (ctrlDown && wp == 'C') {
            wstring copy = SeedHasSelection()
                            ? s.substr(SeedSelLo(), SeedSelHi() - SeedSelLo())
                            : s;
            if (!copy.empty() && OpenClipboard(hw)) {
                EmptyClipboard();
                size_t bytes = (copy.size() + 1) * sizeof(wchar_t);
                HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
                if (hg) {
                    auto* dst = (wchar_t*)GlobalLock(hg);
                    if (dst) {
                        memcpy(dst, copy.c_str(), bytes);
                        GlobalUnlock(hg);
                        SetClipboardData(CF_UNICODETEXT, hg);
                    } else {
                        GlobalFree(hg);
                    }
                }
                CloseClipboard();
            }
            return 0;
        }
        // Ctrl+X — cut (copy then delete selection). If there's no
        // selection, falls through to copy-whole-and-clear — matches
        // Notepad / EDIT control semantics.
        else if (ctrlDown && wp == 'X') {
            wstring cut = SeedHasSelection()
                            ? s.substr(SeedSelLo(), SeedSelHi() - SeedSelLo())
                            : s;
            if (!cut.empty() && OpenClipboard(hw)) {
                EmptyClipboard();
                size_t bytes = (cut.size() + 1) * sizeof(wchar_t);
                HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
                if (hg) {
                    auto* dst = (wchar_t*)GlobalLock(hg);
                    if (dst) {
                        memcpy(dst, cut.c_str(), bytes);
                        GlobalUnlock(hg);
                        SetClipboardData(CF_UNICODETEXT, hg);
                    } else {
                        GlobalFree(hg);
                    }
                }
                CloseClipboard();
            }
            if (SeedHasSelection()) {
                DeleteSeedSelection();
            } else {
                s.clear();
                g_seedCaretPos = 0;
            }
            handled = true;
        }
        // Ctrl+A — select all. Selection runs 0..size() with caret at
        // the end (matches the EDIT control's behavior). Selection
        // collapses on next plain arrow key.
        else if (ctrlDown && wp == 'A') {
            if (!s.empty()) {
                g_seedSelStart = 0;
                g_seedCaretPos = (int)s.size();
            }
            handled = true;
        }

        if (!handled) {
            // Helper: arrow movement that respects the shift modifier.
            // With Shift: anchor the selection (if not already) and
            // move the caret. Without Shift: clear the selection and
            // collapse the caret to lo / hi if there was one — the
            // standard "Left collapses to start, Right collapses to
            // end" behavior — otherwise move one position.
            auto moveCaret = [&](int dir /* -1 or +1 */) {
                if (shiftDown) {
                    if (g_seedSelStart < 0) g_seedSelStart = g_seedCaretPos;
                    g_seedCaretPos += dir;
                    if (g_seedCaretPos < 0) g_seedCaretPos = 0;
                    if (g_seedCaretPos > (int)s.size()) g_seedCaretPos = (int)s.size();
                } else if (SeedHasSelection()) {
                    g_seedCaretPos = (dir < 0) ? SeedSelLo() : SeedSelHi();
                    g_seedSelStart = -1;
                } else {
                    g_seedCaretPos += dir;
                    if (g_seedCaretPos < 0) g_seedCaretPos = 0;
                    if (g_seedCaretPos > (int)s.size()) g_seedCaretPos = (int)s.size();
                    g_seedSelStart = -1;
                }
            };

            switch (wp) {
                case VK_LEFT:
                    moveCaret(-1);
                    handled = true;
                    break;
                case VK_RIGHT:
                    moveCaret(+1);
                    handled = true;
                    break;
                case VK_HOME:
                    if (shiftDown && g_seedSelStart < 0) g_seedSelStart = g_seedCaretPos;
                    else if (!shiftDown) g_seedSelStart = -1;
                    g_seedCaretPos = 0;
                    handled = true;
                    break;
                case VK_END:
                    if (shiftDown && g_seedSelStart < 0) g_seedSelStart = g_seedCaretPos;
                    else if (!shiftDown) g_seedSelStart = -1;
                    g_seedCaretPos = (int)s.size();
                    handled = true;
                    break;
                case VK_DELETE:
                    if (SeedHasSelection()) {
                        DeleteSeedSelection();
                        handled = true;
                    } else if (g_seedCaretPos < (int)s.size()) {
                        s.erase(s.begin() + g_seedCaretPos);
                        handled = true;
                    }
                    break;
                case VK_RETURN:
                    // Commit + blur. Recents get updated by the blur path
                    // below (so Esc-without-Enter, blur-by-click, and Enter
                    // all share one commit code path).
                    handled = true;
                    blur = true;
                    break;
                case VK_ESCAPE:
                    // Cancel typing — drop focus without committing. The
                    // typed value stays in seedArg (we don't snapshot prior
                    // state to roll back), but recents aren't updated.
                    g_seedInputFocused = false;
                    g_seedSelStart = -1;
                    KillTimer(hw, IDT_SEED_CARET);
                    handled = true;
                    break;
            }
        }
        if (blur) {
            g_seedInputFocused = false;
            g_seedSelStart = -1;
            KillTimer(hw, IDT_SEED_CARET);
            CommitTypedSeedToRecents(g_modSettings.seedArg);
            if (g_selMod >= 0 && g_selMod < (int)g_mods.size())
                SaveModSettings(g_mods[g_selMod]);
        }
        if (handled) {
            g_seedCaretVisible = true;
            RECT rc; GetClientRect(hw, &rc);
            BodyLayout B = ComputeBodyLayout(U(rc.right), U(rc.bottom));
            RECT below = {
                B.loX + 12, B.loSeedY,
                B.loX + B.loW - 12,
                B.loCmdPreviewY + 80
            };
            InvalidateRectL(hw, &below, FALSE);
            return 0;
        }
        return DefWindowProc(hw, msg, wp, lp);
    }

    case WM_TIMER: {
        if (wp == IDT_MODS_DEBOUNCE) {
            // Bursty events have settled — do the actual rescan.
            KillTimer(hw, IDT_MODS_DEBOUNCE);
            RefreshMods();
        }
        else if (wp == IDT_SEED_CARET) {
            // Caret blink for the seed text input. We only invalidate
            // the input rect (not the whole panel) — the caret is a
            // 1.5-px-thick vertical line that flips visible/invisible
            // every SEED_CARET_BLINK_MS, and the rest of the input
            // chrome / label doesn't change.
            if (g_seedInputFocused) {
                g_seedCaretVisible = !g_seedCaretVisible;
                RECT rc; GetClientRect(hw, &rc);
                BodyLayout B = ComputeBodyLayout(U(rc.right), U(rc.bottom));
                RECT inR = BodySeedInputRect(B);
                InvalidateRectL(hw, &inR, FALSE);
            } else {
                // Defensive — stop the timer if we somehow lost focus
                // without killing it.
                KillTimer(hw, IDT_SEED_CARET);
            }
        }
        else if (wp == IDT_CLEANUP_OLD_EXE) {
            // Deferred .old cleanup. The prior launcher process is
            // (usually) finishing its shutdown and releasing the image
            // file we want to delete. Retry every 2 s until success,
            // the file disappears, or we hit the cap (~30 s total)
            // and fall back to MoveFileEx for a reboot-scheduled delete.
            if (g_pendingOldExeDelete.empty()) {
                KillTimer(hw, IDT_CLEANUP_OLD_EXE);
                return 0;
            }
            if (DeleteFileW(g_pendingOldExeDelete.c_str())) {
                g_pendingOldExeDelete.clear();
                g_cleanupOldExeAttempts = 0;
                KillTimer(hw, IDT_CLEANUP_OLD_EXE);
                return 0;
            }
            DWORD err = GetLastError();
            if (err == ERROR_FILE_NOT_FOUND ||
                err == ERROR_PATH_NOT_FOUND) {
                // Vanished out from under us — maybe an external
                // cleanup grabbed it. Either way, success.
                g_pendingOldExeDelete.clear();
                g_cleanupOldExeAttempts = 0;
                KillTimer(hw, IDT_CLEANUP_OLD_EXE);
                return 0;
            }
            g_cleanupOldExeAttempts++;
            if (g_cleanupOldExeAttempts >= 15) {
                // 15 × 2 s ≈ 30 s ceiling. Anything still holding the
                // file at this point isn't releasing during our session,
                // so schedule the delete for next reboot and log it.
                MoveFileExW(g_pendingOldExeDelete.c_str(), nullptr,
                            MOVEFILE_DELAY_UNTIL_REBOOT);
                LogCleanupOldExeGaveUp(g_pendingOldExeDelete, err);
                g_pendingOldExeDelete.clear();
                g_cleanupOldExeAttempts = 0;
                KillTimer(hw, IDT_CLEANUP_OLD_EXE);
            }
            return 0;
        }
        else if (wp == IDT_D2R_POLL) {
            // First poll fires ~10 s after the launch click — the game's
            // processes need time to fully spawn before they reliably
            // appear in the process table. On that first fire we drop
            // back to the normal 1 s cadence. SetTimer with the same
            // ID just resets the interval — no KillTimer needed.
            if (g_pollFirstShot) {
                g_pollFirstShot = false;
                SetTimer(hw, IDT_D2R_POLL, 1000, nullptr);
            }

            // Defensive: if we somehow got here while not tracking, kill
            // the timer and bail.
            if (!g_d2rTracking) {
                KillTimer(hw, IDT_D2R_POLL);
                return 0;
            }

            bool d2rRunning = AnyProcessExistsByName(k_d2rProcessNames,
                                                    k_d2rProcessNameCount);

            if (d2rRunning) {
                // At least one of D2R.exe / D2RLoader.exe is alive.
                // First time we see it, anchor the game-start tick —
                // that's when "actual gameplay" begins for the purpose
                // of playtime tracking (excludes the launcher-overhead
                // window between click and process spawn).
                if (!g_d2rEverSeen) {
                    g_d2rGameStartTick = GetTickCount();
                }
                g_d2rEverSeen = true;
                return 0;
            }

            // Neither D2R.exe nor D2RLoader.exe currently present.
            if (g_d2rEverSeen) {
                // We saw the game earlier — now both are gone. True
                // exit: credit the elapsed time to the launched mod's
                // playtime accumulator, then restore the launcher.
                if (g_d2rGameStartTick != 0 && !g_d2rGameModFolder.empty()) {
                    DWORD now = GetTickCount();
                    DWORD elapsedMs = now - g_d2rGameStartTick;
                    uint64_t elapsedSec = elapsedMs / 1000;
                    if (elapsedSec > 0) {
                        RecordPlaytime(g_d2rGameModFolder, elapsedSec);
                    }
                }
                g_d2rTracking = false;
                g_d2rEverSeen = false;
                g_d2rGameStartTick = 0;
                g_d2rGameModFolder.clear();
                KillTimer(hw, IDT_D2R_POLL);
                if (IsIconic(hw)) ShowWindow(hw, SW_RESTORE);
                SetForegroundWindow(hw);
                SetActiveWindow(hw);
                BringWindowToTop(hw);
                return 0;
            }

            // Never seen the game yet — D2R might still be spawning.
            // Fail-safe: if neither process has shown up within 60 s of
            // click, assume the loader crashed or the launch failed,
            // and restore the launcher so the user isn't stranded with
            // a minimized window that never comes back.
            DWORD waited = GetTickCount() - g_d2rLaunchTick;
            if (waited > 60000) {
                g_d2rTracking = false;
                KillTimer(hw, IDT_D2R_POLL);
                if (IsIconic(hw)) ShowWindow(hw, SW_RESTORE);
                SetForegroundWindow(hw);
                SetActiveWindow(hw);
                BringWindowToTop(hw);
            }
            // else: keep polling, the game should appear soon.
        }
        return 0;
    }

    case WM_SIZE: {
        Layout(LOWORD(lp), HIWORD(lp));
        return 0;
    }

    case WM_ACTIVATE: {
        // Restoring from minimized state, or the window otherwise
        // becoming active, can leave the owner-draw children (nav
        // buttons, Nexus/Update, PLAY) showing the BUTTON class's
        // default light-gray fill for a frame or two before their
        // WM_DRAWITEM fires — that's the brief "white flash" on
        // restore. Forcing every descendant to paint synchronously
        // here closes the window between "shown" and "painted",
        // so the first frame the user sees is the finished UI rather
        // than the system default fill underneath.
        //
        // We only do this on activation (LOWORD(wp) != WA_INACTIVE);
        // forcing UPDATENOW during deactivation would burn cycles
        // for no visible benefit.
        if (LOWORD(wp) != WA_INACTIVE) {
            RedrawWindow(hw, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        }
        break;
    }

    case WM_DESTROY:
        StopModsWatcher();      // join the watcher thread cleanly
        // Stop tracking (we no longer hold a process handle to close —
        // tracking is name-based now). D2R itself keeps running.
        g_d2rTracking = false;
        g_d2rEverSeen = false;
        KillTimer(hw, IDT_D2R_POLL);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hw, msg, wp, lp);
}

// ─────────────────────────────────────────────────────────────────────────
//  CONFLICT DIALOG  (themed modal — "Mod Folder Already Exists")
// ─────────────────────────────────────────────────────────────────────────
//
// Shown by the zip-install worker when a dropped mod's target folder
// already exists. Three NexusUpdate-styled buttons:
//
//   Update    → keep folder, copy only files whose paths exist in dest
//                (per the user's strict reading of "Update only overwrites
//                files found in the archive that are also found in the
//                folder")
//   Overwrite → wipe folder, extract fresh
//   Cancel    → leave folder untouched, skip to next queued zip
//
// The dialog uses bg_stone.png as its background (same texture as the
// loader-options panel for visual continuity) with a gold border and
// renders all text in g_fModName so it picks up the user's selected
// font automatically.
//
// Modal mechanics: the worker thread SendMessages into MainProc, which
// calls ShowConflictDialog inline. ShowConflictDialog runs a local
// message pump so the parent stays responsive (in terms of paint /
// non-input messages) while input is gated to the dialog via
// EnableWindow(parent, FALSE).
//
// Button paint is delegated back to MainProc's WM_DRAWITEM handler —
// MainProc's WM_DRAWITEM doesn't depend on the parent being g_hwMain,
// only on the d->hwndItem button HWND, so direct re-entry works.

static const wstring* g_dlgModNamePtr = nullptr;   // shared with paint
static HWND g_conflictDlg = nullptr;               // for re-entry guard

static LRESULT CALLBACK ConflictDlgProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;  // suppress flicker; WM_PAINT fills

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        int W = rc.right, H = rc.bottom;

        // Double-buffer to avoid the asset-blit flicker when the user
        // drags between buttons.
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

        {
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

            // Stone background — same source as the loader-options panel.
            // Cropped from the upper-left region of bg_stone so the
            // texture variation reads consistent with the main window.
            Bitmap* stone = AssetImage(L"bg_stone.png");
            if (stone) {
                int sw = (int)stone->GetWidth();
                int sh = (int)stone->GetHeight();
                // Crop a tile-sized region: the dialog is much smaller
                // than the full asset, so use a top-left slice that
                // matches the loader-options panel's sampling area.
                int cropW = (sw < W) ? sw : W;
                int cropH = (sh < H) ? sh : H;
                Rect dst(0, 0, W, H);
                g.DrawImage(stone, dst, 40, 40, cropW, cropH, UnitPixel);
            } else {
                SolidBrush bg(Color(28, 24, 20));
                g.FillRectangle(&bg, 0, 0, W, H);
            }

            // Gold border (2 px) — matches the main window frame.
            Pen border(Tok::Gold, 2.0f);
            g.DrawRectangle(&border, 1, 1, W - 3, H - 3);
            // Inner darker pen for the engraved feel.
            Pen innerB(Tok::Bronze, 1.0f);
            g.DrawRectangle(&innerB, 3, 3, W - 7, H - 7);

            // Text — uses g_fModName so the user's selected font applies.
            SolidBrush textBr(Tok::TextParchment);
            SolidBrush goldBr(Tok::Gold);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentNear);
            sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsNoWrap);

            int padX = (int)(24 * g_dpiScale);
            int y    = (int)(18 * g_dpiScale);
            int lineH= (int)(30 * g_dpiScale);

            // Title — "Mod Folder Already Exists" in gold accent.
            g.DrawString(L"Mod Folder Already Exists", -1,
                         g_fModName ? g_fModName : g_fBtn,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)lineH),
                         &sf, &goldBr);
            y += lineH;

            // Mod name in quotes on its own line.
            if (g_dlgModNamePtr && !g_dlgModNamePtr->empty()) {
                wstring nm = L"\u201C" + *g_dlgModNamePtr + L"\u201D";
                g.DrawString(nm.c_str(), -1,
                             g_fModName ? g_fModName : g_fBtn,
                             RectF((REAL)padX, (REAL)y,
                                   (REAL)(W - 2 * padX), (REAL)lineH),
                             &sf, &textBr);
            }
            y += lineH;
            y += (int)(8 * g_dpiScale);

            // Per-action descriptions — small body text, multi-line.
            int descY  = y;
            int descLH = (int)(20 * g_dpiScale);
            sf.SetLineAlignment(StringAlignmentCenter);

            auto descLine = [&](const wchar_t* s) {
                g.DrawString(s, -1, g_fBtn,
                             RectF((REAL)padX, (REAL)descY,
                                   (REAL)(W - 2 * padX), (REAL)descLH),
                             &sf, &textBr);
                descY += descLH;
            };
            descLine(L"Update overwrites only files present in the archive that already exist in the folder.");
            descLine(L"Overwrite erases the folder and extracts the archive fresh.");
            descLine(L"Cancel leaves the folder untouched.");
        }

        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        // Re-enter MainProc's WM_DRAWITEM so the dialog's owner-drawn
        // buttons paint with the same NexusUpdate visuals as the main
        // window's. MainProc keys off d->hwndItem, not the parent.
        return MainProc(hw, msg, wp, lp);
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int* res = (int*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        if (res) {
            if      (id == 1) *res = 1;   // Update
            else if (id == 2) *res = 2;   // Overwrite
            else if (id == 3) *res = 0;   // Cancel
        }
        if (id >= 1 && id <= 3) DestroyWindow(hw);
        return 0;
    }

    case WM_CLOSE: {
        int* res = (int*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        if (res) *res = 0;
        DestroyWindow(hw);
        return 0;
    }

    case WM_DESTROY: {
        if (g_conflictDlg == hw) g_conflictDlg = nullptr;
        return 0;
    }
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

// Show the modal conflict dialog on the UI thread. Blocks until the user
// picks an action; returns 0=cancel / 1=update / 2=overwrite.
static int ShowConflictDialog(HWND parent, const wstring& modName) {
    // Re-entry guard: shouldn't happen (worker SendMessages serially),
    // but if it ever does, decline the inner call rather than stack two
    // modal pumps.
    if (g_conflictDlg) return 0;

    static bool classReg = false;
    if (!classReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = ConflictDlgProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"AngirisConflictDlg";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        classReg = true;
    }

    // Dialog size in PHYSICAL pixels — scaled by DPI only (not by
    // g_userScale; the dialog isn't part of the user-scaled UI body).
    int dlgW = (int)(560 * g_dpiScale);
    int dlgH = (int)(280 * g_dpiScale);

    // Center on parent. AdjustWindowRectEx adds caption + border so the
    // CLIENT area ends up at dlgW × dlgH after the WS_CAPTION decoration.
    RECT pr; GetWindowRect(parent, &pr);
    RECT clientReq = { 0, 0, dlgW, dlgH };
    AdjustWindowRectEx(&clientReq, WS_POPUP | WS_CAPTION, FALSE, WS_EX_DLGMODALFRAME);
    int winW = clientReq.right - clientReq.left;
    int winH = clientReq.bottom - clientReq.top;
    int cx = (pr.left + pr.right) / 2 - winW / 2;
    int cy = (pr.top + pr.bottom) / 2 - winH / 2;

    int result = 0;
    g_dlgModNamePtr = &modName;

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"AngirisConflictDlg",
        L"Mod Folder Already Exists",
        WS_POPUP | WS_CAPTION,
        cx, cy, winW, winH,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!dlg) {
        g_dlgModNamePtr = nullptr;
        return 0;
    }
    g_conflictDlg = dlg;
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&result);

    // Three NexusUpdate-styled buttons at the bottom of the client area.
    // The native NexusUpdate asset is 254×54 — for the dialog we scale
    // down to ~170×46 (physical px) so all three fit comfortably inside
    // the 560 px dialog without their decorative edges bleeding into
    // the bronze border. Outer buttons get an additional 5 px inward
    // nudge so the left/right border padding is symmetric (was a tight
    // fit before — at 1.0 DPI scale the row was 568 wide in a 560
    // dialog and the outer chevron ornaments bled over the frame).
    int btnW    = (int)(170 * g_dpiScale);
    int btnH    = (int)(46  * g_dpiScale);
    int btnGap  = (int)(14  * g_dpiScale);
    int nudge   = (int)( 5  * g_dpiScale);   // outer buttons toward center

    RECT cr; GetClientRect(dlg, &cr);
    int rowW  = 3 * btnW + 2 * btnGap;
    int rowX0 = (cr.right - rowW) / 2;
    int rowY  = cr.bottom - btnH - (int)(20 * g_dpiScale);

    MkStdBtn(dlg, L"Update",    1, rowX0 + nudge,                       rowY, btnW, btnH,
             true, ButtonKind::NexusUpdate);
    MkStdBtn(dlg, L"Overwrite", 2, rowX0 + (btnW + btnGap),             rowY, btnW, btnH,
             true, ButtonKind::NexusUpdate);
    MkStdBtn(dlg, L"Cancel",    3, rowX0 + 2 * (btnW + btnGap) - nudge, rowY, btnW, btnH,
             true, ButtonKind::NexusUpdate);

    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    EnableWindow(parent, FALSE);

    // Local modal pump — runs until the dialog is destroyed (one of the
    // three buttons clicked, the close box, or Esc). IsDialogMessageW
    // handles Tab cycling between the buttons automatically.
    MSG msg;
    bool done = false;
    while (!done) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {
            // WM_QUIT or error — break out without committing a choice.
            if (got == 0) PostQuitMessage((int)msg.wParam);
            done = true;
            break;
        }
        // Esc = cancel, Enter = update (the safe default).
        if (msg.message == WM_KEYDOWN && msg.hwnd == dlg) {
            if (msg.wParam == VK_ESCAPE) {
                result = 0;
                done = true;
                break;
            }
            if (msg.wParam == VK_RETURN) {
                result = 1;
                done = true;
                break;
            }
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(dlg)) done = true;
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (IsWindow(dlg)) DestroyWindow(dlg);
    g_dlgModNamePtr = nullptr;
    g_conflictDlg = nullptr;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────
//  LAUNCHER UPDATE DIALOG  (themed modal — Update / Skip Version / Ignore)
// ─────────────────────────────────────────────────────────────────────────
//
// Same paint/dispatch pattern as ConflictDlg: stone background, gold +
// bronze border, three NexusUpdate-styled buttons, Esc = Ignore,
// Enter = Update. State travels via two file-scope pointers so the
// paint code can show the live tag values; the result lands in an int
// pointed to by GWLP_USERDATA so the modal pump can return it.

static const wstring* g_dlgLatestTagPtr  = nullptr;
static const wstring* g_dlgCurrentTagPtr = nullptr;
static HWND           g_launcherUpdateDlg = nullptr;

static LRESULT CALLBACK LauncherUpdateDlgProc(HWND hw, UINT msg,
                                              WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        int W = rc.right, H = rc.bottom;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);
        {
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

            Bitmap* stone = AssetImage(L"bg_stone.png");
            if (stone) {
                int sw = (int)stone->GetWidth();
                int sh = (int)stone->GetHeight();
                int cropW = (sw < W) ? sw : W;
                int cropH = (sh < H) ? sh : H;
                Rect dst(0, 0, W, H);
                g.DrawImage(stone, dst, 40, 40, cropW, cropH, UnitPixel);
            } else {
                SolidBrush bg(Color(28, 24, 20));
                g.FillRectangle(&bg, 0, 0, W, H);
            }
            Pen border(Tok::Gold, 2.0f);
            g.DrawRectangle(&border, 1, 1, W - 3, H - 3);
            Pen innerB(Tok::Bronze, 1.0f);
            g.DrawRectangle(&innerB, 3, 3, W - 7, H - 7);

            SolidBrush textBr(Tok::TextParchment);
            SolidBrush goldBr(Tok::Gold);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentNear);
            sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsNoWrap);

            int padX  = (int)(24 * g_dpiScale);
            int y     = (int)(18 * g_dpiScale);
            int lineH = (int)(30 * g_dpiScale);

            Font* titleFont = g_fModName ? g_fModName : g_fBtn;
            g.DrawString(L"Launcher Update Available", -1, titleFont,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)lineH),
                         &sf, &goldBr);
            y += lineH;

            // Version line: "Version <latest> is available (current <vN>)"
            wstring versionLine = L"Version ";
            versionLine += (g_dlgLatestTagPtr && !g_dlgLatestTagPtr->empty())
                ? *g_dlgLatestTagPtr : L"?";
            versionLine += L" is available (you have ";
            versionLine += (g_dlgCurrentTagPtr && !g_dlgCurrentTagPtr->empty())
                ? *g_dlgCurrentTagPtr : L"?";
            versionLine += L")";
            g.DrawString(versionLine.c_str(), -1, titleFont,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)lineH),
                         &sf, &textBr);
            y += lineH;
            y += (int)(8 * g_dpiScale);

            // Per-action descriptions — same descLine pattern as
            // ShowConflictDialog so the visual rhythm matches.
            int descY  = y;
            int descLH = (int)(20 * g_dpiScale);
            sf.SetLineAlignment(StringAlignmentCenter);
            auto descLine = [&](const wchar_t* s) {
                g.DrawString(s, -1, g_fBtn,
                             RectF((REAL)padX, (REAL)descY,
                                   (REAL)(W - 2 * padX), (REAL)descLH),
                             &sf, &textBr);
                descY += descLH;
            };
            descLine(L"Update downloads the new release and replaces the running launcher.");
            descLine(L"Skip Version stops prompting for this release; you'll be notified for newer ones.");
            descLine(L"Ignore continues normally and re-prompts on the next launch.");
        }
        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_DRAWITEM:
        // Re-enter MainProc's WM_DRAWITEM so the buttons paint as
        // NexusUpdate just like ConflictDlg's do. Owner-drawn buttons
        // key off d->hwndItem, so the dispatching window doesn't have
        // to be g_hwMain.
        return MainProc(hw, msg, wp, lp);

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int* res = (int*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        if (res) {
            if      (id == 1) *res = 1;   // Update
            else if (id == 2) *res = 2;   // Skip Version
            else if (id == 3) *res = 0;   // Ignore
        }
        if (id >= 1 && id <= 3) DestroyWindow(hw);
        return 0;
    }

    case WM_CLOSE: {
        int* res = (int*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        if (res) *res = 0;
        DestroyWindow(hw);
        return 0;
    }

    case WM_DESTROY:
        if (g_launcherUpdateDlg == hw) g_launcherUpdateDlg = nullptr;
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

static int ShowLauncherUpdateDialog(HWND parent, const wstring& latestTag) {
    if (g_launcherUpdateDlg) return 0;   // re-entry guard

    static bool classReg = false;
    if (!classReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = LauncherUpdateDlgProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"AngirisLauncherUpdateDlg";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        classReg = true;
    }

    int dlgW = (int)(560 * g_dpiScale);
    int dlgH = (int)(260 * g_dpiScale);

    RECT pr; GetWindowRect(parent, &pr);
    RECT clientReq = { 0, 0, dlgW, dlgH };
    AdjustWindowRectEx(&clientReq, WS_POPUP | WS_CAPTION, FALSE, WS_EX_DLGMODALFRAME);
    int winW = clientReq.right - clientReq.left;
    int winH = clientReq.bottom - clientReq.top;
    int cx = (pr.left + pr.right) / 2 - winW / 2;
    int cy = (pr.top + pr.bottom) / 2 - winH / 2;

    int result = 0;
    wstring currentVer = LAUNCHER_VERSION;
    g_dlgLatestTagPtr  = &latestTag;
    g_dlgCurrentTagPtr = &currentVer;

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"AngirisLauncherUpdateDlg",
        L"Launcher Update Available",
        WS_POPUP | WS_CAPTION,
        cx, cy, winW, winH,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!dlg) {
        g_dlgLatestTagPtr = g_dlgCurrentTagPtr = nullptr;
        return 0;
    }
    g_launcherUpdateDlg = dlg;
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&result);

    // Same three-button row geometry as the conflict dialog so the
    // two share a visual rhythm.
    int btnW   = (int)(170 * g_dpiScale);
    int btnH   = (int)(46  * g_dpiScale);
    int btnGap = (int)(14  * g_dpiScale);
    int nudge  = (int)( 5  * g_dpiScale);

    RECT cr; GetClientRect(dlg, &cr);
    int rowW  = 3 * btnW + 2 * btnGap;
    int rowX0 = (cr.right - rowW) / 2;
    int rowY  = cr.bottom - btnH - (int)(20 * g_dpiScale);

    MkStdBtn(dlg, L"Update",       1, rowX0 + nudge,                       rowY, btnW, btnH,
             true, ButtonKind::NexusUpdate);
    MkStdBtn(dlg, L"Skip Version", 2, rowX0 + (btnW + btnGap),             rowY, btnW, btnH,
             true, ButtonKind::NexusUpdate);
    MkStdBtn(dlg, L"Ignore",       3, rowX0 + 2 * (btnW + btnGap) - nudge, rowY, btnW, btnH,
             true, ButtonKind::NexusUpdate);

    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    EnableWindow(parent, FALSE);

    MSG msg;
    bool done = false;
    while (!done) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage((int)msg.wParam);
            done = true; break;
        }
        // Esc = Ignore, Enter = Update.
        if (msg.message == WM_KEYDOWN && msg.hwnd == dlg) {
            if (msg.wParam == VK_ESCAPE) { result = 0; done = true; break; }
            if (msg.wParam == VK_RETURN) { result = 1; done = true; break; }
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(dlg)) done = true;
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (IsWindow(dlg)) DestroyWindow(dlg);
    g_dlgLatestTagPtr = g_dlgCurrentTagPtr = nullptr;
    g_launcherUpdateDlg = nullptr;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────
//  NO MODINFO DIALOG  (themed modal — "Zip didn't contain modinfo.json")
// ─────────────────────────────────────────────────────────────────────────
//
// Shown when a dropped zip has no modinfo.json anywhere in its tree.
// Single OK button, message names the offending zip so the user can tell
// it apart in a multi-drop. Returns nothing — the worker just skips
// this zip and moves to the next.

static const wstring* g_dlgZipNamePtr = nullptr;   // shared with paint
static HWND g_noModInfoDlg = nullptr;

static LRESULT CALLBACK NoModInfoDlgProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        int W = rc.right, H = rc.bottom;
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);
        {
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

            Bitmap* stone = AssetImage(L"bg_stone.png");
            if (stone) {
                int sw = (int)stone->GetWidth();
                int sh = (int)stone->GetHeight();
                int cropW = (sw < W) ? sw : W;
                int cropH = (sh < H) ? sh : H;
                Rect dst(0, 0, W, H);
                g.DrawImage(stone, dst, 40, 40, cropW, cropH, UnitPixel);
            } else {
                SolidBrush bg(Color(28, 24, 20));
                g.FillRectangle(&bg, 0, 0, W, H);
            }
            Pen border(Tok::Gold, 2.0f);
            g.DrawRectangle(&border, 1, 1, W - 3, H - 3);
            Pen innerB(Tok::Bronze, 1.0f);
            g.DrawRectangle(&innerB, 3, 3, W - 7, H - 7);

            SolidBrush textBr(Tok::TextParchment);
            SolidBrush goldBr(Tok::Gold);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsNoWrap);

            int padX = (int)(24 * g_dpiScale);
            int y    = (int)(24 * g_dpiScale);
            int lineH= (int)(30 * g_dpiScale);

            // Title — "Invalid Mod Archive" in gold.
            g.DrawString(L"Invalid Mod Archive", -1,
                         g_fModName ? g_fModName : g_fBtn,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)lineH),
                         &sf, &goldBr);
            y += lineH;
            y += (int)(10 * g_dpiScale);

            // Body — wrapped over two lines. Don't NoWrap here because the
            // zip filename can be long and we want it to break naturally
            // rather than overflow the rect.
            StringFormat sfWrap;
            sfWrap.SetAlignment(StringAlignmentCenter);
            sfWrap.SetLineAlignment(StringAlignmentNear);
            int bodyH = (int)(60 * g_dpiScale);
            wstring msgText = (g_dlgZipNamePtr ? *g_dlgZipNamePtr : wstring())
                + L" did not contain a modinfo.json file, please contact the mod author.";
            g.DrawString(msgText.c_str(), -1, g_fBtn,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)bodyH),
                         &sfWrap, &textBr);
        }
        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_DRAWITEM:
        return MainProc(hw, msg, wp, lp);

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == 1) DestroyWindow(hw);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hw);
        return 0;

    case WM_DESTROY:
        if (g_noModInfoDlg == hw) g_noModInfoDlg = nullptr;
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

static void ShowNoModInfoDialog(HWND parent, const wstring& zipName) {
    if (g_noModInfoDlg) return;
    static bool classReg = false;
    if (!classReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = NoModInfoDlgProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"AngirisNoModInfoDlg";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        classReg = true;
    }

    int dlgW = (int)(560 * g_dpiScale);
    int dlgH = (int)(220 * g_dpiScale);
    RECT pr; GetWindowRect(parent, &pr);
    RECT clientReq = { 0, 0, dlgW, dlgH };
    AdjustWindowRectEx(&clientReq, WS_POPUP | WS_CAPTION, FALSE, WS_EX_DLGMODALFRAME);
    int winW = clientReq.right - clientReq.left;
    int winH = clientReq.bottom - clientReq.top;
    int cx = (pr.left + pr.right) / 2 - winW / 2;
    int cy = (pr.top + pr.bottom) / 2 - winH / 2;

    g_dlgZipNamePtr = &zipName;
    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"AngirisNoModInfoDlg",
        L"Invalid Mod Archive",
        WS_POPUP | WS_CAPTION,
        cx, cy, winW, winH,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!dlg) { g_dlgZipNamePtr = nullptr; return; }
    g_noModInfoDlg = dlg;

    // Single centered OK button.
    int btnW = (int)(160 * g_dpiScale);
    int btnH = (int)(46  * g_dpiScale);
    RECT cr; GetClientRect(dlg, &cr);
    int btnX = (cr.right - btnW) / 2;
    int btnY = cr.bottom - btnH - (int)(20 * g_dpiScale);
    MkStdBtn(dlg, L"OK", 1, btnX, btnY, btnW, btnH, true, ButtonKind::NexusUpdate);

    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    EnableWindow(parent, FALSE);

    MSG msg;
    while (IsWindow(dlg)) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (msg.message == WM_KEYDOWN && msg.hwnd == dlg) {
            if (msg.wParam == VK_ESCAPE || msg.wParam == VK_RETURN) {
                DestroyWindow(dlg);
                break;
            }
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (IsWindow(dlg)) DestroyWindow(dlg);
    g_dlgZipNamePtr = nullptr;
    g_noModInfoDlg = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────
//  UNINSTALL CONFIRM DIALOG  (themed modal — "Delete <mod>?")
// ─────────────────────────────────────────────────────────────────────────
//
// Shown from the mod-list right-click context menu when the user picks
// Uninstall. Two NexusUpdate-styled buttons:
//   Cancel  → leaves the mod alone (default; Esc and the close-X also
//             route here)
//   Delete  → returns 1 to the caller, which then DeleteFolderRecursive's
//             the mod's directory
//
// Cancel is positioned on the left and is the default focus, so a
// reflexive Enter press dismisses the dialog without deleting. Delete
// requires an explicit click — there's no keyboard shortcut for it,
// because muscle memory should not be able to nuke a mod by accident.

static const wstring* g_dlgUninstallNamePtr = nullptr;   // shared with paint
static HWND g_uninstallDlg = nullptr;

static LRESULT CALLBACK UninstallDlgProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        int W = rc.right, H = rc.bottom;
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);
        {
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

            Bitmap* stone = AssetImage(L"bg_stone.png");
            if (stone) {
                int sw = (int)stone->GetWidth();
                int sh = (int)stone->GetHeight();
                int cropW = (sw < W) ? sw : W;
                int cropH = (sh < H) ? sh : H;
                Rect dst(0, 0, W, H);
                g.DrawImage(stone, dst, 40, 40, cropW, cropH, UnitPixel);
            } else {
                SolidBrush bg(Color(28, 24, 20));
                g.FillRectangle(&bg, 0, 0, W, H);
            }
            Pen border(Tok::Gold, 2.0f);
            g.DrawRectangle(&border, 1, 1, W - 3, H - 3);
            Pen innerB(Tok::Bronze, 1.0f);
            g.DrawRectangle(&innerB, 3, 3, W - 7, H - 7);

            SolidBrush textBr(Tok::TextParchment);
            SolidBrush goldBr(Tok::Gold);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsNoWrap);

            int padX = (int)(24 * g_dpiScale);
            int y    = (int)(22 * g_dpiScale);
            int lineH= (int)(30 * g_dpiScale);

            g.DrawString(L"Delete Mod", -1,
                         g_fModName ? g_fModName : g_fBtn,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)lineH),
                         &sf, &goldBr);
            y += lineH;

            // Mod name in quotes — visually anchors which mod is on the
            // chopping block, even when modnames are long.
            if (g_dlgUninstallNamePtr && !g_dlgUninstallNamePtr->empty()) {
                wstring nm = L"\u201C" + *g_dlgUninstallNamePtr + L"\u201D";
                g.DrawString(nm.c_str(), -1,
                             g_fModName ? g_fModName : g_fBtn,
                             RectF((REAL)padX, (REAL)y,
                                   (REAL)(W - 2 * padX), (REAL)lineH),
                             &sf, &textBr);
            }
            y += lineH;
            y += (int)(8 * g_dpiScale);

            StringFormat sfWrap;
            sfWrap.SetAlignment(StringAlignmentCenter);
            sfWrap.SetLineAlignment(StringAlignmentNear);
            int bodyH = (int)(60 * g_dpiScale);
            g.DrawString(
                L"This will permanently delete the mod's folder and all its files. This cannot be undone.",
                -1, g_fBtn,
                RectF((REAL)padX, (REAL)y,
                      (REAL)(W - 2 * padX), (REAL)bodyH),
                &sfWrap, &textBr);
        }
        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_DRAWITEM:
        return MainProc(hw, msg, wp, lp);

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int* res = (int*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        if (res) {
            if      (id == 1) *res = 0;   // Cancel
            else if (id == 2) *res = 1;   // Delete
        }
        if (id == 1 || id == 2) DestroyWindow(hw);
        return 0;
    }

    case WM_CLOSE: {
        int* res = (int*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        if (res) *res = 0;
        DestroyWindow(hw);
        return 0;
    }

    case WM_DESTROY:
        if (g_uninstallDlg == hw) g_uninstallDlg = nullptr;
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

static int ShowUninstallConfirmDialog(HWND parent, const wstring& modName) {
    if (g_uninstallDlg) return 0;
    static bool classReg = false;
    if (!classReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = UninstallDlgProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"AngirisUninstallDlg";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        classReg = true;
    }

    int dlgW = (int)(560 * g_dpiScale);
    int dlgH = (int)(260 * g_dpiScale);
    RECT pr; GetWindowRect(parent, &pr);
    RECT clientReq = { 0, 0, dlgW, dlgH };
    AdjustWindowRectEx(&clientReq, WS_POPUP | WS_CAPTION, FALSE, WS_EX_DLGMODALFRAME);
    int winW = clientReq.right - clientReq.left;
    int winH = clientReq.bottom - clientReq.top;
    int cx = (pr.left + pr.right) / 2 - winW / 2;
    int cy = (pr.top + pr.bottom) / 2 - winH / 2;

    int result = 0;
    g_dlgUninstallNamePtr = &modName;
    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"AngirisUninstallDlg",
        L"Delete Mod",
        WS_POPUP | WS_CAPTION,
        cx, cy, winW, winH,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!dlg) { g_dlgUninstallNamePtr = nullptr; return 0; }
    g_uninstallDlg = dlg;
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&result);

    int btnW   = (int)(170 * g_dpiScale);
    int btnH   = (int)(46  * g_dpiScale);
    int btnGap = (int)(16  * g_dpiScale);
    RECT cr; GetClientRect(dlg, &cr);
    int rowW = 2 * btnW + btnGap;
    int rowX = (cr.right - rowW) / 2;
    int rowY = cr.bottom - btnH - (int)(20 * g_dpiScale);
    // Cancel on the left so the user's reflexive default action sits
    // closer to where they'd naturally land. id=1 (Cancel) is also the
    // initial focus so Enter triggers Cancel.
    HWND bCancel = MkStdBtn(dlg, L"Cancel", 1, rowX,                  rowY,
                            btnW, btnH, true, ButtonKind::NexusUpdate);
    MkStdBtn(dlg, L"Delete",                2, rowX + btnW + btnGap,  rowY,
             btnW, btnH, true, ButtonKind::NexusUpdate);

    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    EnableWindow(parent, FALSE);
    SetFocus(bCancel);    // Enter → Cancel, the safe default

    MSG msg;
    while (IsWindow(dlg)) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (msg.message == WM_KEYDOWN && msg.hwnd == dlg) {
            if (msg.wParam == VK_ESCAPE) {
                result = 0;
                DestroyWindow(dlg);
                break;
            }
            // Deliberately no Enter shortcut for Delete — destructive
            // action must be an explicit click. (Enter while Cancel
            // has focus naturally routes to Cancel via IsDialogMessage.)
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (IsWindow(dlg)) DestroyWindow(dlg);
    g_dlgUninstallNamePtr = nullptr;
    g_uninstallDlg = nullptr;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────
//  SET PATH DIALOG  (themed modal — "D2R install folder not configured")
// ─────────────────────────────────────────────────────────────────────────
//
// Shown when a zip is dropped but the launcher doesn't know where D2R
// lives. Two buttons:
//   Set Path  → runs the SHBrowseForFolder picker (same as the rail
//               "..." button); on success the worker continues with the
//               current zip
//   Cancel    → skips this zip; remaining queued zips will hit the same
//               dialog again on their turn (unless one of them set the
//               path)
//
// Returns 1 if a path was set (worker continues with the zip), 0 if
// cancelled (worker skips the zip).

static HWND g_setPathDlg = nullptr;

static LRESULT CALLBACK SetPathDlgProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        int W = rc.right, H = rc.bottom;
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);
        {
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

            Bitmap* stone = AssetImage(L"bg_stone.png");
            if (stone) {
                int sw = (int)stone->GetWidth();
                int sh = (int)stone->GetHeight();
                int cropW = (sw < W) ? sw : W;
                int cropH = (sh < H) ? sh : H;
                Rect dst(0, 0, W, H);
                g.DrawImage(stone, dst, 40, 40, cropW, cropH, UnitPixel);
            } else {
                SolidBrush bg(Color(28, 24, 20));
                g.FillRectangle(&bg, 0, 0, W, H);
            }
            Pen border(Tok::Gold, 2.0f);
            g.DrawRectangle(&border, 1, 1, W - 3, H - 3);
            Pen innerB(Tok::Bronze, 1.0f);
            g.DrawRectangle(&innerB, 3, 3, W - 7, H - 7);

            SolidBrush textBr(Tok::TextParchment);
            SolidBrush goldBr(Tok::Gold);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsNoWrap);

            int padX = (int)(24 * g_dpiScale);
            int y    = (int)(22 * g_dpiScale);
            int lineH= (int)(30 * g_dpiScale);

            g.DrawString(L"D2R Install Folder Not Set", -1,
                         g_fModName ? g_fModName : g_fBtn,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)lineH),
                         &sf, &goldBr);
            y += lineH;
            y += (int)(12 * g_dpiScale);

            StringFormat sfWrap;
            sfWrap.SetAlignment(StringAlignmentCenter);
            sfWrap.SetLineAlignment(StringAlignmentNear);
            int bodyH = (int)(60 * g_dpiScale);
            g.DrawString(L"The launcher needs to know where Diablo II: Resurrected is installed before it can extract mods. Set the folder now to continue installing, or cancel to skip this archive.",
                         -1, g_fBtn,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)bodyH),
                         &sfWrap, &textBr);
        }
        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_DRAWITEM:
        return MainProc(hw, msg, wp, lp);

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int* res = (int*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        if (res) {
            if      (id == 1) *res = 1;   // Set Path
            else if (id == 2) *res = 0;   // Cancel
        }
        if (id == 1 || id == 2) DestroyWindow(hw);
        return 0;
    }

    case WM_CLOSE: {
        int* res = (int*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        if (res) *res = 0;
        DestroyWindow(hw);
        return 0;
    }

    case WM_DESTROY:
        if (g_setPathDlg == hw) g_setPathDlg = nullptr;
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

// Returns 1 if user picked "Set Path" AND successfully chose a folder
// (g_cfg.d2rPath is now populated). Returns 0 otherwise (Cancel, Esc,
// or the SHBrowseForFolder picker was itself cancelled).
static int ShowSetPathDialog(HWND parent) {
    if (g_setPathDlg) return 0;
    static bool classReg = false;
    if (!classReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = SetPathDlgProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"AngirisSetPathDlg";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        classReg = true;
    }

    int dlgW = (int)(580 * g_dpiScale);
    int dlgH = (int)(260 * g_dpiScale);
    RECT pr; GetWindowRect(parent, &pr);
    RECT clientReq = { 0, 0, dlgW, dlgH };
    AdjustWindowRectEx(&clientReq, WS_POPUP | WS_CAPTION, FALSE, WS_EX_DLGMODALFRAME);
    int winW = clientReq.right - clientReq.left;
    int winH = clientReq.bottom - clientReq.top;
    int cx = (pr.left + pr.right) / 2 - winW / 2;
    int cy = (pr.top + pr.bottom) / 2 - winH / 2;

    int result = 0;
    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"AngirisSetPathDlg",
        L"D2R Install Folder Not Set",
        WS_POPUP | WS_CAPTION,
        cx, cy, winW, winH,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!dlg) return 0;
    g_setPathDlg = dlg;
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&result);

    int btnW = (int)(170 * g_dpiScale);
    int btnH = (int)(46  * g_dpiScale);
    int btnGap = (int)(16 * g_dpiScale);
    RECT cr; GetClientRect(dlg, &cr);
    int rowW = 2 * btnW + btnGap;
    int rowX = (cr.right - rowW) / 2;
    int rowY = cr.bottom - btnH - (int)(20 * g_dpiScale);
    MkStdBtn(dlg, L"Set Path", 1, rowX,                  rowY, btnW, btnH,
             true, ButtonKind::NexusUpdate);
    MkStdBtn(dlg, L"Cancel",   2, rowX + btnW + btnGap,  rowY, btnW, btnH,
             true, ButtonKind::NexusUpdate);

    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    EnableWindow(parent, FALSE);

    MSG msg;
    while (IsWindow(dlg)) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (msg.message == WM_KEYDOWN && msg.hwnd == dlg) {
            if (msg.wParam == VK_ESCAPE) {
                result = 0;
                DestroyWindow(dlg);
                break;
            }
            if (msg.wParam == VK_RETURN) {
                result = 1;
                DestroyWindow(dlg);
                break;
            }
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (IsWindow(dlg)) DestroyWindow(dlg);
    g_setPathDlg = nullptr;

    // If user picked "Set Path", run the folder picker. Returns 1 only if
    // a folder was actually chosen — Cancel from the picker rolls back
    // to 0 and the worker will skip this zip.
    if (result == 1) {
        bool got = PromptForD2RPath(parent);
        if (!got) result = 0;
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────
//  PROGRESS DIALOG  (themed modeless — install in progress with 5-stage bar)
// ─────────────────────────────────────────────────────────────────────────
//
// Shown when the install worker starts on its first zip, hidden when the
// queue drains. Disables the main window so the rest of the UI is paused
// for the duration of the install. The 5-stage bar uses progress_bar_0
// through progress_bar_4 assets (the 5 rows of the supplied artwork —
// empty / ~25% / ~50% / ~75% / full).
//
// Stage mapping (per zip):
//   0  starting
//   1  extracted to temp
//   2  located modinfo
//   3  copying files
//   4  done
//
// Worker advances stages via SendMessage(MSG_ZIP_PROGRESS_UPDATE). The
// UI thread handler updates the globals and invalidates the dialog's
// client area.

// Progress state declarations live further up in the file (near the
// g_progressDlg HWND declaration) so the launcher self-update worker
// can write into them — that worker is defined before this point.

static LRESULT CALLBACK ProgressDlgProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        int W = rc.right, H = rc.bottom;
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);
        {
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

            // Themed background — same stone + double border as the
            // other install dialogs.
            Bitmap* stone = AssetImage(L"bg_stone.png");
            if (stone) {
                int sw = (int)stone->GetWidth();
                int sh = (int)stone->GetHeight();
                int cropW = (sw < W) ? sw : W;
                int cropH = (sh < H) ? sh : H;
                Rect dst(0, 0, W, H);
                g.DrawImage(stone, dst, 40, 40, cropW, cropH, UnitPixel);
            } else {
                SolidBrush bg(Color(28, 24, 20));
                g.FillRectangle(&bg, 0, 0, W, H);
            }
            Pen border(Tok::Gold, 2.0f);
            g.DrawRectangle(&border, 1, 1, W - 3, H - 3);
            Pen innerB(Tok::Bronze, 1.0f);
            g.DrawRectangle(&innerB, 3, 3, W - 7, H - 7);

            SolidBrush textBr(Tok::TextParchment);
            SolidBrush goldBr(Tok::Gold);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsNoWrap);

            int padX  = (int)(24 * g_dpiScale);
            int y     = (int)(22 * g_dpiScale);
            int lineH = (int)(30 * g_dpiScale);

            // Title line — zip install: "Installing Mod" (with N-of-M
            // suffix when batching). Launcher self-update no longer
            // uses this dialog; it has its own popup window.
            wstring title = L"Installing Mod";
            if (g_progressZipTotal > 1) {
                wchar_t buf[64];
                swprintf_s(buf, 64, L"Installing Mod %d of %d",
                           g_progressZipIdx, g_progressZipTotal);
                title = buf;
            }
            g.DrawString(title.c_str(), -1,
                         g_fModName ? g_fModName : g_fBtn,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)lineH),
                         &sf, &goldBr);
            y += lineH;

            // Filename line
            g.DrawString(g_progressZipName.c_str(), -1,
                         g_fModName ? g_fModName : g_fBtn,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)lineH),
                         &sf, &textBr);
            y += lineH;
            y += (int)(10 * g_dpiScale);

            // Stage label
            g.DrawString(g_progressStageLabel.c_str(), -1, g_fBtn,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)lineH),
                         &sf, &textBr);
            y += lineH;
            y += (int)(8 * g_dpiScale);

            // ── 5-stage progress bar ──────────────────────────────────────
            // Each of progress_bar_0.png .. progress_bar_4.png is a full
            // bar drawn at the same logical width; only the fill amount
            // differs. We just pick the asset matching g_progressStage.
            int barX = padX;
            int barW = W - 2 * padX;
            int barH = (int)(34 * g_dpiScale);
            int barY = y;
            int stage = g_progressStage;
            if (stage < 0) stage = 0;
            if (stage > 4) stage = 4;
            wchar_t assetName[32];
            swprintf_s(assetName, 32, L"progress_bar_%d.png", stage);
            Bitmap* barBM = AssetImage(assetName);
            if (barBM) {
                // Render the asset at the dialog's bar geometry, using
                // 9-slice if the asset is much wider than the target so
                // the decorative end caps stay crisp.
                int aw = (int)barBM->GetWidth();
                int ah = (int)barBM->GetHeight();
                // 9-slice with 60 px corner inset (matches the chevron
                // ornament width in the supplied artwork). Falls back
                // to a stretched draw if the asset is too small for the
                // inset.
                int inset = 60;
                if (aw > inset * 2 + 10 && ah > 0) {
                    DrawButton9Slice(g, barBM, barX, barY, barW, barH, inset);
                } else {
                    InterpolationMode prev = g.GetInterpolationMode();
                    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                    g.DrawImage(barBM, barX, barY, barW, barH);
                    g.SetInterpolationMode(prev);
                }
            } else {
                // Asset-less fallback — a programmatic bar so the dialog
                // is still useful while the user is preparing the .png
                // assets. Track + fill, no theming.
                Pen trackPen(Tok::Bronze, 1.0f);
                g.DrawRectangle(&trackPen, barX, barY, barW - 1, barH - 1);
                int fillW = (barW - 4) * stage / 4;
                SolidBrush fillBr(Tok::Gold);
                if (fillW > 0)
                    g.FillRectangle(&fillBr, barX + 2, barY + 2, fillW, barH - 4);
            }
        }
        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_CLOSE:
        // Suppress — the user shouldn't close the progress dialog
        // mid-install. It closes itself when the queue drains.
        return 0;

    case WM_DESTROY:
        if (g_progressDlg == hw) g_progressDlg = nullptr;
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

static HWND ShowProgressDialog(HWND parent) {
    if (g_progressDlg) return g_progressDlg;

    static bool classReg = false;
    if (!classReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = ProgressDlgProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"AngirisProgressDlg";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        classReg = true;
    }

    int dlgW = (int)(620 * g_dpiScale);
    int dlgH = (int)(260 * g_dpiScale);
    RECT pr; GetWindowRect(parent, &pr);
    // Borderless (no WS_CAPTION) — the title is rendered in the body so
    // there's no chrome to close accidentally.
    int cx = (pr.left + pr.right) / 2 - dlgW / 2;
    int cy = (pr.top + pr.bottom) / 2 - dlgH / 2;

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"AngirisProgressDlg",
        L"Installing Mods",
        WS_POPUP | WS_CLIPCHILDREN,
        cx, cy, dlgW, dlgH,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!dlg) return nullptr;
    g_progressDlg = dlg;
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    EnableWindow(parent, FALSE);   // pause the main UI
    return dlg;
}

static void UpdateProgressDialog(const ProgressUpdate& p) {
    g_progressStage      = p.stage;
    g_progressZipIdx     = p.zipIdx;
    g_progressZipTotal   = p.zipTotal;
    g_progressZipName    = p.zipName;
    g_progressStageLabel = p.stageLabel;

    if (g_progressDlg) {
        InvalidateRect(g_progressDlg, nullptr, FALSE);
        UpdateWindow(g_progressDlg);   // synchronous repaint — important
                                       // because the worker is waiting on
                                       // this SendMessage round-trip and
                                       // we want the new stage visible
                                       // before it proceeds.
    }
}

static void HideProgressDialog() {
    if (!g_progressDlg) return;
    HWND p = (HWND)GetWindowLongPtrW(g_progressDlg, GWLP_HWNDPARENT);
    if (p) EnableWindow(p, TRUE);
    DestroyWindow(g_progressDlg);
    g_progressDlg = nullptr;
    g_progressZipName.clear();
    g_progressStageLabel.clear();
    g_progressZipIdx = 0;
    g_progressZipTotal = 0;
    g_progressStage = 0;
    if (p) SetForegroundWindow(p);
}

// ─────────────────────────────────────────────────────────────────────────
//  HOVER TOOLTIP  (themed popup — playtime + last played)
// ─────────────────────────────────────────────────────────────────────────
//
// Appears after the cursor has dwelled over a mod row for ~2 seconds.
// Two lines: "Playtime: ..." and "Last played: ...". Themed to match
// the rest of the launcher (stone background, gold/bronze border, the
// user-selected button font).
//
// The HWND is created on demand by ShowHoverTip and destroyed on the
// next HideHoverTip — no caching or reuse, since the lifetime is short
// and reconstructing is cheap. WS_EX_NOACTIVATE keeps the tooltip from
// stealing focus from whatever the user was interacting with, and
// WS_EX_TOOLWINDOW keeps it out of the taskbar.

static LRESULT CALLBACK HoverTipProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        int W = rc.right, H = rc.bottom;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);
        {
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

            Bitmap* stone = AssetImage(L"bg_stone.png");
            if (stone) {
                int sw = (int)stone->GetWidth();
                int sh = (int)stone->GetHeight();
                int cropW = (sw < W) ? sw : W;
                int cropH = (sh < H) ? sh : H;
                Rect dst(0, 0, W, H);
                g.DrawImage(stone, dst, 40, 40, cropW, cropH, UnitPixel);
            } else {
                SolidBrush bg(Color(28, 24, 20));
                g.FillRectangle(&bg, 0, 0, W, H);
            }
            Pen border(Tok::Gold, 2.0f);
            g.DrawRectangle(&border, 1, 1, W - 3, H - 3);
            Pen innerB(Tok::Bronze, 1.0f);
            g.DrawRectangle(&innerB, 3, 3, W - 7, H - 7);

            SolidBrush textBr(Tok::TextParchment);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentNear);
            sf.SetLineAlignment(StringAlignmentNear);
            sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsNoWrap);

            int padX  = (int)(14 * g_dpiScale);
            int y     = (int)(12 * g_dpiScale);
            int lineH = (int)(22 * g_dpiScale);

            Font* font = g_fBtn;
            if (font) {
                g.DrawString(g_hoverTipText1.c_str(), -1, font,
                    RectF((REAL)padX, (REAL)y,
                          (REAL)(W - 2 * padX), (REAL)lineH),
                    &sf, &textBr);
                y += lineH;
                g.DrawString(g_hoverTipText2.c_str(), -1, font,
                    RectF((REAL)padX, (REAL)y,
                          (REAL)(W - 2 * padX), (REAL)lineH),
                    &sf, &textBr);
            }
        }
        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_NCDESTROY:
        if (g_hoverTipHwnd == hw) g_hoverTipHwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

static void HideHoverTip() {
    if (g_hoverTipHwnd) {
        DestroyWindow(g_hoverTipHwnd);
        g_hoverTipHwnd = nullptr;
    }
}

static void ShowHoverTip(int modIdx) {
    // Tear down any existing tooltip first — only one is ever visible.
    HideHoverTip();
    if (modIdx < 0 || modIdx >= (int)g_mods.size()) return;

    const ModInfo& m = g_mods[modIdx];
    auto it = g_playtimes.find(m.folder);
    PlaytimeRec r;
    if (it != g_playtimes.end()) r = it->second;

    g_hoverTipText1 = L"Playtime: "    + FormatPlaytime(r.seconds);
    g_hoverTipText2 = L"Last played: " + FormatLastPlayed(r.lastPlayed);

    static bool classReg = false;
    if (!classReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = HoverTipProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"AngirisHoverTip";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        classReg = true;
    }

    int W = (int)(290 * g_dpiScale);
    int H = (int)(76  * g_dpiScale);

    // Position the tooltip relative to the cursor, offset down-right so
    // the cursor doesn't sit on top of the tooltip frame. If that
    // position would push the tooltip off the working monitor, flip
    // it to up-left of the cursor so it stays fully visible.
    POINT pt;
    GetCursorPos(&pt);
    int off = (int)(18 * g_dpiScale);
    int x = pt.x + off;
    int y = pt.y + off;
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfoW(mon, &mi)) {
        if (x + W > mi.rcWork.right)  x = pt.x - W - off;
        if (y + H > mi.rcWork.bottom) y = pt.y - H - off;
        if (x < mi.rcWork.left)       x = mi.rcWork.left;
        if (y < mi.rcWork.top)        y = mi.rcWork.top;
    }

    g_hoverTipHwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"AngirisHoverTip",
        L"",
        WS_POPUP,
        x, y, W, H,
        nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (g_hoverTipHwnd) {
        ShowWindow(g_hoverTipHwnd, SW_SHOWNOACTIVATE);
        UpdateWindow(g_hoverTipHwnd);
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  CONTROL CREATION
// ═══════════════════════════════════════════════════════════════════════

// Small helper for standard Win32 buttons (used by left rail nav + the
// Mod Description's Discord/Docs/Website link buttons).
// Creates a push-button as BS_OWNERDRAW so all buttons paint through the
// shared WM_DRAWITEM handler. The optional `kind` controls which asset
// family will paint it (Nav / Refresh / NexusUpdate / Play / Arrow). The
// label text rendering still happens in WM_DRAWITEM, except for kinds
// whose art has the label baked in (Refresh).
static HWND MkStdBtn(HWND parent, const wchar_t* lbl, int id,
                     int x, int y, int w, int h, bool visible = true,
                     ButtonKind kind = ButtonKind::Nav) {
    DWORD style = WS_CHILD | BS_PUSHBUTTON | BS_OWNERDRAW;
    if (visible) style |= WS_VISIBLE;
    HWND hw = CreateWindow(L"BUTTON", lbl, style,
        x, y, w, h, parent, (HMENU)(UINT_PTR)id, g_hInst, nullptr);
    RegisterButton(hw, kind);
    return hw;
}

static void CreateControls(HWND hw) {
    using namespace LO;

    // ── Left rail nav buttons ───────────────────────────────────────────
    // Owner-drawn (BS_OWNERDRAW via MkStdBtn). Positions assigned by
    // Layout() so they track the frame inset; placeholder size below
    // matches the native btn_nav_*.png art (310×76).
    g_hwNavMods    = MkStdBtn(hw, L"Mods",    IDC_NAV_MODS,    0, 0, 310, 76);
    g_hwNavOptions = MkStdBtn(hw, L"Options", IDC_NAV_OPTIONS, 0, 0, 310, 76);
    g_hwNavLogs    = MkStdBtn(hw, L"Logs",    IDC_NAV_LOGS,    0, 0, 310, 76);
    g_hwNavHelp    = MkStdBtn(hw, L"Help",    IDC_NAV_HELP,    0, 0, 310, 76);
    g_hwNavAbout   = MkStdBtn(hw, L"About",   IDC_NAV_ABOUT,   0, 0, 310, 76);
    g_hwNavExit    = MkStdBtn(hw, L"Exit",    IDC_NAV_EXIT,    0, 0, 310, 76);

    // ── Loader Options section (bottom of left rail) ────────────────────
    // The two dropdowns are click-targets painted by MainProc, and the
    // path bar is now fully programmatic (paints text_box.png + path
    // text via PaintLeftRail). Only the "..." button remains a real
    // Win32 control. All three positions are computed in Layout().

    g_hwLoaderDirBtn = MkStdBtn(hw, L"...", IDC_LOADER_DIR_BTN,
                                0, 0, 37, 36, true, ButtonKind::Ellipse);

    // ── Center column: Nexus Mod Directory + Update Selected Mod ────────
    // Both buttons share the btn_nexus_update_* asset family (254×54 native).
    g_hwBrowseMods = MkStdBtn(hw, L"Nexus Mod Directory", IDC_BROWSE_MODS,
                              0, 0, 254, 54, false,        // positioned in Layout()
                              ButtonKind::NexusUpdate);
    ShowWindow(g_hwBrowseMods, SW_SHOW);
    g_hwUpdateMod  = MkStdBtn(hw, L"Update Selected Mod", IDC_UPDATE_MOD,
                              0, 0, 254, 54, false,
                              ButtonKind::NexusUpdate);
    ShowWindow(g_hwUpdateMod, SW_SHOW);

    // ── Refresh button (top-right of center column) ─────────────────────
    // Its own asset family (btn_refresh_*). The "Refresh" text is baked
    // into the art when the asset is present; live text only renders if
    // the asset is missing.
    g_hwRefresh = CreateWindow(L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_PUSHBUTTON,
        0, 0, 138, 52, hw, (HMENU)(UINT_PTR)IDC_REFRESH_BTN, g_hInst, nullptr);
    SetWindowText(g_hwRefresh, L"Refresh");        // fallback label
    RegisterButton(g_hwRefresh, ButtonKind::Refresh);

    // ── Mod list (custom-painted child window) ──────────────────────────
    g_hwList = CreateWindow(L"Angiris_ModList", nullptr,
        WS_CHILD | WS_VISIBLE,
        0, 0, 100, 100, hw, (HMENU)(UINT_PTR)IDC_MOD_LIST, g_hInst, nullptr);

    // ── Right column: Mod Description link buttons (per-mod, hidden by default) ──
    // Three Mod Description link buttons. Each is 85×85 square with its
    // own art (btn_docs.png / btn_discord.png / btn_website.png at 4x
    // native = 340×340). When the per-button asset is missing, the
    // paint code falls back to a 9-sliced btn_nexus_update.png and
    // renders the button's label as a single-letter glyph — D / X / W
    // for Docs / Discord / Website respectively. The label is invisible
    // when the real asset is present (skipLabel in WM_DRAWITEM).
    g_hwModDocs    = MkStdBtn(hw, L"D", IDC_MOD_DOCS,    0, 0, 85, 85, false, ButtonKind::ModLinkDocs);
    g_hwModDiscord = MkStdBtn(hw, L"X", IDC_MOD_DISCORD, 0, 0, 85, 85, false, ButtonKind::ModLinkDiscord);
    g_hwModWebsite = MkStdBtn(hw, L"W", IDC_MOD_WEBSITE, 0, 0, 85, 85, false, ButtonKind::ModLinkWebsite);

    // ── Right column: PLAY button ────────────────────────────────────────
    g_hwLaunch = MkStdBtn(hw, L"PLAY", IDC_LAUNCH_BTN, 0, 0, 422, 102, true,
                          ButtonKind::Play);
    EnableWindow(g_hwLaunch, FALSE);

    // ── Bottom expansion arrow toggle ───────────────────────────────────
    // Native art is 68×50. The Unicode caption (▼/▲) is the fallback when
    // btn_expand_arrow_*.png is missing — visible only in that case.
    g_hwExpandToggle = MkStdBtn(hw, L"\u25BC", IDC_EXPAND_TOGGLE,
                                0, 0, 68, 50, true,
                                ButtonKind::Arrow);

    // ── Bottom panel placeholder buttons (created hidden) ───────────────
    // Tools (6), references (3), downloads (3). Wired in Commit 6.
    const wchar_t* toolLabels[6] = {
        L"Edit TXT Files",
        L"Edit Sprite Files",
        L"Edit JSON Files",
        L"Edit Models",
        L"Edit Textures",
        L"Edit Particles",
    };
    for (int i = 0; i < 6; ++i)
        g_hwBottomTools[i] = MkStdBtn(hw, toolLabels[i], IDC_TOOL_FIRST + i,
                                      0, 0, 254, 54, false, ButtonKind::NexusUpdate);

    const wchar_t* refLabels[3] = {
        L"Eez's File Guides",
        L"Phrozen Keep",
        L"Amazon Basin",
    };
    for (int i = 0; i < 3; ++i)
        g_hwBottomRefs[i] = MkStdBtn(hw, refLabels[i], IDC_REF_FIRST + i,
                                     0, 0, 254, 54, false, ButtonKind::NexusUpdate);

    const wchar_t* dlLabels[3] = {
        L"AFJ Pro Text Editor",
        L"Eez's Sprite Editor",
        L"Visual Basic Code",
    };
    for (int i = 0; i < 3; ++i)
        g_hwBottomDls[i] = MkStdBtn(hw, dlLabels[i], IDC_DL_FIRST + i,
                                    0, 0, 254, 54, false, ButtonKind::NexusUpdate);
}

// ═══════════════════════════════════════════════════════════════════════
//  ENTRY POINT
// ═══════════════════════════════════════════════════════════════════════

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    g_hInst = hInst;
    // Declare DPI awareness BEFORE the GDI+/Common Controls init and before
    // any window is created — once a window is up, the awareness state for
    // this process is locked in. With per-monitor V2, the OS stops
    // bitmap-scaling our window and starts handing us real pixel sizes,
    // and we take responsibility for scaling everything ourselves.
    InitDpiAwareness();

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    InitCommonControls();

    GdiplusStartupInput gsi;
    GdiplusStartup(&g_gdipToken, &gsi, nullptr);
    g_pfc = new Gdiplus::PrivateFontCollection();   // must outlive every Font

    // Read the config first so we know g_userScale; then query the system
    // DPI; then compute the final g_scale used by S()/SF()/U(). Fonts and
    // the window size below pick this up automatically through SF()/S().
    LoadCfg();
    g_dpiScale  = QuerySystemDpiScale();

    // Between-sessions DPI change → reset uiScale to 1.0. A setting that
    // felt right at 100% scaling can leave the window tiny at 150% (or
    // vice-versa), and the user can't easily reach the toolbar dropdown
    // to fix it if it's offscreen. The 0.05 tolerance covers float
    // round-trip noise through the JSON config.
    if (fabs(g_dpiScale - g_cfg.lastDpiScale) > 0.05) {
        g_cfg.uiScale = 1.0;
    }

    // Narrow uiScale to one of the three presets active at this DPI
    // (the cycling button only exposes those three — see ActiveScalePresets).
    {
        int a, b, c;
        ActiveScalePresets(a, b, c);
        double bestDist = 1e9;
        double best = 1.00;
        for (int idx : { a, b, c }) {
            double p = g_scalePresets[idx].mul;
            double d = (p > g_cfg.uiScale) ? (p - g_cfg.uiScale) : (g_cfg.uiScale - p);
            if (d < bestDist) { bestDist = d; best = p; }
        }
        g_cfg.uiScale = best;
    }

    g_userScale = g_cfg.uiScale;
    g_scale     = g_userScale * g_dpiScale;

    LoadFonts();
    LoadSeedsJson();
    UpdateUserFontFromCfg();   // pick up cfg.fontName so CreateGdipFonts uses it
    ApplyColorChange();        // apply cfg.fontColorIdx to Tok::Gold/GoldBright
    CreateGdipFonts();

    if (!g_cfg.d2rPath.empty()
        && GetFileAttributes(g_cfg.d2rPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        g_cfg.d2rPath.clear();
    if (g_cfg.d2rPath.empty()) g_cfg.d2rPath = FindD2RInstall();
    LoadLoaderOpts();         // reads <D2R>\D2RLoader.ini (missing file = defaults)

    // Custom child window classes are registered in commits 2-6 as they
    // come back online. The Loader-options dropdown popup window class is
    // rebuilt later — for now the dropdowns are click-targets only,
    // painted by MainProc once that paint code lands in Commit 5.
    RegisterModListClass(hInst);

    LoadUpdateCache();
    LoadPlaytimes();

    // Wipe the previous-launcher artifact (Angiris.exe.old) if a prior
    // self-update left one behind. Cheap no-op when there isn't one,
    // so safe to run unconditionally on every launch.
    CleanupLauncherOldExe();

    WNDCLASSEX wc = { sizeof(wc) };
    // No CS_HREDRAW/CS_VREDRAW: we invalidate precisely on resize so the
    // expand/collapse of the bottom panel doesn't flicker the whole window.
    wc.style         = 0;
    wc.lpfnWndProc   = MainProc;
    wc.hInstance     = hInst;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"Angiris_Main";
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);

    HICON hIcon = (HICON)LoadImage(hInst, MAKEINTRESOURCE(1),
        IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    wc.hIcon   = hIcon;
    wc.hIconSm = (HICON)LoadImage(hInst, MAKEINTRESOURCE(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    RegisterClassEx(&wc);

    // Center on the primary monitor. screenW/screenH come from the OS in
    // physical pixels; LO::WIN_W / LO::WIN_H are logical pixels, so we
    // scale them through S() before computing the centered origin.
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winWphys = S(LO::WIN_W);
    int winHphys = S(LO::WIN_H);
    int posX = (screenW - winWphys) / 2;
    int posY = (screenH - winHphys) / 2;
    if (posY < 0) posY = 0;
    if (posX < 0) posX = 0;

    g_hwMain = CreateWindowEx(
        WS_EX_APPWINDOW,
        L"Angiris_Main", L"D2RLoader",
        // Custom chrome: no system title bar. The asset's frame_main.png
        // is the chrome; we paint our own min/close buttons over the
        // top-right filigree and handle dragging via WM_NCHITTEST.
        // WS_MINIMIZEBOX is kept so Windows still does the taskbar
        // minimize animation when our custom button is clicked.
        WS_POPUP | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        posX, posY, winWphys, winHphys,
        nullptr, nullptr, hInst, nullptr);

    CreateControls(g_hwMain);
    {
        RECT rc; GetClientRect(g_hwMain, &rc);
        Layout(rc.right, rc.bottom);
    }
    // Accept dropped files — drag-and-drop zip installer (V1.1). The
    // WM_DROPFILES handler in MainProc filters to .zip and feeds the
    // ZipInstallWorker queue.
    DragAcceptFiles(g_hwMain, TRUE);
    ShowWindow(g_hwMain, nShow);
    UpdateWindow(g_hwMain);

    // If CleanupLauncherOldExe couldn't delete Angiris.exe.old up-front
    // (the prior launcher is still releasing its image file), arm the
    // 2-second deferred-retry timer now that g_hwMain exists to deliver
    // WM_TIMER messages.
    StartDeferredOldExeCleanup();

    RefreshMods();
    StartModsWatcher();
    KickUpdateChecks(false);   // honor TTL — use cached entries if fresh

    // Kick off the launcher self-update check on a background thread.
    // The worker hits GitHub's releases-latest API, parks the result
    // in g_launcherUpdate* globals, and PostMessage's
    // MSG_LAUNCHER_UPDATE_AVAILABLE to g_hwMain when a release is
    // available. Done AFTER g_hwMain exists so the post has a valid
    // target; the check runs in parallel with the rest of startup so
    // even a slow network can't delay the window from appearing.
    KickoffLauncherUpdateCheck();

    // ── Startup notices ──────────────────────────────────────────────
    // If the D2R install couldn't be auto-detected, point the user at the
    // Loader Directory "..." button. Otherwise, if the install is known
    // but no mods are present, let them know once.
    if (g_cfg.d2rPath.empty()) {
        MessageBox(g_hwMain,
            L"Could not detect your Diablo II: Resurrected install folder.\n\n"
            L"Please set it using the \"...\" button next to the Loader "
            L"Directory field (bottom-left).",
            L"D2R Install Not Found",
            MB_OK | MB_ICONINFORMATION);
    }
    else if (g_mods.empty()) {
        MessageBox(g_hwMain,
            L"No mods were detected in your D2R \\mods\\ folder.\n\n"
            L"Use \"Nexus Mod Directory\" to find mods, then place them in "
            L"the mods folder (\"Browse Mods Folder\" opens it) and click "
            L"Refresh.",
            L"No Mods Found",
            MB_OK | MB_ICONINFORMATION);
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DestroyAssetCache();   // free image bitmaps before GDI+ shuts down
    DestroyGdipFonts();
    delete g_userFontFamilyOverride;
    g_userFontFamilyOverride = nullptr;
    delete g_pfc;
    g_pfc = nullptr;
    UnloadFonts();
    GdiplusShutdown(g_gdipToken);
    CoUninitialize();
    return (int)msg.wParam;
}
