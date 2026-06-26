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

// Common Win32/GDI+/std includes live in angiris_common.h so every
// translation unit sees the same surface. Module headers follow.
#include "angiris_common.h"
#include "core.h"
#include "version.h"
#include "ini_editor.h"
#include "http.h"
#include "config.h"
#include "update_cache.h"
#include "playtime.h"
#include "seeds.h"
#include "mod_types.h"
#include "mod_scan.h"
#include "launch_flags.h"
#include "mod_config.h"
#include "tool_resolver.h"
#include "fs_utils.h"
#include "mod_updates.h"
#include "save_backup.h"
#include "zip_install.h"
#include "launcher_self_update.h"
#include "assets.h"
#include "fonts.h"
#include "layout.h"
#include "scaling.h"
#include "colors.h"
#include "hover_tip.h"
#include "mod_list.h"
#include "plugin_manager.h"
#include "control_ids.h"
#include "dialogs.h"
#include "paint_helpers.h"
#include "paint_main.h"
#include "ui_state.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winhttp.lib")

// ═══════════════════════════════════════════════════════════════════════
//  DESIGN TOKENS
//  All colors and spacing live here — extend this block, never inline.
// ═══════════════════════════════════════════════════════════════════════

// (extracted to module — was GP/GPA color helpers)

// (extracted to module — was Tok:: + Sp:: namespaces)

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

// (extracted to module — was g_userScale + g_scale globals)

// Logical → physical.
// (extracted to module — was S/SF/U inline functions)

// SetWindowPos that takes LOGICAL coords/sizes and applies S() at the
// Win32 boundary. Use this everywhere in Layout() so the layout math
// stays readable (one unit system: logical pixels) and the scaling is
// applied uniformly. Mouse handlers and Layout never have to know
// about g_scale individually.
// SPosL extracted to scaling.h so layout.cpp + other TUs can use it.
// (extracted to module — was InvalidateRectL + GetClientRectL helpers)

// Per-monitor V2 DPI awareness if available, falling back to per-monitor V1,
// then system-aware. Must be called before the first window is created.
void InitDpiAwareness() {
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
double QuerySystemDpiScale() {
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
// (TBL namespace extracted to layout.h)


// ═══════════════════════════════════════════════════════════════════════
//  CONTROL IDs
// ═══════════════════════════════════════════════════════════════════════
//
// Extracted to control_ids.h so modules other than Angiris.cpp
// (mod_list, plugin_manager, future dialog modules) can see the same
// numeric values without redeclaring them. Header-only.

// (control IDs enum + OPT_NOTIFY_CHANGED extracted to control_ids.h)

// ═══════════════════════════════════════════════════════════════════════
//  DATA TYPES
// ═══════════════════════════════════════════════════════════════════════

// ModInfo struct now lives in mod_types.h.

// Per-mod cached result of the latest update check. Persisted to
// <appdir>\assets\update_cache.json keyed by mod folder name.
// UpdateInfo struct now lives in update_cache.h. See update_cache.h/cpp.

// LaunchBehavior enum and LauncherCfg struct now live in config.h.
// See config.h/config.cpp.

// ── Launcher self-update wiring ────────────────────────────────────────
// The launcher checks the GitHub repository for a newer tagged release
// at startup. When a newer release is found and the user hasn't
// "skipped" that exact tag, a themed dialog offers Update / Skip /
// Ignore. The Update path downloads the release zip, renames the
// running .exe to .old, extracts the new files in place, and spawns
// the new process — see the long comment near LauncherUpdateInstallWorker.
// (extracted to module — was LAUNCHER_VERSION constant)
// (extracted to module — was LAUNCHER_GITHUB_OWNER/REPO)

// (extracted to module — was launcher self-update state globals)

// ── Version label hit-rect (paint state, NOT launcher-update state) ─────
// Filled by PaintBody when it lays out the version label under the
// logo; read by MainProc's WM_LBUTTONDOWN / WM_SETCURSOR to hit-test
// the clickable area. Stays in Angiris.cpp because it's a paint
// concern, not part of the self-update module's state. Was
// accidentally swept up in Phase 3b's anchor-pair removal of the
// launcher_self_update globals — restored here.
RECT g_versionLabelRect = {0, 0, 0, 0};

// ModSettings struct and g_modSettings now live in launch_flags.h/cpp.

// Update-check cache, keyed by mod folder. Persisted to
// <appdir>\assets\update_cache.json. Refetches happen at startup and
// on Refresh Mod List click; within the TTL we serve cached results.
// g_updateInfo now lives in update_cache.cpp. See update_cache.h.

// Per-mod playtime tracking. Persisted to <appdir>\assets\playtime.json
// (centralized, not per-mod) so that re-installing a mod doesn't wipe
// its history — the cache survives any change to the mod's own files.
// `seconds` is the running total of D2R-up-and-running time across all
// launches; `lastPlayed` is the unix epoch of the most recent exit.
// Display surface: the hover tooltip on each mod row.
// PlaytimeRec struct and g_playtimes now live in playtime.h/cpp.

// Tracking state for the current launch. Snapshot at click time so the
// poll handler can attribute play time correctly even if the user
// switches the selected mod mid-game. g_d2rGameStartTick is the
// GetTickCount() value captured the first poll where D2R was seen
// running — that's the proper "game actually playing" start (vs. the
// click tick, which includes loader/spawn overhead).
DWORD   g_d2rGameStartTick = 0;
wstring g_d2rGameModFolder;

// (extracted to module — was UPDATE_* constants + MSG_UPDATE_CHECK_DONE)

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
// (extracted to module — was MSG_ZIP_* and MSG_LAUNCHER/LUPOPUP constants)

// (extracted to module — was ConflictDialogParam + ProgressUpdate)

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
bool   g_d2rTracking   = false;
bool   g_d2rEverSeen   = false;
DWORD  g_d2rLaunchTick = 0;
// True between the post-launch SetTimer and the FIRST IDT_D2R_POLL fire.
// While set, the timer is on its "wait 10 s before first poll" interval;
// the first fire resets the timer to the normal 1 s cadence.
bool   g_pollFirstShot = false;

// Returns true if any process whose image name matches one of `names`
// (case-insensitive) is present in the current process snapshot. The
// snapshot is built once and walked once regardless of how many names
// we're checking, which keeps the polling cost flat.
bool AnyProcessExistsByName(const wchar_t* const* names, size_t count) {
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
const wchar_t* const k_d2rProcessNames[] = {
    L"D2R.exe",
    L"D2RLoader.exe",
};
const size_t k_d2rProcessNameCount =
    sizeof(k_d2rProcessNames) / sizeof(k_d2rProcessNames[0]);
constexpr UINT IDT_D2R_POLL = 9101;         // 1s timer fired on the main HWND
// (extracted to module — was IDT_HOVER_TIP constant)        // 2s timer on the mod list HWND;
                                            //   fires the playtime tooltip
                                            //   if the cursor's been still
                                            //   over a row long enough.
// (extracted to module — was IDT_CLEANUP_OLD_EXE)

// (extracted to module — was .old cleanup state globals)

// Discord Rich Presence integration is held back pending the Discord
// application review process — the IPC code lives in a separate module
// (discord_rpc.cpp / discord_rpc.h) which is currently not included or
// built. See discord_rpc.h for re-enable instructions when the time
// comes.

// Decoded banner image cache, keyed by file path. Reloaded only when
// the path changes — repeated paints of the same banner are free.
// (Now used by the mod list rows, which render banners as backgrounds.)
namespace Gdiplus { class Bitmap; }
// (extracted to module — was g_bannerCache + g_bannerCacheKey)

// ═══════════════════════════════════════════════════════════════════════
//  GLOBALS
// ═══════════════════════════════════════════════════════════════════════

// `g_hInst` is declared extern in core.h so other modules (plugin_manager,
// future dialog modules) can pass it to CreateWindowEx without re-querying.
// The value is set once in wWinMain.
HINSTANCE   g_hInst        = nullptr;
// g_hwMain now lives in core.cpp (extern via core.h) so background
// threads in mod_scan / mod_updates / etc. can PostMessage to it.
HWND        g_hwList       = nullptr;     // mod list (custom-painted)
HWND        g_hwLaunch     = nullptr;     // PLAY button (in right column)

// Mod Description link buttons (per-mod, shown/hidden based on modinfo.json)
HWND        g_hwModDiscord = nullptr;
HWND        g_hwModDocs    = nullptr;
HWND        g_hwModWebsite = nullptr;

// Left rail navigation buttons (open external paths/files)
HWND        g_hwNavMods    = nullptr;
HWND        g_hwNavOptions = nullptr;
HWND        g_hwNavLogs    = nullptr;
HWND        g_hwNavHelp    = nullptr;
HWND        g_hwNavAbout   = nullptr;
HWND        g_hwNavExit    = nullptr;

// Loader Directory row (read-only path + ... browse button)
RECT        g_loaderDirRect = {};            // paint+hit-test rect for the path bar
RECT        g_stashDropdownRect = {};         // Stash Tabs row rect (Layout populates)
// g_dmgDropdownRect removed — the Dmg Display dropdown was replaced by
// the Plugins button (g_hwLoaderPlugins) in the Loader Options layout.

// Center-column toolbar dropdowns (sit between the Nexus/Update button
// row and the expand arrow). All three are painted programmatically and
// hit-tested in WM_LBUTTONDOWN. Layout() populates their rects.
RECT        g_scaleDropdownRect = {};   // top-row Scale label+value (display only, not clickable)
RECT        g_scaleSliderRect   = {};   // 3-state toggle slider below Scale — the click target
RECT        g_onLaunchHeaderRect= {};   // top tier — "ON LAUNCH" header label
RECT        g_onLaunchRect      = {};   // middle tier — value-only textbox (Min/Close/Stay)
RECT        g_onLaunchSliderRect= {};   // bottom tier — 3-state toggle slider

// Measured rendered width of the "Seed" label (in LOGICAL units, not
// scaled). Recomputed every time CreateGdipFonts runs so a font swap
// via the toolbar Font dropdown immediately reflows the combo. Default
// covers the case where the global is read before the first measure.
int         g_seedLabelLogicalW = 44;
RECT        g_fontDropdownRect  = {};
RECT        g_colorDropdownRect = {};
HWND        g_hwLoaderDirBtn = nullptr;     // "..." button
HWND        g_hwLoaderPlugins = nullptr;    // Plugins button — opens plugin manager

// Mod list adjacent buttons
HWND        g_hwRefresh    = nullptr;       // top-right "Refresh"
HWND        g_hwBrowseMods = nullptr;       // bottom-left
HWND        g_hwUpdateMod  = nullptr;       // bottom-right

// Bottom expansion panel
HWND        g_hwExpandToggle = nullptr;     // arrow button
// `g_bottomExpanded` is exposed via core.h so buttons.cpp's Arrow paint
// can flip the chevron art and paint_main / layout (Phase 7c+) can read
// the same flag without a re-entry into MainProc.
bool        g_bottomExpanded = false;

// Custom title-bar button state (rendered as image assets in PaintBody;
// hit-tested in WM_NCHITTEST and WM_LBUTTONDOWN). -1 = no hover, 0 = min,
// 1 = close. Pressed is true while the mouse is held over a button.
static int         g_tbHover    = -1;
int         g_tbPressed  = -1;

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
// (ButtonKind enum extracted to buttons.h)
#include "buttons.h"

// (extracted to module — was ButtonStateTransform + StateTransformFor + AssetNameFor)
// (extracted to module — was AssetNameFor body)
// (extracted to module — was ButtonState struct + g_btnStates)

// (extracted to module — was BtnHoverSubclass)

// (extracted to module — was RegisterButton)

HWND        g_hwBottomTools[6]   = {};      // 6 tool launchers
HWND        g_hwBottomRefs[3]    = {};      // 3 references
HWND        g_hwBottomDls[3]     = {};      // 3 download links

static bool        g_modsDirty    = false;       // watcher saw changes; manual refresh pending

static ULONG_PTR   g_gdipToken    = 0;
// g_cfg now lives in config.cpp. See config.h.
// g_mods and g_selMod now live in mod_scan.cpp. See mod_scan.h.

// Bundled fonts — loaded from assets/fonts/ at startup with FR_PRIVATE
// so they're visible to GDI+ but not added to the system font list.
// (extracted to module — was g_loadedFonts)

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
// (extracted to module — was g_pfc + g_ff* + g_userFont*)

// Cached GDI+ Font instances at the design sizes. Exocet (D2 menu font)
// carries the launcher's identity; Georgia is used where dense legibility
// matters (cmd preview, mod description body, hero meta italics).
// (extracted to module — was g_f* font globals)   // Georgia, 11px

// ═══════════════════════════════════════════════════════════════════════
//  UTILITIES (carried from D2R_ModLauncher.cpp — proven working)
// ═══════════════════════════════════════════════════════════════════════

// AppDir() now lives in core.cpp. See core.h.

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

// (extracted to module — was Asset cache + DrawAssetAt/Stretched/9Slice)

// (DrawAssetAt + DrawButton9Slice extracted to assets.cpp — leftover removed)

// Measures the safe-interior inset of a frame asset by scanning each edge
// inward until it finds a row/column whose opacity drops below the "edge
// filigree" density. The result tells layout how far inset from the window
// edges the content area starts.
//
// Cached per-asset by name. Once computed it's reused for the life of the
// process (no need to re-scan a 1536×1024 bitmap on every paint).
// (extracted to module — was FrameInset + MeasureFrameInset)

// Measures the three internal region boundaries of frame_panel_right.png
// (the right-column panel asset). The asset has horizontal dividers at
// specific Y coordinates that split it into MOD DESCRIPTION (top),
// LAUNCH OPTIONS (middle), and PLAY (bottom) regions. We detect dividers
// by scanning row density; rows that are mostly-opaque across the full
// width are dividers.
//
// All values are in the asset's native pixel space (not stretched), since
// the asset itself is drawn at 1:1 in the launcher.
// (extracted to module — was PanelRegions + MeasurePanelRegions)

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

// ReadTextFile, WriteTextFile, EscapeJson, JsonStr, JsonBool, JsonInt,
// JsonDouble now live in core.cpp. See core.h.

// ═══════════════════════════════════════════════════════════════════════
//  INI LINE-EDITOR
// ═══════════════════════════════════════════════════════════════════════
//
//  D2RLoader.ini contains user-managed config that we should NOT trash
//  when we touch one of its values. These helpers do line-by-line in-place
//  edits: read preserves the full file as-is, write replaces ONLY the
//  line(s) for the key(s) we care about while keeping every comment,
//  blank line, and unknown key untouched.

// TrimWs, ParseIniLine, IniGetInt, IniSetInt now live in ini_editor.cpp.
// See ini_editor.h.

// ═══════════════════════════════════════════════════════════════════════
//  CONFIG I/O
// ═══════════════════════════════════════════════════════════════════════

// CfgPath, LoadCfg, SaveCfg now live in config.cpp. See config.h.

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


// (LoaderOpts struct moved to ui_state.h)
LoaderOpts g_loaderOpts;

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

// SaveLoaderOptDamageIndicator removed — the Dmg Display UI was
// replaced by the Plugins button, so there's no longer a UI path
// that writes [Advanced.Logging] damage_indicator. The value is
// still read in LoadLoaderOpts so any pre-existing setting from
// a prior launcher version is preserved silently on disk.

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

// UpdateCachePath, LoadUpdateCache, SaveUpdateCache now live in
// update_cache.cpp. See update_cache.h.

// Playtime cache I/O (PlaytimeCachePath, LoadPlaytimes, SavePlaytimes,
// RecordPlaytime, FormatPlaytime, FormatLastPlayed) now lives in
// playtime.cpp. See playtime.h.

// ── Hover-tip state ──────────────────────────────────────────────────────
// Shown over a mod row after the cursor has rested on it for ~2 s. Hidden
// on row change, mouse leave, click, or scroll. The display surface is
// the only consumer of g_playtimes outside the recording path.
// (extracted to module — was hover tip globals)

// Defined further down the file (after the existing modal dialogs) so
// these can be called from ModListProc.
// (extracted to module — was hover tip forward decls)

// ── Version comparison ────────────────────────────────────────────────
//
// "2.5.0" > "2.4.1", "1.10" > "1.9", "0.9.5b" > "0.9.5a", etc.
// NormalizeVersion, CompareVersions now live in version.cpp. See version.h.

// HttpResult, ParseUrl, HttpGet, HttpDownloadFile now live in http.cpp.
// Utf8ToWide moved to core.cpp. See http.h / core.h.


//
// GitHub Releases API response: parse `tag_name`, `body`, `published_at`,
// first .zip in `assets`, `html_url`.
// Generic manifest: parse `latest_version`, `changelog`, `download_url`,
// `source_url`, `release_date`, optional `sha256`.

// (extracted to module — was mod_updates block (Parse* + Fetch* + UpdateFetchWorker + KickUpdateChecks + GetUpdateInfo))

//
// Persisted to <mod>\Launcher Files\launcher_mod_cfg.json. Each mod has
// its own independent set of flags; switching mods loads that mod's saved
// state, switching back restores it.
// (block extracted to module — was mod_config block)

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

// (block extracted to module — was ModInfoFromJsonPath + FindMods)

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
std::vector<std::wstring> g_availableFonts;
static std::vector<std::wstring> g_availableFamilies;
static std::vector<INT>          g_availableStyles;
std::vector<std::wstring> g_availableAbbrevs;

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
// (extracted to module — was ColorPreset + g_colorPresets)

// ─────────────────────────────────────────────────────────────────────
// UI scale presets for the toolbar Scale cycling button. The percentage
// label is what the on-screen button shows; the multiplier is what gets
// stored in LauncherCfg::uiScale. Final g_scale = multiplier * g_dpiScale.
// The active preset SET is DPI-dependent (see ActiveScalePresets below):
// at 150% Windows scaling only the smaller three make sense (anything
// above 100% would push the launcher past most monitors); at 100% the
// larger three give the user room to scale up.
// (extracted to module — was ScalePreset + g_scalePresets)

// Return the indices into g_scalePresets[] that are active under the
// current g_dpiScale. The boundary is 1.25 — anything at-or-above
// returns the {75/85/100} subset (typical "150%" Windows scaling),
// anything below returns the {100/115/127} subset (typical "100%"
// scaling on a high-pixel-density display).
// (extracted to module — was ActiveScalePresets)

// Return the slider state (0/1/2) for the current cfg.uiScale. Used
// both to pick which btn_toggle*.png to render and as the starting
// index for the cycle-on-click action.
// (extracted to module — was ScaleToggleState)

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
int OnLaunchSliderState() {
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
const wchar_t* OnLaunchStateLabel() {
    switch (g_cfg.launchBehavior) {
        case LB_MINIMIZE: return L"Min";
        case LB_CLOSE:    return L"Close";
        case LB_STAY:     return L"Stay";
    }
    return L"Min";
}

// (extracted to module — was TryLoadFont + LoadFonts)

// (extracted to module — was UnloadFonts)

// (extracted to module — was MakeFamily)

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
// (extracted to module — was ApplyColorChange)

// ═══════════════════════════════════════════════════════════════════════
//  GDI+ HELPERS
// ═══════════════════════════════════════════════════════════════════════

// Double-buffered paint into a memory DC, blit to dst on dtor.
// (MemDC extracted to scaling.h — paint primitive used by multiple TUs)

// (extracted to module — was FillSolid)

// (extracted to module — was DrawGoldText)

// ═══════════════════════════════════════════════════════════════════════
//  MOD FOLDER WATCHER
// ═══════════════════════════════════════════════════════════════════════
//
//  Watches <D2R>\mods\ for changes and posts MSG_MODS_DIRTY to the main
//  window when anything moves. The main window doesn't auto-rescan — it
//  just lights up the "Refresh Mod List" button to let the user know
//  there's new state to pick up. This keeps the UI from flickering
//  every time a mod is installed/extracted.

// (block extracted to module — was mod_scan watcher block)




// ═══════════════════════════════════════════════════════════════════════
//  LAUNCH FLAG DEFINITIONS
// ═══════════════════════════════════════════════════════════════════════
//
//  The six launch flags live in g_modSettings (per-mod). FlagDef binds
//  each to a member pointer plus its CLI arg and display label. Locked
//  flags are forced true after every config load and ignore clicks.

// (block extracted to module — was launch_flags FLAGS + EnforceLockedFlags)

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
// g_seedNames, g_seedValues, g_recentSeeds, g_lastSeedIdx now live in
// seeds.cpp. See seeds.h.

// Seeds I/O (SeedsJsonPath, ParseSeedArray, LoadSeedsJson, SaveSeedsJson,
// CommitTypedSeedToRecents, FindSeedIndexForValue) now lives in seeds.cpp.
// See seeds.h.

// (extracted to module — was DrawFlagCheckbox)


// ═══════════════════════════════════════════════════════════════════════
//  TOOL RESOLUTION HELPERS
// ═══════════════════════════════════════════════════════════════════════
//
//  Logic: when the user clicks a tool button, we look up the cached path
//  in g_cfg. If empty or invalid, we recursively scan the user's tools
//  directory for a matching exe (or .lnk shortcut). If still not found,
//  we offer a file picker so the user can locate it manually. Resolved
//  paths get cached back into g_cfg so subsequent clicks are instant.

// (block extracted to module — was tool_resolver block)


// ═══════════════════════════════════════════════════════════════════════
//  CMD PREVIEW BUILDER
// ═══════════════════════════════════════════════════════════════════════
//
//  Returns the launch command string from current g_modSettings, formatted
//  for display ("-mod RoK -ns -w"). Used both by the live preview in the
//  footer and as the actual args passed to D2RLoader.exe.

// (block extracted to module — was BuildLaunchArgs)


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

// RepositionForExpansion forward decl removed — layout.h provides it now
// (extracted to layout.cpp — was ComputeBodyLayout)

// (extracted to layout.cpp — was ComputeLeftPanelGeom)

// (extracted to layout.cpp — was BodyFlagRect)


// Seed row layout — same horizontal pattern as the flag rows so the
// checkbox lines up with the flag-grid checkboxes vertically:
//   [checkbox 27x28] gap 8 [label "Seed"]      [text input ~136x26] [arrow 24x26]
// loSeedY / loSeedH define the row band; the helpers below return the
// individual clickable rects. The text input + arrow read visually as
// one chrome (single text_box.png stretched across both), but they have
// separate hit-tests: input area = focus + start typing, arrow area =
// open the seeds dropdown.
// (SEED_* constants extracted to layout.h)
// (extracted to layout.cpp — was BodySeedCheckRect)

// (extracted to layout.cpp — was BodySeedComboRect)

// (extracted to layout.cpp — was BodySeedInputRect)

// (extracted to layout.cpp — was BodySeedArrowRect)


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
bool g_seedInputFocused = false;
int  g_seedCaretPos     = 0;     // caret index within g_modSettings.seedArg
static int  g_seedSelStart     = -1;    // selection anchor; -1 = no selection
bool g_seedCaretVisible = true;  // blink toggle
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
int SeedSelLo() {
    if (g_seedSelStart < 0) return g_seedCaretPos;
    return min(g_seedSelStart, g_seedCaretPos);
}
int SeedSelHi() {
    if (g_seedSelStart < 0) return g_seedCaretPos;
    return max(g_seedSelStart, g_seedCaretPos);
}
bool SeedHasSelection() {
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
// (TB_BTN_* constants extracted to layout.h)

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
// (extracted to layout.cpp — was RefreshModDescriptionLinks)

// (extracted to layout.cpp — was Layout)

// (extracted to layout.cpp — was RepositionForExpansion)

// (extracted to layout.cpp — was RefreshMods)


// Shared themed button frame — gold border, dark bg, optional highlight.
// Used by the refresh button (and by the bottom panel buttons in commit 6).
// (extracted to module — was OPDrawBtnFrame)

// (extracted to module — was mod list block)

// ═══════════════════════════════════════════════════════════════════════
//  BODY GEOMETRY + PAINT
// ═══════════════════════════════════════════════════════════════════════
//
//  Right column = MOD DESCRIPTION (top half) + LAUNCH OPTIONS (bottom half).
//  Painted as part of MainProc::WM_PAINT. The flag checkboxes are hit-tested
//  in WM_LBUTTONDOWN against geometry derived from BodyLayout, which is
//  declared earlier (above Layout()) so Layout() can use it to position
//  the right column's Win32 button children.

// Loader Options rows (left rail). One click-target row remains (Stash
// Tabs); the former DMG Display row was replaced by the Plugins button,
// which is a real HWND and doesn't need a paint-side hit rect. Returned
// in client-space.
// LoaderOptHits struct moved to ui_state.h

LoaderOptHits ComputeLoaderOptRects() {
    LoaderOptHits L = {};
    L.stash = g_stashDropdownRect;
    return L;
}
// (extracted to paint_main.cpp — was PaintBody)


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

// (extracted to module — was Zip queue state globals)
// (extracted to module — was progress dialog state globals)


// Strip filesystem-reserved characters from a mod name. Spaces and dots
// are allowed mid-string but trimmed off the trailing edge (Windows
// rejects directory names ending in either).
// (extracted to module — was SanitizeModName)

// (extracted to module — was fs_utils existence checks + tar extract)

// BFS for the shallowest modinfo.json in a tree. Picking shallowest
// handles archives that nest the mod folder one level deep (a very
// common layout — the zip contains MyMod/modinfo.json, MyMod/data/...).
// (extracted to module — was FindModinfoJson + ReadModNameFromInfo)

// Recursive tree copy with two modes:
//   addMissing=true   → standard recursive copy (mkdir as needed, copy
//                       every file). Used by Overwrite (after wipe) and
//                       by fresh-install.
//   addMissing=false  → "Update" mode: only copy files whose same
//                       relative path EXISTS in dst. Doesn't create new
//                       subdirectories; doesn't add new files. The strict
//                       reading of "Update only overwrites files found in
//                       the archive that are also found in the folder".
// (extracted to module — was CopyTreeInto + CopyTreeExcept)

// Keep the most recent `keep` timestamped subfolders in `backupsRoot`;
// delete the rest. Names follow the YYYY-MM-DD_HHMMSS pattern which
// sorts correctly as plain strings, so a lexicographic sort puts the
// oldest at the front of the list.
// (extracted to module — was save_backup block)

// Forward decls — defined after MainProc so they can directly call into
// the existing WM_DRAWITEM handler for the dialogs' owner-drawn buttons.
// MkStdBtn lives even further down (CONTROL CREATION section), so we
// forward-declare it too — the dialogs create their buttons through it.
// Defaults stay on the definition only (C++ only allows defaults to be
// specified once per signature); dialog call sites pass all 9 args.
// (extracted to module — was MkStdBtn forward decl)
// (extracted to module — was dialog forward decls)

// (extracted to module — was BaseName + PushProgress)

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

// (extracted to module — was ShowLauncherUpdateDialog forward decl)
// Kicks off the install: shows the progress dialog at stage 0 and
// spawns LauncherUpdateInstallWorker on a background thread. Returns
// immediately so the UI stays responsive while the install runs.
// StartLauncherUpdateInstall is now in launcher_self_update.h.

// Scan the GitHub API response body for a release asset URL that
// looks like a release zip. Strategy: walk every
// "browser_download_url" field in the JSON; first one whose value
// ends in .zip wins. If no .zip is found, fall back to the first
// URL we saw. Crude but robust enough for a single-asset release
// flow — we're not parsing the array structure, just substring
// matching, which keeps us free of nested-object parsing.
// (extracted to module — was FindReleaseZipUrl)

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
// (extracted to module — was LauncherUpdateCheckWorker + KickoffLauncherUpdateCheck)

// Tries once to delete Angiris.exe.old (cheap no-op when there isn't one).
// If the file exists but can't be deleted yet — typically because the
// prior launcher process is still shutting down and Windows still has
// its image file mapped — populates g_pendingOldExeDelete so the main
// window's WM_TIMER handler can keep retrying every 2s for ~30s total.
//
// Called once at wWinMain entry. The deferred retry is then armed by
// StartDeferredOldExeCleanup() once g_hwMain exists.
// (extracted to module — was CleanupLauncherOldExe + StartDeferredOldExeCleanup)

// Writes a one-line entry to assets\last_update_install.log explaining
// why the deferred cleanup gave up. Called only when MoveFileEx is the
// last resort.
// (extracted to module — was LogCleanupOldExeGaveUp)

// BFS for the shallowest Angiris.exe in the extracted release. Mirrors
// FindModinfoJson — release zips may wrap the payload in a
// "Angiris-vX.Y" folder, so we don't assume a flat layout.
// (extracted to module — was FindAngirisExeInTree)

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

// (extracted to module — was Launcher update popup state)

// (extracted to module — was LauncherUpdatePopupProc)

// (extracted to module — was ShowLauncherUpdatePopup)

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
// (extracted to module — was CopyTreeIntoLogged)

// (extracted to module — was LauncherUpdateInstallWorker + StartLauncherUpdateInstall)

// Process a single zip end-to-end. Runs on the worker thread. Synchronously
// calls SendMessage to show the conflict / set-path / no-modinfo dialogs
// (which run on the UI thread); the worker blocks until the user picks.
// (extracted to module — was ProcessOneZip + ZipInstallWorker + EnqueueZipsForInstall)

// Show the D2R folder picker (same SHBrowseForFolder used for the
// Loader Directory "..." button). Returns true if the user picked a
// folder and the cfg was saved. Used both by the rail button and by
// the zip installer's "Set Path" dialog when a drop happens before
// the D2R path is configured.
// Exposed (non-static) so dialogs.cpp's SetPath dialog can re-use the
// same picker logic instead of duplicating the SHBrowseForFolder call.
// Declared in dialogs.cpp via a local forward decl.
bool PromptForD2RPath(HWND parent) {
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

        // ── Owner-drawn buttons (delegated) ─────────────────────────────
        // Button paint moved to buttons.cpp. PaintOwnerDrawButton returns
        // true if the message hit a registered owner-draw button. Dialogs
        // call the same helper from their WM_DRAWITEM handlers so visuals
        // stay consistent between main window and dialog buttons.
        if (PaintOwnerDrawButton(d)) return TRUE;
        return DefWindowProc(hw, msg, wp, lp);
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

        // ── Plugins button → plugin manager popup ─────────────────────────
        // Opens the manager scoped to the currently selected mod (its
        // ModName.mpq\Plugins\ folder) plus the global plugins folder
        // under D2R. With no mod selected, only globals show.
        else if (id == IDC_LOADER_PLUGINS) {
            const ModInfo* mod = nullptr;
            if (g_selMod >= 0 && g_selMod < (int)g_mods.size()) {
                mod = &g_mods[g_selMod];
            }
            ShowPluginManager(hw, mod, g_cfg.d2rPath);
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
            if (wasDirty) SetButtonDirty(g_hwRefresh, false);
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
            SetButtonDirty(g_hwRefresh, true);
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
        HWND parent = GetProgressDialog() ? GetProgressDialog() : hw;
        if (p) p->choice = ShowConflictDialog(parent, p->modName);
        return 0;
    }

    case MSG_ZIP_NEED_PATH: {
        // Worker says g_cfg.d2rPath is empty and there's a zip waiting.
        // Show the themed "Set Path / Cancel" dialog; if Set Path, the
        // dialog itself runs SHBrowseForFolder and populates g_cfg before
        // returning. Result: 1 = path set (worker continues with zip),
        // 0 = cancel (worker skips zip).
        HWND parent = GetProgressDialog() ? GetProgressDialog() : hw;
        return ShowSetPathDialog(parent);
    }

    case MSG_ZIP_NO_MODINFO: {
        // Worker found a zip with no modinfo.json. Surface the error to
        // the user (modal OK) so they know which archive was rejected
        // and can contact the mod author.
        const wstring* zipName = (const wstring*)lp;
        HWND parent = GetProgressDialog() ? GetProgressDialog() : hw;
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

// (extracted to module — was MainProc MSG_LUPOPUP_STATUS case)

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
        // DMG Display dropdown click handler removed — the slot is now
        // occupied by the Plugins button (g_hwLoaderPlugins), which is
        // a real HWND that delivers WM_COMMAND IDC_LOADER_PLUGINS to us
        // directly (handled in the IDC_* dispatch below).

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

// (extracted to module — was all 6 dialog implementations)

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

// (extracted to module — was HoverTipProc + Show/HideHoverTip impls)

// ═══════════════════════════════════════════════════════════════════════
//  CONTROL CREATION
// ═══════════════════════════════════════════════════════════════════════

// (extracted to module — was MkStdBtn definition)

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

    // Loader Options "Plugins" button — opens the plugin manager popup
    // for the currently selected mod. Sized 254×54 native (matches the
    // btn_nexus_update.png art exactly so the frame draws at 1:1).
    // No overflow pad: ButtonKind::Plugins has no hover-grow, so the
    // HWND doesn't need headroom for an animation.
    g_hwLoaderPlugins = MkStdBtn(hw, L"Plugins", IDC_LOADER_PLUGINS,
                                 0, 0, 254, 54, true, ButtonKind::Plugins);

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
    g_hwList = CreateWindow(MOD_LIST_CLASS, nullptr,
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

    // Read assets\user_layout.json (if present) so layout accessors and
    // the row-height constant pick up overrides for the rest of init.
    // Must happen BEFORE anything that reads LO::ROW_H, nav button
    // visibility, or the modding-expand flag.
    LoadLayoutOverrides();
    LO::ROW_H = LayoutModRowHeight(LO::ROW_H);

    // If the modding-expand toggle is overridden off, also force the
    // panel to start collapsed. The toggle is the ONLY UI way to
    // re-open the panel, so leaving it expanded with the toggle
    // hidden would strand the user with no way to close it again.
    if (!LayoutShowModdingExpand(true)) {
        g_bottomExpanded = false;
    }

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

    LoadFonts(g_availableFonts, g_availableFamilies,
              g_availableStyles, g_availableAbbrevs);
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
