// ═══════════════════════════════════════════════════════════════════════
//  plugin_manager.cpp — see plugin_manager.h for the interface
// ═══════════════════════════════════════════════════════════════════════
//
//  Implementation overview:
//
//    1. ScanPlugins walks the active + Disabled\ folders for the
//       mod's per-mod plugins dir AND the global plugins dir, building
//       a vector<PluginEntry> with each .dll's name, source folder,
//       active-vs-disabled flag, and a mutable user-checked flag.
//
//    2. The popup is a self-registered WS_POPUP window with a child
//       owner-drawn LISTBOX and two themed buttons (Save / Cancel).
//       Title bar carries the close box; ESC + Cancel + close all
//       discard pending changes.
//
//    3. The listbox is subclassed so a click anywhere in a row
//       toggles that row's check state (the row's checkbox is just
//       visual feedback — the click target is the whole row).
//       Space on the focused row also toggles. Keyboard up/down
//       moves the focus per stock listbox behavior.
//
//    4. Save iterates the list and moves any row whose user-checked
//       state differs from its on-disk state between the active
//       folder and Disabled\. The Disabled\ folder is auto-created
//       on first use. Same-name conflicts (a file already exists
//       at the destination) are silently skipped.
//
//  The popup is modal-style: its parent is EnableWindow(FALSE) while
//  the popup is open and re-enabled when the popup closes. The
//  internal message loop runs until the popup HWND is destroyed.
//
// ═══════════════════════════════════════════════════════════════════════

#include "plugin_manager.h"
#include <algorithm>          // std::sort (readme picker)
#include "plugin_config.h"     // v1.3: per-mod manifest mode + sweeps
#include "plugin_drop_ui.h"     // v1.6: HandlePluginManagerDrop (drag-drop)
#include "plugin_install.h"     // v1.6: PeekZipKind, ZipKind (patch bundle routing)
#include "readme_reader.h"      // v1.6: ShowReadmeReader
#include "repo_browser.h"      // v1.6.2: ShowRepoBrowser
#include "plugin_manifest.h"   // v1.3: friendly-name lookup
#include "ui_state.h"        // v1.6.2: g_loaderOpts (extension gates)
#include "core.h"            // g_hInst, g_dpiScale
#include "scaling.h"         // S(), SF()
#include "colors.h"          // Tok::Gold, Tok::crBgPanel, etc.
#include "fonts.h"           // g_fNavSm, g_fBtn, g_fModName
#include "assets.h"          // AssetImage, DrawButton9Slice — v1.3
#include "buttons.h"         // MkStdBtn, PaintOwnerDrawButton, ButtonKind — v1.3
#include "paint_helpers.h"   // DrawFlagCheckbox — v1.3: reuse launch-options checkbox art

// ── Plugin entry record (file-local) ─────────────────────────────────
//
// One per .dll discovered across both folders. activeDir is the
// "enabled" location; disabledDir is the Disabled\ subfolder. isActive
// records the on-disk state at scan time; isChecked is what the user
// wants after they hit Save. The difference between the two is what
// drives file moves.

struct PluginEntry {
    wstring fileName;     // "MyPlugin.dll"
    wstring activeDir;    // <baseFolder>          (no trailing slash)
    wstring disabledDir;  // <baseFolder>\Disabled (no trailing slash)
    bool    isGlobal;     // true → (G); false → (M)
    bool    isActive;     // on-disk state at scan time (true = in activeDir)
    bool    isChecked;    // user's pending choice (true = want active)
};

// ── PMRow: visual listbox-row abstraction (v1.3, Phase D) ───────────
//
// The listbox displays a sequence of PMRows. Most rows are Plugin
// (toggleable, indexes into g_pluginList) — legacy behavior. Section
// headers separate the (G) and (M) groups in legacy mode and label
// the single "Mod plugins" group in manifest mode. Manifest rows are
// read-only and may render greyed-out if the underlying file wasn't
// recoverable. EmptyMessage isn't used in the listbox itself — when
// the manifest is empty, the listbox isn't created and the message
// is painted directly on the popup background by WM_PAINT.

struct PMRow {
    enum class Kind {
        SectionHeader,    // non-toggleable banner: "Global plugins" / "Mod plugins"
        Plugin,           // toggleable; pluginIdx → g_pluginList[]
        Manifest,         // read-only manifest entry; may be missing (greyed)
    };
    Kind    kind        = Kind::Plugin;
    int     pluginIdx   = -1;        // valid when kind == Plugin
    wstring text;                    // header label / manifest filename
    bool    isMissing   = false;     // manifest entry not found anywhere
};

// ── Window / state (file-local) ──────────────────────────────────────

static std::vector<PluginEntry> g_pluginList;
static std::vector<PMRow>       g_pmRows;           // v1.3: visual rows in the listbox
static HWND    g_pmHwnd      = nullptr;
// v1.6.2: the single list became TWO independently-scrolling lists so
// the "Mod plugins" / "Global plugins" headers can stay pinned instead
// of scrolling away as list rows. g_pmRows still holds every row in one
// vector (so pluginIdx, rename and toggle paths are unchanged); the
// lists just view different halves of it:
//     mod rows    = [0, g_pmSplit)
//     global rows = [g_pmSplit, g_pmRows.size())
// Headers are no longer rows — they're painted by WM_PAINT.
static HWND    g_pmListMod    = nullptr;
static HWND    g_pmListGlobal = nullptr;
static int     g_pmSplit      = 0;
static bool    g_pmHasMod     = false;
static bool    g_pmHasGlobal  = false;
static HWND    g_pmSaveBtn   = nullptr;
static HWND    g_pmCancelBtn = nullptr;
static HWND    g_pmReadmesBtn = nullptr;   // v1.6: opens the readme picker
static wstring g_pmModName;    // display name of the selected mod, or empty
static wstring g_pmModFolder;  // folder name of the selected mod (drag-drop scope)
// The mod + d2rPath the manager was opened with, retained so a drag-drop
// install can rescan and refresh the list in place (v1.6). g_pmSelectedMod
// may be null (no mod selected / globals view).
static const ModInfo* g_pmSelectedMod = nullptr;
static wstring        g_pmD2rPath;
static bool    g_pmClassReg   = false;

// v1.3 manifest-mode state. g_pmConfigMode is true whenever the
// active mod had a parseable manifest (whether or not the manifest
// declared any plugins). g_pmConfigEmpty narrows that to the
// "manifest exists but lists zero plugins" sub-case — that's when
// the listbox is suppressed and the popup paints a centered author
// message instead.
static bool    g_pmConfigMode  = false;
static bool    g_pmConfigEmpty = false;
static wstring g_pmEmptyMessage;   // pre-rendered author / modname phrase

// Layout constants in LOGICAL pixels — physical sizing happens once
// at popup creation via S() / g_dpiScale.
constexpr int PM_W            = 480;
constexpr int PM_H            = 480;
constexpr int PM_TITLE_H      = 56;   // two-line: "Plugin Manager" + mod name
constexpr int PM_PAD          = 12;
constexpr int PM_BTN_W        = 140;
constexpr int PM_BTN_H        = 58;    // bottom-anchored — grows upward
constexpr int PM_BTN_GAP      = 12;
constexpr int PM_LIST_PAD_TOP = 8;
constexpr int PM_LIST_PAD_BOT = 8;
constexpr int PM_ROW_H        = 28;
constexpr int PM_CHECKBOX_SIZE = 16;

// ── Rename modal (Phase E2) ───────────────────────────────────────────
//
// Small themed popup that asks the user for a new friendly name. Shown
// when the user right-clicks a plugin row and chooses Rename...
// Layout is intentionally cramped — single line of input, two buttons,
// title strip naming the DLL — to keep the modal lightweight.

constexpr int RM_W         = 420;   // total popup width  (logical)
constexpr int RM_H         = 168;   // total popup height (logical)
constexpr int RM_TITLE_H   = 40;
constexpr int RM_PAD       = 16;
constexpr int RM_EDIT_H    = 30;
constexpr int RM_LABEL_H   = 18;    // small descriptor row between title and edit
constexpr int RM_BTN_W     = 110;
constexpr int RM_BTN_H     = 42;    // bottom-anchored — grows upward
constexpr int RM_BTN_GAP   = 12;

// Rename modal state. Like the plugin manager popup, these are all
// file-static — only one rename modal can be active at a time and it
// always nests inside the plugin manager's modal pump.
static HWND    g_rmHwnd      = nullptr;
static HWND    g_rmInput     = nullptr;  // visible EDIT inside the box — real editing surface
static HWND    g_rmOkBtn     = nullptr;
static HWND    g_rmCancelBtn = nullptr;
static wstring g_rmDllName;              // DLL being renamed (display only)
static wstring g_rmText;                 // current input text (mirrored from g_rmInput)
static HFONT   g_rmEditFont  = nullptr;  // font for the visible rename EDIT
static wstring g_rmResult;               // captured friendly name on OK
static bool    g_rmAccepted  = false;    // OK pressed (true) vs Cancel/Esc (false)
static bool    g_rmClassReg  = false;

// Forward decl — definition is after ShowPluginManager since it shares
// the PMDrawButton helper. Returns true if the user accepted; outNewName
// holds the entered text (possibly empty — empty means "clear friendly
// name"). Returns false if cancelled/Esc'd, in which case outNewName
// is untouched.
static bool ShowRenameModal(HWND parent,
                            const wstring& dllName,
                            const wstring& currentFriendly,
                            wstring& outNewName);

// v1.6: open a picker listing every plugin that has a readme; the chosen
// one opens in the themed reader. Defined after PluginManagerProc.
static void ShowReadmePicker(HWND pmHwnd);

// ── Filesystem helpers ───────────────────────────────────────────────

// Best-effort CreateDirectory; ignores ERROR_ALREADY_EXISTS. Used so
// callers don't have to check the error code in normal flow.
static void EnsureDirExists(const wstring& dir) {
    CreateDirectoryW(dir.c_str(), nullptr);
}

// Scan a single active/Disabled folder pair for files matching `pattern`
// (e.g. L"*.dll" for plugins, L"*.json" for patches) and append entries
// to g_pluginList. Creates the Disabled subfolder if it doesn't exist
// so the user has a consistent place to look on disk after launching
// the manager.
static void ScanPluginFolder(const wstring& baseDir, bool isGlobal,
                             const wchar_t* pattern) {
    // Skip entirely if the base folder doesn't exist (e.g. mod has no
    // d2rloader\plugins\ or d2rloader\patches\ folder).
    DWORD attr = GetFileAttributesW(baseDir.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) return;

    wstring disabledDir = baseDir + L"\\Disabled";
    EnsureDirExists(disabledDir);

    auto scanOne = [&](const wstring& dir, bool active) {
        WIN32_FIND_DATAW fd;
        wstring searchPattern = dir + L"\\" + pattern;
        HANDLE h = FindFirstFileW(searchPattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            PluginEntry e;
            e.fileName    = fd.cFileName;
            e.activeDir   = baseDir;
            e.disabledDir = disabledDir;
            e.isGlobal    = isGlobal;
            e.isActive    = active;
            e.isChecked   = active;          // initial state mirrors disk
            g_pluginList.push_back(e);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    };

    scanOne(baseDir,     true);    // active
    scanOne(disabledDir, false);   // disabled
}

// Populate g_pluginList for (mod, d2rPath). Mod-local entries come
// first (they're more immediately relevant to the user's current
// session); global entries follow. Each row's fileName carries its
// extension (.dll = plugin, .json = patch), so downstream code that
// needs to distinguish just looks at the extension.
//
// D2RLoader beta 1.0 lives in <base>\d2rloader\ instead of the flat
// <base>\plugins\, and adds a sibling <base>\d2rloader\patches\ for
// JSON memory patches. Plugins and patches are treated uniformly by
// the launcher — same enable/disable/rename plumbing for both.
static void ScanPlugins(const ModInfo* mod, const wstring& d2rPath) {
    g_pluginList.clear();

    // Mod-local plugins + patches
    if (mod) {
        wstring modBase = mod->dir + L"\\d2rloader";
        ScanPluginFolder(modBase + L"\\plugins", /*isGlobal=*/false, L"*.dll");
        ScanPluginFolder(modBase + L"\\patches", /*isGlobal=*/false, L"*.json");
    }

    // Global plugins + patches
    wstring globalBase = d2rPath + L"\\d2rloader";
    ScanPluginFolder(globalBase + L"\\plugins", /*isGlobal=*/true, L"*.dll");
    ScanPluginFolder(globalBase + L"\\patches", /*isGlobal=*/true, L"*.json");
}

// Walk the user's checkbox decisions and commit them to disk. Each
// row whose isChecked != isActive moves between activeDir and
// disabledDir. Conflicts (target file already exists with the same
// name) are silently skipped — without ABA-style merging there's no
// safe automatic resolution.
static void ApplyChanges() {
    for (const auto& e : g_pluginList) {
        if (e.isChecked == e.isActive) continue;
        wstring fromDir = e.isActive  ? e.activeDir  : e.disabledDir;
        wstring toDir   = e.isChecked ? e.activeDir  : e.disabledDir;
        wstring fromPath = fromDir + L"\\" + e.fileName;
        wstring toPath   = toDir   + L"\\" + e.fileName;

        // Skip if destination already exists — leaves the source in
        // place. The user can resolve manually by renaming.
        if (GetFileAttributesW(toPath.c_str()) != INVALID_FILE_ATTRIBUTES)
            continue;

        EnsureDirExists(toDir);
        MoveFileExW(fromPath.c_str(), toPath.c_str(),
                    MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH);
    }
}

// ── Row-builder helpers (v1.3, Phase D) ──────────────────────────────
//
// Translate the scanned plugin set (legacy) or the parsed manifest
// (manifest mode) into a flat vector<PMRow> with section headers
// interleaved. The listbox indexes into g_pmRows; PMDrawItem dispatches
// on each row's Kind.

// Legacy mode: g_pluginList has (M) entries first (mod plugins) then
// (G) entries (globals). Insert a header before each non-empty group;
// drop the trailing "(G)"/"(M)" tag from row labels — headers now
// carry the source-group meaning.
static void BuildLegacyRows() {
    g_pmRows.clear();

    // Find where (M) ends and (G) begins. ScanPlugins always orders
    // mod-first then global, so a simple sweep suffices.
    size_t firstGlobal = g_pluginList.size();
    for (size_t i = 0; i < g_pluginList.size(); ++i) {
        if (g_pluginList[i].isGlobal) { firstGlobal = i; break; }
    }
    g_pmHasMod    = firstGlobal > 0;
    g_pmHasGlobal = firstGlobal < g_pluginList.size();

    // No SectionHeader rows any more — the two headers are painted as
    // static chrome above their lists, so they can't scroll away.
    for (size_t i = 0; i < firstGlobal; ++i) {
        PMRow r;
        r.kind      = PMRow::Kind::Plugin;
        r.pluginIdx = (int)i;
        r.text      = g_pluginList[i].fileName;
        g_pmRows.push_back(r);
    }
    g_pmSplit = (int)g_pmRows.size();
    for (size_t i = firstGlobal; i < g_pluginList.size(); ++i) {
        PMRow r;
        r.kind      = PMRow::Kind::Plugin;
        r.pluginIdx = (int)i;
        r.text      = g_pluginList[i].fileName;
        g_pmRows.push_back(r);
    }
}

// Manifest mode: g_pluginList stays empty (no toggleable state). Build
// one Manifest row per manifest entry, marking entries whose
// corresponding `found` flag is false so PMDrawItem can grey them out.
// Manifest mode is always mod-scoped, so everything lands in the mod
// list and the global list stays absent.
static void BuildConfigRows(const PluginConfig& mf,
                              const vector<bool>&   found) {
    g_pmRows.clear();
    g_pmSplit     = 0;
    g_pmHasMod    = false;
    g_pmHasGlobal = false;
    if (mf.plugins.empty()) return;   // empty manifest → no rows; WM_PAINT handles the message

    g_pmHasMod = true;

    for (size_t i = 0; i < mf.plugins.size(); ++i) {
        PMRow r;
        r.kind      = PMRow::Kind::Manifest;
        r.text      = mf.plugins[i];
        r.isMissing = (i < found.size()) ? !found[i] : true;
        g_pmRows.push_back(r);
    }
    // Every manifest row is mod-scoped: split sits at the end so the
    // global list resolves to an empty range.
    g_pmSplit = (int)g_pmRows.size();
}

// ── Two-list geometry + row-range helpers (v1.6.2) ───────────────────

constexpr int PM_SECTION_H   = 22;   // static header band above each list
constexpr int PM_SECTION_GAP = 10;   // gap between the two sections
constexpr int PM_IDC_LIST_MOD    = 100;   // was the single list's ID
constexpr int PM_IDC_LIST_GLOBAL = 101;

// ── Themed scrollbars (v1.6.2) ───────────────────────────────────────
// The lists dropped WS_VSCROLL — the stock bar is a bright system
// control that reads as a hole in the stone. Instead each list gets a
// painted gutter beside it, drawn by the popup's WM_PAINT from the same
// scroll_up / scroll_down / scrollbar_track / scroll.png assets the mod
// list uses. All dimensions are NATIVE asset pixels, never scaled by
// g_dpiScale: the art is blitted at native size, so scaling the gutter
// would push the arrow caps out of line with the track.
constexpr int PM_SB_W         = 30;   // gutter width (asset native)
constexpr int PM_SB_GAP       = 4;    // gap between the list and its gutter
constexpr int PM_SB_THUMB_W   = 15;
constexpr int PM_SB_MIN_THUMB = 40;
constexpr int PM_SB_THUMB_CAP = 16;
constexpr int PM_SB_UP_H_FB   = 35;
constexpr int PM_SB_DOWN_H_FB = 32;

// Where each header and list sits, in physical client coordinates.
// WM_PAINT and the creation path both call this so the painted headers
// and the real child windows can't drift apart.
struct PMListGeom {
    bool hasMod = false, hasGlobal = false;
    RECT modHdr = {}, modList = {}, globHdr = {}, globList = {};
    // Painted gutters. Only valid when the matching needsSb flag is
    // set; when a list fits entirely, no gutter is drawn and the list
    // keeps the full width.
    bool modNeedsSb = false, globNeedsSb = false;
    RECT modBar = {}, globBar = {};
};

static PMListGeom PMComputeListGeom(int physW, int physH) {
    PMListGeom gm;
    gm.hasMod    = g_pmHasMod;
    gm.hasGlobal = g_pmHasGlobal;

    int listX = (int)(PM_PAD * g_dpiScale);
    int listW = physW - 2 * listX;
    int top   = (int)((PM_TITLE_H + PM_LIST_PAD_TOP) * g_dpiScale);
    int bot   = physH - (int)((PM_PAD + PM_BTN_H + PM_LIST_PAD_BOT) * g_dpiScale);
    int avail = bot - top;
    if (avail < 0) avail = 0;

    int hdrH = (int)(PM_SECTION_H * g_dpiScale);
    int gap  = (int)(PM_SECTION_GAP * g_dpiScale);
    int rowH = (int)(PM_ROW_H * g_dpiScale);

    int modCount  = g_pmSplit;
    int globCount = (int)g_pmRows.size() - g_pmSplit;

    if (gm.hasMod && gm.hasGlobal) {
        // Split the leftover space in proportion to how many rows each
        // side actually has, so a 20-plugin mod list doesn't get the
        // same height as a 2-entry global list. Each side keeps room
        // for at least two rows so neither collapses to a sliver.
        int space = avail - 2 * hdrH - gap;
        if (space < 0) space = 0;
        int minH  = rowH * 2;
        int total = modCount + globCount;
        int modH  = (total > 0) ? space * modCount / total : space / 2;
        if (modH < minH)          modH = minH;
        if (modH > space - minH)  modH = space - minH;
        if (modH < 0)             modH = 0;
        int globH = space - modH;
        if (globH < 0) globH = 0;

        int y = top;
        gm.modHdr  = { listX, y, listX + listW, y + hdrH };  y += hdrH;
        gm.modList = { listX, y, listX + listW, y + modH };  y += modH + gap;
        gm.globHdr = { listX, y, listX + listW, y + hdrH };  y += hdrH;
        gm.globList= { listX, y, listX + listW, y + globH };
    } else if (gm.hasMod || gm.hasGlobal) {
        // Only one section — it takes the whole band.
        int y = top;
        RECT hdr  = { listX, y, listX + listW, y + hdrH };
        RECT list = { listX, y + hdrH, listX + listW, bot };
        if (gm.hasMod) { gm.modHdr = hdr; gm.modList = list; }
        else           { gm.globHdr = hdr; gm.globList = list; }
    }

    // Decide which lists overflow, and carve their gutter out of the
    // right edge of the list rect. Done last so it applies to both the
    // split and single-section layouts above.
    auto reserve = [&](RECT& listRc, RECT& barRc, bool& needs, int count) {
        needs = false;
        if (count <= 0) return;
        int h = listRc.bottom - listRc.top;
        if (h <= 0 || rowH <= 0) return;
        int visible = h / rowH;
        if (count <= visible) return;      // fits — no gutter
        needs = true;
        barRc = { listRc.right - PM_SB_W, listRc.top,
                  listRc.right, listRc.bottom };
        listRc.right -= (PM_SB_W + PM_SB_GAP);
        if (listRc.right < listRc.left) listRc.right = listRc.left;
    };
    if (gm.hasMod)
        reserve(gm.modList, gm.modBar, gm.modNeedsSb, modCount);
    if (gm.hasGlobal)
        reserve(gm.globList, gm.globBar, gm.globNeedsSb, globCount);

    return gm;
}

// ── Themed scrollbar for a listbox ───────────────────────────────────
// The listbox scrolls by ITEM (LB_SETTOPINDEX), so the bar works in row
// units rather than pixels: topIndex ranges 0..(count - visibleRows).

struct PMSbGeom {
    bool present = false;
    RECT area = {}, up = {}, down = {}, track = {}, thumb = {};
    int  trackTop = 0, trackH = 0;
    int  visible = 0, maxTop = 0, topIndex = 0;
};

static PMSbGeom PMScrollbarGeom(HWND lb, const RECT& bar, int count) {
    PMSbGeom s;
    if (!lb || count <= 0) return s;
    int rowH = (int)(PM_ROW_H * g_dpiScale);
    int h = bar.bottom - bar.top;
    if (h <= 0 || rowH <= 0) return s;

    s.visible = h / rowH;
    if (s.visible < 1) s.visible = 1;
    s.maxTop = count - s.visible;
    if (s.maxTop < 0) s.maxTop = 0;
    s.topIndex = (int)SendMessage(lb, LB_GETTOPINDEX, 0, 0);
    if (s.topIndex < 0) s.topIndex = 0;
    if (s.topIndex > s.maxTop) s.topIndex = s.maxTop;

    int upH = PM_SB_UP_H_FB, downH = PM_SB_DOWN_H_FB;
    if (Gdiplus::Bitmap* a = AssetImage(L"scroll_up.png"))   upH   = (int)a->GetHeight();
    if (Gdiplus::Bitmap* a = AssetImage(L"scroll_down.png")) downH = (int)a->GetHeight();

    s.present = true;
    s.area = bar;
    s.up   = { bar.left, bar.top,           bar.right, bar.top + upH };
    s.down = { bar.left, bar.bottom - downH, bar.right, bar.bottom };

    s.trackTop = (int)bar.top + upH;
    int trackBot = (int)bar.bottom - downH;
    s.trackH = trackBot - s.trackTop;
    if (s.trackH < 0) s.trackH = 0;
    s.track = { bar.left, s.trackTop, bar.right, trackBot };

    int thumbH;
    if (s.maxTop <= 0 || s.trackH <= PM_SB_MIN_THUMB) {
        thumbH = s.trackH;
    } else {
        thumbH = (int)((long long)s.trackH * s.visible / count);
        if (thumbH < PM_SB_MIN_THUMB) thumbH = PM_SB_MIN_THUMB;
        if (thumbH > s.trackH)        thumbH = s.trackH;
    }
    int thumbTop = s.trackTop;
    if (s.maxTop > 0) {
        int travel = s.trackH - thumbH;
        if (travel > 0) {
            thumbTop = s.trackTop
                     + (int)((long long)s.topIndex * travel / s.maxTop);
            if (thumbTop < s.trackTop)          thumbTop = s.trackTop;
            if (thumbTop > s.trackTop + travel) thumbTop = s.trackTop + travel;
        }
    }
    int thumbX = (int)bar.left + (PM_SB_W - PM_SB_THUMB_W) / 2;
    s.thumb = { thumbX, thumbTop, thumbX + PM_SB_THUMB_W, thumbTop + thumbH };
    return s;
}

// Vertical 3-slice so the grip's finished ends stay sharp.
static void PMDrawThumb(Gdiplus::Graphics& g, Gdiplus::Bitmap* b,
                        int x, int y, int w, int h, int cap) {
    if (!b) return;
    int sw = (int)b->GetWidth(), sh = (int)b->GetHeight();
    if (h >= sh && h > cap * 2 && sh > cap * 2) {
        g.DrawImage(b, Gdiplus::Rect(x, y, w, cap), 0, 0, sw, cap, Gdiplus::UnitPixel);
        g.DrawImage(b, Gdiplus::Rect(x, y + cap, w, h - cap * 2),
                    0, cap, sw, sh - cap * 2, Gdiplus::UnitPixel);
        g.DrawImage(b, Gdiplus::Rect(x, y + h - cap, w, cap),
                    0, sh - cap, sw, cap, Gdiplus::UnitPixel);
    } else {
        g.DrawImage(b, Gdiplus::Rect(x, y, w, h), 0, 0, sw, sh, Gdiplus::UnitPixel);
    }
}

static void PMPaintScrollbar(Gdiplus::Graphics& g, const PMSbGeom& s) {
    if (!s.present) return;
    int aw = (int)(s.area.right - s.area.left);
    int ah = (int)(s.area.bottom - s.area.top);

    if (Gdiplus::Bitmap* tk = AssetImage(L"scrollbar_track.png")) {
        g.DrawImage(tk, Gdiplus::Rect((INT)s.area.left, (INT)s.area.top, (INT)aw, (INT)ah),
                    0, 0, (INT)tk->GetWidth(), (INT)tk->GetHeight(), Gdiplus::UnitPixel);
    } else {
        Gdiplus::SolidBrush groove(Gdiplus::Color(150, 0x10, 0x0A, 0x06));
        g.FillRectangle(&groove, (INT)s.area.left, (INT)s.area.top, (INT)aw, (INT)ah);
    }

    int tw = (int)(s.thumb.right - s.thumb.left);
    int th = (int)(s.thumb.bottom - s.thumb.top);
    if (Gdiplus::Bitmap* tb = AssetImage(L"scroll.png")) {
        PMDrawThumb(g, tb, (INT)s.thumb.left, (INT)s.thumb.top,
                    tw, th, PM_SB_THUMB_CAP);
    } else {
        Gdiplus::SolidBrush grip(Tok::BronzeBright);
        g.FillRectangle(&grip, (INT)s.thumb.left, (INT)s.thumb.top, (INT)tw, (INT)th);
    }

    if (Gdiplus::Bitmap* up = AssetImage(L"scroll_up.png"))
        g.DrawImage(up, (INT)s.up.left, (INT)s.up.top,
                    (INT)up->GetWidth(), (INT)up->GetHeight());
    if (Gdiplus::Bitmap* dn = AssetImage(L"scroll_down.png"))
        g.DrawImage(dn, (INT)s.down.left, (INT)s.down.top,
                    (INT)dn->GetWidth(), (INT)dn->GetHeight());
}

// Repaint ONLY a list's scrollbar gutter on the popup.
//
// The gutter is painted by the popup rather than the listbox, so moving
// the thumb needs a parent repaint — but invalidating the whole parent
// repaints every owner-drawn button too, which is exactly the
// full-window-invalidate flicker this codebase avoids everywhere else.
// Scoping it to the bar rect keeps the buttons untouched.
static void PMInvalidateBar(HWND lb) {
    if (!g_pmHwnd || !lb) return;
    RECT rc; GetClientRect(g_pmHwnd, &rc);
    PMListGeom gm = PMComputeListGeom(rc.right, rc.bottom);
    bool isGlobal = (lb == g_pmListGlobal);
    bool needs = isGlobal ? gm.globNeedsSb : gm.modNeedsSb;
    if (!needs) return;
    RECT bar = isGlobal ? gm.globBar : gm.modBar;
    InvalidateRect(g_pmHwnd, &bar, FALSE);
}

// Thumb-drag state. Only one bar can be dragged at a time.
static HWND g_pmSbDragList = nullptr;
static int  g_pmSbGrabDY   = 0;

// ── Extension gates (D2RLoader 1.1.0) ────────────────────────────────
// allow_global_extensions / allow_mod_extensions decide whether D2RLoader
// loads plugins at all. With one off, everything in that section is
// inert in game no matter what the checkboxes here say — so installing
// through this manager or the Repository Browser appears to succeed and
// then silently does nothing. Surface it on the section itself, since
// the two toml keys map exactly onto the two lists.
static bool PMModGateOff()    { return !g_loaderOpts.allowModExtensions; }
static bool PMGlobalGateOff() { return !g_loaderOpts.allowGlobalExtensions; }

// True when the rows in this listbox belong to a section whose gate is
// off — PMDrawItem dims them so a disabled section reads as inert.
static bool PMListGateOff(HWND lb) {
    return (lb == g_pmListGlobal) ? PMGlobalGateOff() : PMModGateOff();
}

// A listbox's window handle tells us which half of g_pmRows it shows.// Every row lookup goes through these so the two lists stay honest
// about which underlying rows they own.
static int PMBaseFor(HWND lb) {
    return (lb == g_pmListGlobal) ? g_pmSplit : 0;
}
static int PMCountFor(HWND lb) {
    return (lb == g_pmListGlobal)
           ? (int)g_pmRows.size() - g_pmSplit
           : g_pmSplit;
}

// Format the empty-manifest message. If both author and mod name are
// available, attribute the choice; otherwise fall back to a generic
// phrasing. Called once from ShowPluginManager when manifest is empty.
static wstring FormatEmptyConfigMessage(const ModInfo* mod) {
    if (mod && !mod->author.empty() && !mod->name.empty()) {
        return mod->author + L" has selected to disable plugins for " + mod->name;
    }
    return L"Plugins disabled for this mod";
}

// v1.3: build the displayed text for a plugin row. If the launcher's
// plugin_manifest.json defines a friendly name for this DLL, render
// "Friendly Name (dll)"; otherwise just "dll". Same rule for both
// legacy-mode plugin rows and read-only manifest-mode rows so the
// rename is universal — the same DLL filename always shows the same
// label everywhere.
static wstring BuildDisplayLabel(const wstring& dllName) {
    wstring friendly = GetPluginFriendlyName(dllName);
    if (friendly.empty()) return dllName;
    return friendly + L" (" + dllName + L")";
}


//
// Stock LISTBOX gives us focus + keyboard nav for free; we just need
// to add the toggle gesture. WM_LBUTTONDOWN does the click-to-toggle;
// WM_KEYDOWN VK_SPACE does the keyboard variant. We still pass through
// to DefSubclassProc so the listbox's normal selection-change logic
// runs (the row stays highlighted after a toggle click).

static LRESULT CALLBACK PMListSubclass(HWND hw, UINT msg,
                                       WPARAM wp, LPARAM lp,
                                       UINT_PTR /*id*/, DWORD_PTR /*data*/) {
    auto toggle = [&](int idx) {
        // idx is a listbox item index; shift it into g_pmRows space by
        // whichever half this listbox is showing.
        int base   = PMBaseFor(hw);
        int absIdx = base + idx;
        if (idx < 0 || idx >= PMCountFor(hw)) return;
        if (absIdx < 0 || absIdx >= (int)g_pmRows.size()) return;
        const PMRow& row = g_pmRows[absIdx];
        // Only Plugin-kind rows participate in toggle. Section headers
        // and (read-only) Manifest rows ignore the click.
        if (row.kind != PMRow::Kind::Plugin) return;
        if (row.pluginIdx < 0
            || row.pluginIdx >= (int)g_pluginList.size()) return;
        g_pluginList[row.pluginIdx].isChecked =
            !g_pluginList[row.pluginIdx].isChecked;
        // Explicit invalidate — rows are non-selectable now, so we
        // can't rely on selection-change repaint (there is none).
        RECT r;
        if (SendMessage(hw, LB_GETITEMRECT, idx, (LPARAM)&r) != LB_ERR) {
            InvalidateRect(hw, &r, FALSE);
        }
    };

    switch (msg) {

    // Row backgrounds are sampled from bg_stone by SCREEN position, so
    // the texture is meant to sit still while rows move over it. The
    // listbox's default scroll does the opposite: it bitblts the
    // existing pixels upward (dragging the stone with them) and only
    // repaints the newly exposed row. The two then disagree and the
    // pattern visibly breaks apart. Forcing a full repaint after any
    // scroll costs one extra blit and keeps the stone continuous.
    case WM_VSCROLL:
    case WM_MOUSEWHEEL: {
        if (msg == WM_MOUSEWHEEL) {
            // With WS_VSCROLL removed, don't rely on the listbox's
            // default wheel handling — drive the top index directly so
            // the wheel works whether or not the stock bar would have.
            int count   = PMCountFor(hw);
            RECT lrc; GetClientRect(hw, &lrc);
            int rowH    = (int)(PM_ROW_H * g_dpiScale);
            int visible = (rowH > 0) ? (int)(lrc.bottom / rowH) : 1;
            if (visible < 1) visible = 1;
            int maxTop  = count - visible;
            if (maxTop < 0) maxTop = 0;
            int top = (int)SendMessage(hw, LB_GETTOPINDEX, 0, 0);
            int delta = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
            top -= delta * 3;                 // 3 rows per notch
            if (top < 0)      top = 0;
            if (top > maxTop) top = maxTop;
            SendMessage(hw, LB_SETTOPINDEX, (WPARAM)top, 0);
            InvalidateRect(hw, nullptr, FALSE);
            PMInvalidateBar(hw);
            return 0;
        }
        LRESULT r = DefSubclassProc(hw, msg, wp, lp);
        InvalidateRect(hw, nullptr, FALSE);
        // The gutter is painted by the POPUP, not the listbox, so the
        // thumb won't move unless the parent repaints too — but only
        // the bar rect, never the whole popup.
        PMInvalidateBar(hw);
        return r;
    }

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        // v1.3: rows are effectively non-selectable — only the checkbox
        // band captures clicks. Any click outside that band is silently
        // consumed (returned) so DefSubclassProc doesn't advance the
        // listbox selection or focus. This means no visual selection
        // state can accumulate on rows, which eliminates the whole
        // class of "toggle didn't repaint because selection didn't
        // change" bugs.
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int idx = (int)SendMessage(hw, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
        if (HIWORD(idx) == 0) {
            int row = LOWORD(idx);
            RECT rr;
            if (SendMessage(hw, LB_GETITEMRECT, row, (LPARAM)&rr) != LB_ERR) {
                int cbLeft  = rr.left + S(8);
                int cbRight = cbLeft + S(27) + S(4);   // asset width + 4 pad
                if (pt.x >= rr.left && pt.x < cbRight) {
                    toggle(row);
                }
            }
        }
        // Always consume — never fall through to default listbox
        // click handling that would set selection.
        return 0;
    }
    // v1.3: no keyboard toggle. Rows are non-selectable now (see
    // WM_LBUTTONDOWN above), so there's no "current row" for space
    // bar to act on. Only the checkbox click drives state changes.

    case WM_CONTEXTMENU: {
        // v1.3: right-click → Rename menu. lParam packs screen
        // coordinates. Since rows are non-selectable (v1.3), the
        // keyboard-invoke path (lParam == -1) has no "current row"
        // to anchor on — we simply ignore that case.
        if (lp == (LPARAM)-1) return 0;
        POINT scr = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        POINT cli = scr;
        ScreenToClient(hw, &cli);
        int packed = (int)SendMessage(hw, LB_ITEMFROMPOINT, 0,
                                      MAKELPARAM(cli.x, cli.y));
        int rowIdx = -1;
        if (HIWORD(packed) == 0) rowIdx = LOWORD(packed);
        if (rowIdx < 0 || rowIdx >= PMCountFor(hw)) return 0;
        // Shift into g_pmRows space for whichever list was clicked.
        int absIdx = PMBaseFor(hw) + rowIdx;
        if (absIdx < 0 || absIdx >= (int)g_pmRows.size()) return 0;

        // Section headers can't be renamed — only plugin filename rows
        // (legacy or manifest-mode read-only entries) are valid targets.
        const PMRow& row = g_pmRows[absIdx];
        wstring dllName;
        if (row.kind == PMRow::Kind::Plugin) {
            if (row.pluginIdx >= 0
                && row.pluginIdx < (int)g_pluginList.size()) {
                dllName = g_pluginList[row.pluginIdx].fileName;
            }
        } else if (row.kind == PMRow::Kind::Manifest) {
            // Manifest rows store the DLL filename directly in `text`
            // (display label is built lazily by BuildDisplayLabel).
            dllName = row.text;
        }
        if (dllName.empty()) return 0;

        // Does this plugin have a readme (recorded at install time)? Drives
        // whether the "Open README" item is enabled.
        wstring readmeRel = GetPluginReadmePath(dllName);
        bool hasReadme = !readmeRel.empty();

        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, L"Rename...");
        AppendMenuW(menu, MF_STRING | (hasReadme ? 0 : MF_GRAYED),
                    2, L"Open README");
        int cmd = TrackPopupMenu(menu,
                                 TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                 scr.x, scr.y, 0, hw, nullptr);
        DestroyMenu(menu);

        if (cmd == 2) {
            // Open this plugin's readme in the themed reader. Path is
            // launcher-relative (or absolute for out-of-tree readmes).
            wstring full = readmeRel;
            if (full.size() > 1 && full[1] != L':')       // not absolute
                full = AppDir() + L"\\" + readmeRel;
            ShowReadmeReader(GetParent(hw), GetPluginFriendlyName(dllName), full);
            return 0;
        }
        if (cmd != 1) return 0;

        wstring currentFriendly = GetPluginFriendlyName(dllName);
        wstring newFriendly;
        if (ShowRenameModal(GetParent(hw), dllName, currentFriendly, newFriendly)) {
            SetPluginFriendlyName(dllName, newFriendly);
            SavePluginManifest();
            // Trigger a redraw — PMDrawItem re-resolves the friendly
            // name on every paint, so a single invalidate is enough.
            InvalidateRect(hw, nullptr, FALSE);
        }
        return 0;
    }
    }
    return DefSubclassProc(hw, msg, wp, lp);
}

// ── Owner-draw rendering ─────────────────────────────────────────────

// PMDrawItem (v1.3) dispatches on PMRow.kind. Section headers render
// as a centered gold band with no background highlight or focus rect.
// Plugin rows keep the legacy checkbox + filename look (minus the
// (G)/(M) tag, since headers now carry that meaning). Manifest rows
// render filename only — no checkbox — and grey out the text when
// the underlying file wasn't found.
//
// v1.3 changes: row backgrounds now sample bg_stone.png at the correct
// offset within the popup (so the stone pattern is continuous with the
// stone painted by WM_PAINT — no visible seam at the listbox edge),
// and checkboxes use the checkbox.png / checkbox_checked.png asset
// family via DrawFlagCheckbox, matching the launch-options section.
// v1.6.2: if this section's D2RLoader extension gate is off, mute the
// whole row. The checkbox still reflects the on-disk state and still
// toggles — the file move is real — but nothing in this section loads
// in game until the gate is turned back on, and the row should look
// that way. Applied to both Plugin and Manifest rows.
static void PMMuteIfGated(Gdiplus::Graphics& g, DRAWITEMSTRUCT* di,
                          int rowW, int rowH) {
    if (!PMListGateOff(di->hwndItem)) return;
    Gdiplus::SolidBrush mute(Gdiplus::Color(150, 20, 16, 12));
    g.FillRectangle(&mute, (INT)di->rcItem.left, (INT)di->rcItem.top,
                    (INT)rowW, (INT)rowH);
}

static void PMDrawItem(DRAWITEMSTRUCT* di) {
    // di->itemID is an index within THIS listbox; shift it into
    // g_pmRows space by whichever half the listbox owns.
    int base   = PMBaseFor(di->hwndItem);
    int absIdx = base + (int)di->itemID;
    if ((int)di->itemID < 0
        || (int)di->itemID >= PMCountFor(di->hwndItem)) return;
    if (absIdx < 0 || absIdx >= (int)g_pmRows.size()) return;
    const PMRow& row = g_pmRows[absIdx];

    bool selected = (di->itemState & ODS_SELECTED) != 0;
    bool focused  = (di->itemState & ODS_FOCUS)    != 0;

    // Compute this row's offset within the popup client area so we can
    // sample bg_stone.png at the same coordinates used by the popup's
    // own WM_PAINT (which crops the stone at 40,40 into rect(0,0,W,H)).
    // With two lists at different Y positions the old hardcoded offset
    // no longer works, so ask Windows where this listbox actually is.
    POINT listOrigin = { 0, 0 };
    MapWindowPoints(di->hwndItem, g_pmHwnd, &listOrigin, 1);
    int listX_phys = listOrigin.x;
    int listY_phys = listOrigin.y;
    int rowW = di->rcItem.right  - di->rcItem.left;
    int rowH = di->rcItem.bottom - di->rcItem.top;

    // GDI+ graphics for asset draws — reused throughout the function.
    Gdiplus::Graphics g(di->hDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    // Paint the row background with bg_stone, sampled so the pattern
    // is continuous with the popup. Fallback: solid dark tone that
    // approximates the stone's average value so it doesn't jump out.
    if (Gdiplus::Bitmap* stone = AssetImage(L"bg_stone.png")) {
        int srcX = 40 + listX_phys + di->rcItem.left;
        int srcY = 40 + listY_phys + di->rcItem.top;
        Gdiplus::Rect dst(di->rcItem.left, di->rcItem.top, rowW, rowH);
        g.DrawImage(stone, dst, srcX, srcY, rowW, rowH,
                    Gdiplus::UnitPixel);
    } else {
        HBRUSH bg = CreateSolidBrush(Tok::crBgDeep);
        FillRect(di->hDC, &di->rcItem, bg);
        DeleteObject(bg);
    }

    // v1.3: Selection highlight overlay removed — it was leaving
    // horizontal line artifacts under rows as the mouse moved between
    // items (partial repaint of the semi-transparent overlay would
    // leave a strip un-covered on the OLD selected row). Rows now
    // paint identically regardless of selection state; row focus is
    // still visible via cursor position + the row that would toggle
    // on click. If we want visual feedback later, we should invalidate
    // BOTH the old and new selected rows on LBN_SELCHANGE rather than
    // relying on Windows to do it under NULL_BRUSH bg semantics.

    // ── Section header ─────────────────────────────────────────────
    if (row.kind == PMRow::Kind::SectionHeader) {
        // Subtle underline rule under the text so the header reads as
        // a section divider rather than a row.
        HPEN rulePen = CreatePen(PS_SOLID, 1, Tok::crBronzeDim);
        HPEN oldPen = (HPEN)SelectObject(di->hDC, rulePen);
        MoveToEx(di->hDC,
                 di->rcItem.left + S(8),
                 di->rcItem.bottom - S(2),
                 nullptr);
        LineTo(di->hDC,
               di->rcItem.right - S(8),
               di->rcItem.bottom - S(2));
        SelectObject(di->hDC, oldPen);
        DeleteObject(rulePen);

        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, Tok::crGold);
        RECT textR = di->rcItem;
        textR.left += S(8);
        DrawTextW(di->hDC, row.text.c_str(), -1, &textR,
                  DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }

    // ── Manifest row (read-only inventory entry) ───────────────────
    if (row.kind == PMRow::Kind::Manifest) {
        SetBkMode(di->hDC, TRANSPARENT);
        // Missing entries grey out completely so the user can see at
        // a glance which manifest items aren't on disk.
        SetTextColor(di->hDC, row.isMissing ? Tok::crBronzeDim : Tok::crText);

        // Same left padding as a checkbox would occupy so the rows
        // align visually with header underlines + button row.
        RECT textR = di->rcItem;
        textR.left  += S(8) + S(PM_CHECKBOX_SIZE) + S(10);
        textR.right -= S(8);
        wstring label = BuildDisplayLabel(row.text);
        DrawTextW(di->hDC, label.c_str(), -1, &textR,
                  DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        // v1.3: no DrawFocusRect. Its XOR pattern leaks visible artifacts
        // (bronze/gold horizontal lines under previously-focused rows)
        // when focus moves between rows and the row's paint doesn't
        // XOR the old rect back off. The selection overlay above is
        // enough focus feedback for the pointer path; keyboard nav
        // is still visible via the selection state.
        PMMuteIfGated(g, di, rowW, rowH);
        return;
    }

    // ── Plugin row (toggleable; legacy behavior) ───────────────────
    if (row.kind != PMRow::Kind::Plugin) return;          // defensive
    if (row.pluginIdx < 0
        || row.pluginIdx >= (int)g_pluginList.size()) return;
    const PluginEntry& e = g_pluginList[row.pluginIdx];

    // Checkbox — asset native size is 27×28 (matches DrawFlagCheckbox's
    // CB_SIZE constant). Scaled by g_dpiScale so it grows with the rest
    // of the UI at higher zooms.
    constexpr int CB_ASSET_W = 27;
    constexpr int CB_ASSET_H = 28;
    int cbW = S(CB_ASSET_W);
    int cbH = S(CB_ASSET_H);
    int cbX = di->rcItem.left + S(8);
    int cbY = di->rcItem.top + (rowH - cbH) / 2;

    // DrawFlagCheckbox draws at native 27×28 with no built-in DPI
    // scaling, so we don't call it directly — instead we do the same
    // asset draw at the DPI-scaled size. Fallback to a small gold
    // square + tick if the assets aren't available.
    const wchar_t* cbAsset = e.isChecked ? L"checkbox_checked.png"
                                         : L"checkbox.png";
    if (Gdiplus::Bitmap* cb = AssetImage(cbAsset)) {
        Gdiplus::InterpolationMode prev = g.GetInterpolationMode();
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.DrawImage(cb, cbX, cbY, cbW, cbH);
        g.SetInterpolationMode(prev);
    } else {
        // Programmatic fallback: gold-filled square if checked, empty
        // bronze outline if not — same intent as the asset variants.
        Gdiplus::SolidBrush fill(e.isChecked ? Tok::GoldDeep : Tok::BgPanel);
        g.FillRectangle(&fill, cbX, cbY, cbW - 1, cbH - 1);
        Gdiplus::Pen border(e.isChecked ? Tok::Gold : Tok::BronzeDim, 1.0f);
        g.DrawRectangle(&border, cbX, cbY, cbW - 1, cbH - 1);
        if (e.isChecked) {
            Gdiplus::Pen tick(Gdiplus::Color(255, 0x20, 0x18, 0x08), 2.0f);
            Gdiplus::PointF pts[3] = {
                { (Gdiplus::REAL)(cbX + cbW / 5),       (Gdiplus::REAL)(cbY + cbH / 2)     },
                { (Gdiplus::REAL)(cbX + cbW * 2 / 5),   (Gdiplus::REAL)(cbY + cbH * 3 / 4) },
                { (Gdiplus::REAL)(cbX + cbW - cbW / 5), (Gdiplus::REAL)(cbY + cbH / 4)     },
            };
            g.DrawLines(&tick, pts, 3);
        }
    }

    // Label text — filename only (no (G)/(M) tag; headers carry that).
    // v1.3: substitutes the friendly name from plugin_manifest.json
    // if one is defined for this DLL filename.
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, Tok::crText);
    RECT textR = di->rcItem;
    textR.left = cbX + cbW + S(10);
    textR.right -= S(8);
    wstring label = BuildDisplayLabel(e.fileName);
    DrawTextW(di->hDC, label.c_str(), -1, &textR,
              DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    // v1.3: no DrawFocusRect — see Manifest row above for rationale.
    // Selection overlay is enough visual feedback for the focused row.

    PMMuteIfGated(g, di, rowW, rowH);
}

// ── Themed dialog button paint (Save / Cancel) ───────────────────────
//
// Bronze-bordered gold-text button. Hover lifts the border + text to
// bright gold. No asset — pure GDI/GDI+ paint so this dialog stays
// independent of the launcher's image cache.

static void PMDrawButton(DRAWITEMSTRUCT* di, const wchar_t* label) {
    bool selected = (di->itemState & ODS_SELECTED) != 0;
    bool hover    = (di->itemState & ODS_HOTLIGHT) != 0;
    bool disabled = (di->itemState & ODS_DISABLED) != 0;
    bool highlight = (hover || selected) && !disabled;

    // Fill background.
    HBRUSH bg = CreateSolidBrush(Tok::crBgPanel);
    FillRect(di->hDC, &di->rcItem, bg);
    DeleteObject(bg);

    // Border.
    COLORREF borderCol = disabled ? Tok::crBronzeDim
                                  : (highlight ? Tok::crGoldBright : Tok::crGold);
    HPEN borderPen = CreatePen(PS_SOLID, 1, borderCol);
    HPEN oldPen = (HPEN)SelectObject(di->hDC, borderPen);
    HBRUSH oldBr = (HBRUSH)SelectObject(di->hDC, GetStockObject(NULL_BRUSH));
    Rectangle(di->hDC,
              di->rcItem.left, di->rcItem.top,
              di->rcItem.right, di->rcItem.bottom);
    SelectObject(di->hDC, oldPen);
    SelectObject(di->hDC, oldBr);
    DeleteObject(borderPen);

    // Label.
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, borderCol);
    DrawTextW(di->hDC, label, -1, &di->rcItem,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ── In-place list refresh (v1.6) ─────────────────────────────────────
//
// Re-run the scan/build for the manager's current mod + mode and refill
// the listbox, so a drag-drop install shows up without reopening. Mirrors
// the scan→build→fill sequence in ShowPluginManager, minus the one-time
// window/discovery setup. Manifest-mode mods are read-only inventories
// (BuildConfigRows), legacy mods rescan disk (ScanPlugins + BuildLegacyRows).
static void RefreshPluginManagerContents() {
    if (!g_pmHwnd) return;

    g_pluginList.clear();
    g_pmRows.clear();

    PluginConfig manifest;
    if (g_pmSelectedMod) manifest = LoadPluginConfig(g_pmSelectedMod->dir);

    if (manifest.present) {
        g_pmConfigMode = true;
        if (manifest.plugins.empty()) {
            g_pmConfigEmpty = true;
            g_pmEmptyMessage = FormatEmptyConfigMessage(g_pmSelectedMod);
        } else {
            g_pmConfigEmpty = false;
            wstring modD2rLoaderDir = g_pmSelectedMod->dir + L"\\d2rloader";
            vector<bool> configFound = RunPluginRecoverySweep(
                modD2rLoaderDir, g_pmD2rPath, manifest.plugins);
            BuildConfigRows(manifest, configFound);
        }
    } else {
        g_pmConfigMode = false;
        ScanPlugins(g_pmSelectedMod, g_pmD2rPath);
        BuildLegacyRows();
    }

    // Refill the listbox to match the rebuilt rows. If the list was empty
    // before (manifest-empty mode) the lists may be null — in that case a
    // repaint of the popup shows the message; nothing to refill.
    // Refill BOTH lists from their halves of the rebuilt row vector.
    // A rescan can change which sections exist, so re-run the geometry
    // and move/show the lists to match before repopulating.
    {
        RECT crc; GetClientRect(g_pmHwnd, &crc);
        PMListGeom gm = PMComputeListGeom(crc.right, crc.bottom);
        auto refill = [&](HWND lb, const RECT& r, bool present,
                          int from, int to) {
            if (!lb) return;
            if (!present) { ShowWindow(lb, SW_HIDE); return; }
            MoveWindow(lb, r.left, r.top,
                       r.right - r.left, r.bottom - r.top, TRUE);
            ShowWindow(lb, SW_SHOW);
            SendMessageW(lb, LB_RESETCONTENT, 0, 0);
            for (int i = from; i < to; ++i)
                SendMessageW(lb, LB_ADDSTRING, 0,
                             (LPARAM)g_pmRows[i].text.c_str());
            SendMessageW(lb, LB_SETITEMHEIGHT, 0,
                         (LPARAM)(int)(PM_ROW_H * g_dpiScale));
            InvalidateRect(lb, nullptr, TRUE);
        };
        refill(g_pmListMod, gm.modList, gm.hasMod, 0, g_pmSplit);
        refill(g_pmListGlobal, gm.globList, gm.hasGlobal,
               g_pmSplit, (int)g_pmRows.size());
    }
    InvalidateRect(g_pmHwnd, nullptr, FALSE);
}

// ── Window proc ──────────────────────────────────────────────────────

static LRESULT CALLBACK PluginManagerProc(HWND hw, UINT msg,
                                          WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;

    case WM_DROPFILES: {
        // A zip dropped on the plugin manager is ALWAYS a plugin, installed
        // to the manager's selected mod (mod scope). No mod-vs-plugin
        // detection here — the main window handles mods.
        HDROP hDrop = (HDROP)wp;
        UINT n = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        vector<wstring> zips;
        vector<wstring> patches;
        for (UINT i = 0; i < n; ++i) {
            wchar_t p[MAX_PATH * 2];
            if (DragQueryFileW(hDrop, i, p, MAX_PATH * 2) > 0) {
                wstring path = p;
                size_t dot = path.find_last_of(L'.');
                if (dot == wstring::npos) continue;
                wstring ext = path.substr(dot);
                if (_wcsicmp(ext.c_str(), L".zip") == 0)
                    zips.push_back(path);
                else if (_wcsicmp(ext.c_str(), L".json") == 0)
                    patches.push_back(path);      // bare patch → patches folder
            }
        }
        DragFinish(hDrop);
        for (const wstring& z : zips) {
            // A zip on the plugin manager is a plugin OR a patch bundle
            // (manifest-less zip of .json files). Peek to route.
            ZipKind kind = PeekZipKind(z);
            if (kind == ZipKind::PatchBundle)
                HandlePluginManagerPatchBundle(hw, z, g_pmModFolder);
            else
                HandlePluginManagerDrop(hw, z, g_pmModFolder);   // plugin (or bare)
        }
        for (const wstring& j : patches)
            HandlePluginManagerPatchDrop(hw, j, g_pmModFolder);
        // Rescan + rebuild the list so newly-installed plugins appear.
        if (!zips.empty() || !patches.empty()) RefreshPluginManagerContents();
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        int W = rc.right, H = rc.bottom;

        // v1.3: same treatment as the rename modal — stone
        // background + frame_modbanner.png 9-slice border, so both
        // popups share one visual language. Double-buffered so asset
        // blits don't flicker.
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

        {
            Gdiplus::Graphics g(memDC);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

            // Stone bg — sampled at (40,40) so the texture cadence
            // matches the loader-options panel + the rename modal.
            Gdiplus::Bitmap* stone = AssetImage(L"bg_stone.png");
            if (stone) {
                int sw = (int)stone->GetWidth();
                int sh = (int)stone->GetHeight();
                int cropW = (sw < W) ? sw : W;
                int cropH = (sh < H) ? sh : H;
                Gdiplus::Rect dst(0, 0, W, H);
                g.DrawImage(stone, dst, 40, 40, cropW, cropH,
                            Gdiplus::UnitPixel);
            } else {
                Gdiplus::SolidBrush bg(Gdiplus::Color(28, 24, 20));
                g.FillRectangle(&bg, 0, 0, W, H);
            }

            // Frame chrome — 9-sliced frame_modbanner.png. Corner
            // inset of 24 keeps the ornaments crisp.
            if (Gdiplus::Bitmap* frame = AssetImage(L"frame_modbanner.png")) {
                DrawButton9Slice(g, frame, 0, 0, W, H, 24);
            } else {
                Gdiplus::Pen fallback(Tok::Bronze, 1.0f);
                g.DrawRectangle(&fallback, 1, 1, W - 3, H - 3);
            }

            // Title — left-justified "Plugin Manager" 15px from the window
            // edge, with the mod name on a second line beneath it. Two lines
            // keep it clear of the Repository button in the top-right corner.
            Gdiplus::SolidBrush titleBr(Tok::Gold);
            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentNear);
            sf.SetLineAlignment(Gdiplus::StringAlignmentNear);

            Gdiplus::Font* tf = g_fModName ? g_fModName : g_fNavSm;
            if (tf) {
                int tx = (int)(15 * g_dpiScale);   // 15px from the window edge
                g.DrawString(L"Plugin Manager", -1, tf,
                    Gdiplus::RectF((REAL)tx, (REAL)S(8),
                                   (REAL)(rc.right - rc.left - tx),
                                   (REAL)S(26)),
                    &sf, &titleBr);
                if (!g_pmModName.empty()) {
                    Gdiplus::SolidBrush subBr(Gdiplus::Color(0xC8, 0xB4, 0x84));
                    Gdiplus::Font* sub = g_fNavSm ? g_fNavSm : tf;
                    g.DrawString(g_pmModName.c_str(), -1, sub,
                        Gdiplus::RectF((REAL)tx, (REAL)S(32),
                                       (REAL)(rc.right - rc.left - tx),
                                       (REAL)S(22)),
                        &sf, &subBr);
                }
            }

            // v1.3: when the active manifest is empty (modder explicitly
            // shipped {"plugins": []}), there's no listbox — we paint the
            // author/modname message centered in the area the listbox
            // would normally occupy.
            if (g_pmConfigEmpty && !g_pmEmptyMessage.empty()) {
                int listX = (int)(PM_PAD * g_dpiScale);
                int listY = (int)((PM_TITLE_H + PM_LIST_PAD_TOP) * g_dpiScale);
                int listW = W - 2 * listX;
                int listH = H - listY
                          - (int)((PM_PAD + PM_BTN_H + PM_LIST_PAD_BOT) * g_dpiScale);
                Gdiplus::RectF msgRect((REAL)listX, (REAL)listY,
                                       (REAL)listW, (REAL)listH);
                Gdiplus::SolidBrush msgBr(Tok::Gold);
                if (tf) {
                    g.DrawString(g_pmEmptyMessage.c_str(), -1, tf,
                                 msgRect, &sf, &msgBr);
                }
            }

            // v1.6.2: static section headers. These used to be rows
            // inside the listbox, which meant they scrolled out of view
            // as soon as the list was long enough to scroll. Painting
            // them on the popup pins them above their list.
            if (!g_pmConfigEmpty) {
                PMListGeom gm = PMComputeListGeom(W, H);
                Gdiplus::Font* hf = g_fNavSm ? g_fNavSm : g_fBtn;
                Gdiplus::SolidBrush hdrBr(Tok::Gold);
                Gdiplus::StringFormat sfH;
                sfH.SetAlignment(Gdiplus::StringAlignmentNear);
                sfH.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                sfH.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
                Gdiplus::Pen rule(Tok::BronzeDim, 1.0f);
                Gdiplus::Pen warnRule(Tok::RedDark, 1.0f);

                auto header = [&](const RECT& r, const wchar_t* text,
                                  bool gateOff) {
                    if (hf) {
                        g.DrawString(text, -1, hf,
                            Gdiplus::RectF((REAL)(r.left + S(4)), (REAL)r.top,
                                           (REAL)((r.right - r.left) - S(8)),
                                           (REAL)(r.bottom - r.top)),
                            &sfH, &hdrBr);
                        // v1.6.2: when the matching D2RLoader extension
                        // gate is off, nothing in this section loads in
                        // game. Say so on the section itself rather
                        // than in a banner — the warning is only
                        // meaningful next to the list it applies to.
                        if (gateOff) {
                            Gdiplus::StringFormat sfR;
                            sfR.SetAlignment(Gdiplus::StringAlignmentFar);
                            sfR.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                            sfR.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                            sfR.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
                            Gdiplus::SolidBrush warn(Tok::RedBright);
                            g.DrawString(L"NOT LOADING \u2014 see Loader Options",
                                         -1, hf,
                                Gdiplus::RectF((REAL)(r.left + S(4)), (REAL)r.top,
                                               (REAL)((r.right - r.left) - S(8)),
                                               (REAL)(r.bottom - r.top)),
                                &sfR, &warn);
                        }
                    }
                    // Hairline under the label, matching the rule the
                    // old SectionHeader row drew.
                    g.DrawLine(gateOff ? &warnRule : &rule,
                               (INT)r.left, (INT)(r.bottom - 1),
                               (INT)r.right, (INT)(r.bottom - 1));
                };

                if (gm.hasMod)    header(gm.modHdr,  L"Mod plugins",
                                         PMModGateOff());
                if (gm.hasGlobal) header(gm.globHdr, L"Global plugins",
                                         PMGlobalGateOff());

                // Themed gutters beside whichever lists overflow.
                if (gm.hasMod && gm.modNeedsSb) {
                    PMPaintScrollbar(g, PMScrollbarGeom(
                        g_pmListMod, gm.modBar, g_pmSplit));
                }
                if (gm.hasGlobal && gm.globNeedsSb) {
                    PMPaintScrollbar(g, PMScrollbarGeom(
                        g_pmListGlobal, gm.globBar,
                        (int)g_pmRows.size() - g_pmSplit));
                }
            }
        }

        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);

        EndPaint(hw, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        // Scrollbar gutters live on the popup, not inside the lists,
        // so the popup owns their hit-testing.
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT rc; GetClientRect(hw, &rc);
        if (g_pmConfigEmpty) break;
        PMListGeom gm = PMComputeListGeom(rc.right, rc.bottom);

        struct BarRef { HWND lb; RECT bar; int count; bool on; };
        BarRef bars[2] = {
            { g_pmListMod,    gm.modBar,  g_pmSplit,
              gm.hasMod && gm.modNeedsSb },
            { g_pmListGlobal, gm.globBar, (int)g_pmRows.size() - g_pmSplit,
              gm.hasGlobal && gm.globNeedsSb },
        };

        for (int b = 0; b < 2; ++b) {
            if (!bars[b].on || !bars[b].lb) continue;
            PMSbGeom s = PMScrollbarGeom(bars[b].lb, bars[b].bar, bars[b].count);
            if (!s.present) continue;
            if (!PtInRect(&s.area, pt)) continue;

            int newTop = s.topIndex;
            if (PtInRect(&s.thumb, pt) && s.maxTop > 0) {
                g_pmSbDragList = bars[b].lb;
                g_pmSbGrabDY   = pt.y - (int)s.thumb.top;
                SetCapture(hw);
                return 0;
            } else if (PtInRect(&s.up, pt)) {
                newTop = s.topIndex - 1;
            } else if (PtInRect(&s.down, pt)) {
                newTop = s.topIndex + 1;
            } else if (PtInRect(&s.track, pt)) {
                newTop = (pt.y < s.thumb.top)
                       ? s.topIndex - s.visible
                       : s.topIndex + s.visible;
            } else {
                return 0;
            }
            if (newTop < 0)        newTop = 0;
            if (newTop > s.maxTop) newTop = s.maxTop;
            if (newTop != s.topIndex) {
                SendMessage(bars[b].lb, LB_SETTOPINDEX, (WPARAM)newTop, 0);
                InvalidateRect(bars[b].lb, nullptr, FALSE);
                PMInvalidateBar(bars[b].lb);
            }
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE: {
        if (!g_pmSbDragList) break;
        RECT rc; GetClientRect(hw, &rc);
        PMListGeom gm = PMComputeListGeom(rc.right, rc.bottom);
        bool isGlobal = (g_pmSbDragList == g_pmListGlobal);
        RECT bar   = isGlobal ? gm.globBar : gm.modBar;
        int  count = isGlobal ? (int)g_pmRows.size() - g_pmSplit : g_pmSplit;
        PMSbGeom s = PMScrollbarGeom(g_pmSbDragList, bar, count);
        if (s.present && s.maxTop > 0) {
            int thumbH = (int)(s.thumb.bottom - s.thumb.top);
            int travel = s.trackH - thumbH;
            if (travel > 0) {
                int newTop = GET_Y_LPARAM(lp) - g_pmSbGrabDY;
                if (newTop < s.trackTop)          newTop = s.trackTop;
                if (newTop > s.trackTop + travel) newTop = s.trackTop + travel;
                int idx = (int)((long long)(newTop - s.trackTop)
                                * s.maxTop / travel);
                if (idx < 0)        idx = 0;
                if (idx > s.maxTop) idx = s.maxTop;
                if (idx != s.topIndex) {
                    SendMessage(g_pmSbDragList, LB_SETTOPINDEX, (WPARAM)idx, 0);
                    InvalidateRect(g_pmSbDragList, nullptr, FALSE);
                }
                PMInvalidateBar(g_pmSbDragList);
            }
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_pmSbDragList) {
            HWND wasDragging = g_pmSbDragList;
            g_pmSbDragList = nullptr;
            ReleaseCapture();
            PMInvalidateBar(wasDragging);
        }
        return 0;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
        if (di->CtlID == PM_IDC_LIST_MOD
            || di->CtlID == PM_IDC_LIST_GLOBAL) {   // listbox row
            PMDrawItem(di);
            return TRUE;
        }
        // v1.3: buttons are created via MkStdBtn(...Plugins) so
        // PaintOwnerDrawButton handles them — nexus_update asset 9-slice
        // + hover glow + click shrink, no hover grow.
        if (PaintOwnerDrawButton(di)) return TRUE;
        return 0;
    }

    // v1.3: suppress the listbox's own background paint so the popup's
    // stone (drawn in WM_PAINT above) shows through in the client area
    // outside individual row rects. Returning the stock NULL_BRUSH tells
    // the listbox not to erase — WM_DRAWITEM per row supplies the visible
    // paint via bg_stone samples that align with the popup's stone.
    case WM_CTLCOLORLISTBOX: {
        HDC lbDC = (HDC)wp;
        SetBkMode(lbDC, TRANSPARENT);
        SetTextColor(lbDC, Tok::crText);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == 1) {           // Save
            ApplyChanges();
            DestroyWindow(hw);
            return 0;
        }
        if (id == 2) {           // Cancel
            DestroyWindow(hw);
            return 0;
        }
        if (id == 50) {          // v1.6: READMEs picker
            ShowReadmePicker(hw);
            return 0;
        }
        if (id == 51) {          // v1.6.2: Repository browser
            ShowRepoBrowser(hw, g_pmModFolder);
            return 0;
        }
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hw);
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(hw); return 0; }
        break;

    case WM_DESTROY: {
        HWND parent = GetWindow(hw, GW_OWNER);
        if (parent) {
            EnableWindow(parent, TRUE);
            SetForegroundWindow(parent);
        }
        if (g_pmListGlobal) {
            RemoveWindowSubclass(g_pmListGlobal, PMListSubclass, 1);
        }
        if (g_pmListMod) {
            RemoveWindowSubclass(g_pmListMod, PMListSubclass, 1);
        }
        g_pmHwnd      = nullptr;
        if (g_pmSbDragList) { g_pmSbDragList = nullptr; ReleaseCapture(); }
        g_pmSbGrabDY   = 0;
        g_pmListMod    = nullptr;
        g_pmListGlobal = nullptr;
        g_pmSplit      = 0;
        g_pmHasMod     = false;
        g_pmHasGlobal  = false;
        g_pmSaveBtn   = nullptr;
        g_pmCancelBtn = nullptr;
        g_pmReadmesBtn = nullptr;
        g_pmSelectedMod = nullptr;
        g_pmD2rPath.clear();
        // Don't PostQuitMessage — that would propagate WM_QUIT to the
        // launcher's main message loop and quit the whole app. The
        // pump in ShowPluginManager checks g_pmHwnd and exits cleanly.
        return 0;
    }
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

// ── Public entry point ───────────────────────────────────────────────

void ShowPluginManager(HWND parent,
                       const ModInfo* selectedMod,
                       const wstring& d2rPath) {
    if (g_pmHwnd) return;   // one popup at a time

    g_pmModName = selectedMod ? selectedMod->name : L"";
    g_pmModFolder = selectedMod ? selectedMod->folder : L"";   // for drag-drop scope
    g_pmSelectedMod = selectedMod;   // retained for in-place refresh (v1.6)
    g_pmD2rPath     = d2rPath;

    // Reset v1.3 manifest-mode state before each invocation so a stale
    // value from a previous popup never leaks through.
    g_pmConfigMode  = false;
    g_pmConfigEmpty = false;
    g_pmEmptyMessage.clear();
    g_pluginList.clear();
    g_pmRows.clear();

    // ── v1.3 mode detection ─────────────────────────────────────────
    //
    // If the active mod has a parseable plugin_config.json, this
    // popup is the inventory view: read-only mod plugin list, no
    // globals, sweeps applied on entry. Otherwise, fall through to
    // the legacy scan + toggle behavior.
    PluginConfig manifest;
    vector<bool>   configFound;
    if (selectedMod) manifest = LoadPluginConfig(selectedMod->dir);

    if (manifest.present) {
        g_pmConfigMode = true;
        // Globals must not load alongside a manifest-mod's plugins;
        // run the sweep before we (re)scan or build any rows.
        MoveGlobalPluginsToDisabled(d2rPath);

        if (manifest.plugins.empty()) {
            // Empty manifest: nothing to render in the list. WM_PAINT
            // will draw the author/modname message in the listbox slot.
            g_pmConfigEmpty = true;
            g_pmEmptyMessage  = FormatEmptyConfigMessage(selectedMod);
        } else {
            // Non-empty manifest: try to recover any missing entries
            // from globals/mod-disabled, then build read-only rows
            // with grey-outs for whatever's still missing. The sweep
            // routes each entry to plugins\ or patches\ based on file
            // extension, so a mixed .dll + .json manifest is handled
            // in one pass.
            wstring modD2rLoaderDir = selectedMod->dir + L"\\d2rloader";
            configFound = RunPluginRecoverySweep(modD2rLoaderDir,
                                                   d2rPath,
                                                   manifest.plugins);
            BuildConfigRows(manifest, configFound);
        }
    } else {
        // Legacy mode: scan disk, build header-grouped row list.
        ScanPlugins(selectedMod, d2rPath);
        BuildLegacyRows();
    }

    // Discovery: pre-populate plugin_manifest.json with empty entries
    // for every plugin (.dll) and patch (.json) we just learned about.
    // Existing entries — including ones whose file is no longer found —
    // are never modified. This makes it easy for users to hand-edit the
    // manifest: they just open it in a text editor and fill in friendly
    // names beside pre-populated keys.
    //
    // Scope: every plugin/patch in the eight relevant folders
    // (mod/global × plugins/patches × active/Disabled), PLUS every
    // entry in the per-mod plugin_config.json if one exists (those may
    // be missing from disk but the user might still want to assign
    // friendly names so future renames are ready).
    {
        auto enumerateFiles = [](const wstring& folder,
                                 const wchar_t* pattern,
                                 vector<wstring>& out) {
            DWORD attr = GetFileAttributesW(folder.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES) return;
            if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) return;
            WIN32_FIND_DATAW fd;
            wstring searchPattern = folder + L"\\" + pattern;
            HANDLE h = FindFirstFileW(searchPattern.c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return;
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                out.push_back(fd.cFileName);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        };

        vector<wstring> seen;

        // Per-mod config entries first (if any) — covers manifest-only
        // files that aren't on disk yet but the modder declared anyway.
        if (manifest.present) {
            for (const auto& p : manifest.plugins) seen.push_back(p);
        }

        // Global plugins + patches: active + disabled. Both are scanned
        // in both modes (in config mode, MoveGlobalPluginsToDisabled has
        // just emptied the active folders, but the disabled folders now
        // hold those entries — discovery still picks them up).
        wstring globalBase = d2rPath + L"\\d2rloader";
        enumerateFiles(globalBase + L"\\plugins",           L"*.dll",  seen);
        enumerateFiles(globalBase + L"\\plugins\\Disabled", L"*.dll",  seen);
        enumerateFiles(globalBase + L"\\patches",           L"*.json", seen);
        enumerateFiles(globalBase + L"\\patches\\Disabled", L"*.json", seen);

        // Mod-local plugins + patches: active + disabled.
        if (selectedMod) {
            wstring modBase = selectedMod->dir + L"\\d2rloader";
            enumerateFiles(modBase + L"\\plugins",           L"*.dll",  seen);
            enumerateFiles(modBase + L"\\plugins\\Disabled", L"*.dll",  seen);
            enumerateFiles(modBase + L"\\patches",           L"*.json", seen);
            enumerateFiles(modBase + L"\\patches\\Disabled", L"*.json", seen);
        }

        // EnsureManifestEntries handles dedup internally (case-
        // insensitive find before insert), so duplicates across folders
        // and the config list don't cause spurious adds.
        if (EnsureManifestEntries(seen)) {
            SavePluginManifest();
        }
    }

    if (!g_pmClassReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = PluginManagerProc;
        wc.hInstance     = g_hInst;
        wc.lpszClassName = L"AngirisPluginManager";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        g_pmClassReg = true;
    }

    // Position the popup centered on the parent.
    RECT pr;
    GetWindowRect(parent, &pr);
    int physW = (int)(PM_W * g_dpiScale);
    int physH = (int)(PM_H * g_dpiScale);
    int x = pr.left + ((pr.right  - pr.left) - physW) / 2;
    int y = pr.top  + ((pr.bottom - pr.top ) - physH) / 2;

    g_pmHwnd = CreateWindowExW(
        0,                 // NOT WS_EX_TOPMOST: as an owned popup (owner =
                           // the launcher, passed below), it already stays
                           // above the launcher via owner/owned z-order.
                           // TOPMOST pinned it above EVERY window system-
                           // wide, covering unrelated Explorer windows the
                           // user had brought to the front. No DLGMODALFRAME
                           // either — the frame_modbanner 9-slice is the
                           // visible border; the system 3D edge showed up as
                           // a bright white ring around the popup.
        L"AngirisPluginManager",
        L"Plugin Manager",
        WS_POPUP | WS_VISIBLE,
        x, y, physW, physH,
        parent, nullptr, g_hInst, nullptr);
    if (!g_pmHwnd) return;

    // Accept plugin-zip drops → mod-scoped install for the selected mod
    // (see the WM_DROPFILES handler in PluginManagerProc).
    DragAcceptFiles(g_pmHwnd, TRUE);

    // Owner-drawn LISTBOXes — one per section, so each scrolls on its
    // own and the section headers can stay pinned above them (painted
    // by WM_PAINT). Skipped in empty-manifest mode, where WM_PAINT
    // renders the author message instead.
    if (!g_pmConfigEmpty) {
        PMListGeom gm = PMComputeListGeom(physW, physH);

        auto mkList = [&](const RECT& r, int ctlId) -> HWND {
            // NOTE: do NOT add WS_EX_COMPOSITED here. It was tried to
            // smooth the per-scroll repaint and stopped the owner-drawn
            // rows painting entirely — the items are present and
            // selectable, the list just renders empty. Its double
            // buffering doesn't cooperate with LBS_OWNERDRAWFIXED
            // rows drawing straight to di->hDC.
            HWND lb = CreateWindowExW(0,   // v1.3: no CLIENTEDGE — stone bg + parent chrome only
                L"LISTBOX", L"",
                // No WS_VSCROLL: PMPaintScrollbar draws a themed
                // gutter beside the list instead.
                WS_CHILD | WS_VISIBLE
                    | LBS_OWNERDRAWFIXED | LBS_NOTIFY | LBS_HASSTRINGS,
                r.left, r.top, r.right - r.left, r.bottom - r.top,
                g_pmHwnd, (HMENU)(UINT_PTR)ctlId, g_hInst, nullptr);
            if (lb) {
                SendMessage(lb, LB_SETITEMHEIGHT, 0,
                            (LPARAM)(int)(PM_ROW_H * g_dpiScale));
                SetWindowSubclass(lb, PMListSubclass, 1, 0);
            }
            return lb;
        };

        // Handles must exist before the rows are added: PMDrawItem asks
        // PMBaseFor(di->hwndItem) which compares against g_pmListGlobal,
        // and LB_ADDSTRING can trigger a draw.
        if (gm.hasMod)    g_pmListMod    = mkList(gm.modList,  PM_IDC_LIST_MOD);
        if (gm.hasGlobal) g_pmListGlobal = mkList(gm.globList, PM_IDC_LIST_GLOBAL);

        // Populate with placeholder strings so each listbox knows how
        // many items it has. The owner-draw path reads from g_pmRows by
        // base + index — the string content here doesn't actually paint.
        for (int i = 0; i < g_pmSplit && g_pmListMod; ++i) {
            SendMessageW(g_pmListMod, LB_ADDSTRING, 0,
                         (LPARAM)g_pmRows[i].text.c_str());
        }
        for (int i = g_pmSplit; i < (int)g_pmRows.size() && g_pmListGlobal; ++i) {
            SendMessageW(g_pmListGlobal, LB_ADDSTRING, 0,
                         (LPARAM)g_pmRows[i].text.c_str());
        }
    }

    // Themed owner-draw buttons at the bottom. Layout depends on mode:
    //   • Legacy mode → Cancel + Save Selection, side by side, centered
    //   • Manifest mode (either flavor) → one Close button, centered
    int btnRowY = physH
                - (int)((PM_PAD + PM_BTN_H) * g_dpiScale);
    int physBtnW = (int)(PM_BTN_W * g_dpiScale);
    int physBtnH = (int)(PM_BTN_H * g_dpiScale);
    int physGap  = (int)(PM_BTN_GAP * g_dpiScale);

    if (g_pmConfigMode) {
        // Manifest mode: Close + READMEs, two across, centered.
        int rowW = physBtnW * 2 + physGap;
        int rowX = (physW - rowW) / 2;
        g_pmCancelBtn = MkStdBtn(g_pmHwnd, L"Close", 2,
            rowX, btnRowY, physBtnW, physBtnH,
            true, ButtonKind::Plugins);
        g_pmReadmesBtn = MkStdBtn(g_pmHwnd, L"READMEs", 50,
            rowX + physBtnW + physGap, btnRowY, physBtnW, physBtnH,
            true, ButtonKind::Plugins);
    } else {
        // Legacy mode: Save + Cancel + READMEs, three across, centered.
        int rowW   = physBtnW * 3 + physGap * 2;
        int rowX   = (physW - rowW) / 2;
        int saveX     = rowX;
        int cancelX   = rowX + physBtnW + physGap;
        int readmesX  = rowX + (physBtnW + physGap) * 2;

        g_pmSaveBtn = MkStdBtn(g_pmHwnd, L"Save Selection", 1,
            saveX, btnRowY, physBtnW, physBtnH,
            true, ButtonKind::Plugins);
        g_pmCancelBtn = MkStdBtn(g_pmHwnd, L"Cancel", 2,
            cancelX, btnRowY, physBtnW, physBtnH,
            true, ButtonKind::Plugins);
        g_pmReadmesBtn = MkStdBtn(g_pmHwnd, L"READMEs", 50,
            readmesX, btnRowY, physBtnW, physBtnH,
            true, ButtonKind::Plugins);
    }

    // Repository button — top-right corner, aligned with the title row
    // (per the layout mockup). Standalone, above the list.
    {
        int repoW = (int)(PM_BTN_W * g_dpiScale);
        int repoH = (int)(PM_BTN_H * g_dpiScale);
        int repoX = physW - (int)(PM_PAD * g_dpiScale) - repoW;
        int repoY = (int)((PM_PAD - 4) * g_dpiScale);
        MkStdBtn(g_pmHwnd, L"Repository", 51,
            repoX, repoY, repoW, repoH, true, ButtonKind::Plugins);
    }

    // Modal: disable the parent until our internal pump exits.
    EnableWindow(parent, FALSE);
    ShowWindow(g_pmHwnd, SW_SHOW);
    UpdateWindow(g_pmHwnd);

    // Internal message loop. Exits when g_pmHwnd has been nulled by
    // WM_DESTROY. After DestroyWindow is called, Windows still posts
    // WM_NCDESTROY which unblocks GetMessage one final time, so the
    // loop reliably terminates without needing a wakeup message.
    MSG msg;
    while (g_pmHwnd) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == 0 || got == -1) {
            // WM_QUIT or error — re-post WM_QUIT so the outer loop
            // sees it too, then bail. Shouldn't happen in practice
            // because our WM_DESTROY no longer calls PostQuitMessage.
            if (got == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (g_pmHwnd && IsDialogMessageW(g_pmHwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    // Parent re-enable happens in WM_DESTROY. Nothing left to do.
}

// ═════════════════════════════════════════════════════════════════════
//  RENAME MODAL (Phase E2)
// ═════════════════════════════════════════════════════════════════════
//
// Shown from PMListSubclass when the user right-clicks a plugin row
// and selects "Rename...". Modal to the plugin manager popup (nested
// pump pattern, same as ShowPluginManager is modal to the main window).
//
// On accept:
//   • Plugin manager's WM_CONTEXTMENU handler stores the result via
//     SetPluginFriendlyName + SavePluginManifest.
//   • The listbox is invalidated so PMDrawItem re-resolves the friendly
//     name on the next paint.
//
// On cancel (Esc, Cancel button, or close): no map mutation, no save.

static LRESULT CALLBACK RenameProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CTLCOLOREDIT: {
        // Style the visible rename EDIT to match the box interior: pale
        // gold text on the dark shadow well, no repaint flicker (return a
        // solid dark brush as the control background).
        HDC dc = (HDC)wp;
        SetTextColor(dc, RGB(0xF0, 0xDF, 0xB0));   // pale gold, ~Tok::GoldBright
        SetBkColor(dc, RGB(0, 0, 0));               // matches the opaque interior
        static HBRUSH s_rmBg = CreateSolidBrush(RGB(0, 0, 0));
        return (LRESULT)s_rmBg;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        int W = rc.right, H = rc.bottom;

        // v1.3: double-buffered stone-textured popup, matching
        // dialogs.cpp's ConflictDialog treatment so all secondary
        // popups share one visual language.
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

        {
            Gdiplus::Graphics g(memDC);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

            // Stone background — sampled from bg_stone.png at (40,40)
            // to match the loader-options panel's texture cadence so
            // the popup reads as inlaid rather than pasted on top.
            Gdiplus::Bitmap* stone = AssetImage(L"bg_stone.png");
            if (stone) {
                int sw = (int)stone->GetWidth();
                int sh = (int)stone->GetHeight();
                int cropW = (sw < W) ? sw : W;
                int cropH = (sh < H) ? sh : H;
                Gdiplus::Rect dst(0, 0, W, H);
                g.DrawImage(stone, dst, 40, 40, cropW, cropH,
                            Gdiplus::UnitPixel);
            } else {
                Gdiplus::SolidBrush bg(Gdiplus::Color(28, 24, 20));
                g.FillRectangle(&bg, 0, 0, W, H);
            }

            // Frame chrome — 9-sliced frame_modbanner.png overlaying
            // the stone bg, so the popup shares its border language
            // with the mod-list row banners on the main window.
            // corner inset of 24 keeps the ornate corners crisp; the
            // straight edge strips stretch to whatever size the popup
            // happens to be. Fallback: solid 1px bronze rect if the
            // asset didn't load.
            if (Gdiplus::Bitmap* frame = AssetImage(L"frame_modbanner.png")) {
                DrawButton9Slice(g, frame, 0, 0, W, H, 24);
            } else {
                Gdiplus::Pen fallback(Tok::Bronze, 1.0f);
                g.DrawRectangle(&fallback, 1, 1, W - 3, H - 3);
            }

            // Title — "Rename <DLL>" in gold, using the mod-name font
            // (same weight as the main window's mod list) so the popup
            // feels of a piece with the rest of the UI.
            Gdiplus::Font* tf = g_fModName ? g_fModName : g_fNavSm;
            Gdiplus::SolidBrush goldBr(Tok::Gold);
            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentCenter);
            sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            if (tf) {
                wstring title = L"Rename " + g_rmDllName;
                g.DrawString(title.c_str(), -1, tf,
                    Gdiplus::RectF(0, (REAL)S(8),
                                   (REAL)W, (REAL)S(RM_TITLE_H - 8)),
                    &sf, &goldBr);
            }

            // Body sub-label in parchment tone.
            Gdiplus::SolidBrush labelBr(Tok::TextParchment);
            if (tf) {
                int labelY = (int)((RM_TITLE_H + 4) * g_dpiScale);
                int labelH = (int)(RM_LABEL_H * g_dpiScale);
                int padX = (int)(RM_PAD * g_dpiScale);
                g.DrawString(L"Display name in the launcher:", -1, tf,
                    Gdiplus::RectF((REAL)padX, (REAL)labelY,
                                   (REAL)(W - 2 * padX), (REAL)labelH),
                    &sf, &labelBr);
            }

            // text_box.png as the input chrome. Text + caret are
            // painted programmatically over the shadow well below —
            // the hidden EDIT is off-screen and doesn't contribute to
            // what the user sees inside this rect.
            int editX = (int)(RM_PAD * g_dpiScale);
            int editY = (int)((RM_TITLE_H + RM_LABEL_H + 8) * g_dpiScale);
            int editW = W - 2 * editX;
            int editH = (int)(RM_EDIT_H * g_dpiScale);
            if (Gdiplus::Bitmap* tb = AssetImage(L"text_box.png")) {
                Gdiplus::InterpolationMode prev = g.GetInterpolationMode();
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                g.DrawImage(tb, editX, editY, editW, editH);
                g.SetInterpolationMode(prev);
            }

            // Dark shadow overlay INSIDE text_box.png's interior. Opaque
            // black so it reads unambiguously against text_box's bronze
            // chrome. 4px inset preserves the chrome ring.
            {
                int shadowInset = S(4);
                Gdiplus::SolidBrush shadow(Gdiplus::Color(255, 0, 0, 0));
                g.FillRectangle(&shadow,
                    editX + shadowInset,
                    editY + shadowInset,
                    editW - 2 * shadowInset,
                    editH - 2 * shadowInset);
            }

            // The input text and caret are now rendered by a real, visible
            // EDIT control positioned inside this box (see the setup code and
            // WM_CTLCOLOREDIT). That gives native click-to-position, caret
            // movement, selection, and mid-string editing. We only draw the
            // box chrome + dark interior here; the EDIT draws the text.
        }

        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);

        EndPaint(hw, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
        // OK / Cancel buttons — nexus_update art, hover glow, click shrink.
        if (PaintOwnerDrawButton(di)) return TRUE;
        return 0;
    }

    case WM_COMMAND: {
        WORD id   = LOWORD(wp);
        WORD code = HIWORD(wp);
        // The visible EDIT (id=10) draws its own text now. We still mirror
        // its content into g_rmText on change so the OK handler can read it.
        // No InvalidateRect needed — the control repaints itself.
        if (id == 10 && code == EN_CHANGE) {
            wchar_t buf[1024] = {};
            GetWindowTextW(g_rmInput, buf, 1024);
            g_rmText = buf;
            return 0;
        }
        if (code == BN_CLICKED) {
            if (id == 1) {
                // OK — trim leading/trailing whitespace from g_rmText.
                wstring v = g_rmText;
                size_t a = v.find_first_not_of(L" \t");
                size_t b = v.find_last_not_of(L" \t");
                g_rmResult = (a == wstring::npos) ? L""
                                                  : v.substr(a, b - a + 1);
                g_rmAccepted = true;
                DestroyWindow(hw);
                return 0;
            }
            if (id == 2) {
                g_rmAccepted = false;
                DestroyWindow(hw);
                return 0;
            }
        }
        break;
    }

    case WM_KEYDOWN:
        // Esc cancels. Enter → OK via IsDialogMessage's default-button
        // handling. Character input, backspace, arrows, and click-to-
        // position are all handled natively by the visible EDIT.
        if (wp == VK_ESCAPE) {
            g_rmAccepted = false;
            DestroyWindow(hw);
            return 0;
        }
        break;

    case WM_LBUTTONDOWN:
        // A click on the modal BACKGROUND (not the EDIT) refocuses the
        // input so typing resumes. Clicks inside the EDIT are handled by
        // the control itself and position the caret.
        if (g_rmInput) SetFocus(g_rmInput);
        break;

    case WM_ACTIVATE:
        // Restore focus to the EDIT when the modal becomes active again
        // (e.g. bringing the launcher back from the background).
        if (LOWORD(wp) != WA_INACTIVE && g_rmInput) {
            SetFocus(g_rmInput);
        }
        break;

    case WM_SETFOCUS:
        // Forward any focus the modal frame receives to the EDIT.
        if (g_rmInput) SetFocus(g_rmInput);
        return 0;

    case WM_CLOSE:
        g_rmAccepted = false;
        DestroyWindow(hw);
        return 0;

    case WM_DESTROY: {
        HWND parent = GetWindow(hw, GW_OWNER);
        if (parent) {
            EnableWindow(parent, TRUE);
            SetForegroundWindow(parent);
        }
        g_rmHwnd      = nullptr;
        g_rmInput     = nullptr;
        g_rmOkBtn     = nullptr;
        g_rmCancelBtn = nullptr;
        if (g_rmEditFont) { DeleteObject(g_rmEditFont); g_rmEditFont = nullptr; }
        // Don't PostQuitMessage — that would propagate WM_QUIT to the
        // outer plugin manager pump (and the main app pump). The
        // ShowRenameModal pump checks g_rmHwnd and exits cleanly.
        return 0;
    }
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

static bool ShowRenameModal(HWND parent,
                            const wstring& dllName,
                            const wstring& currentFriendly,
                            wstring& outNewName) {
    if (g_rmHwnd || dllName.empty()) return false;

    g_rmDllName  = dllName;
    g_rmText     = currentFriendly;   // seed-style buffer
    g_rmResult.clear();
    g_rmAccepted = false;

    if (!g_rmClassReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = RenameProc;
        wc.hInstance     = g_hInst;
        wc.lpszClassName = L"AngirisRenameModal";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        g_rmClassReg = true;
    }

    RECT pr;
    GetWindowRect(parent, &pr);
    int physW = (int)(RM_W * g_dpiScale);
    int physH = (int)(RM_H * g_dpiScale);
    int x = pr.left + ((pr.right  - pr.left) - physW) / 2;
    int y = pr.top  + ((pr.bottom - pr.top ) - physH) / 2;

    g_rmHwnd = CreateWindowExW(
        0,                 // owned popup: above its owner without pinning
                           // over other apps (see the manager window above).
                           // No DLGMODALFRAME — same reason as the
                           // plugin manager above: frame_modbanner is
                           // the border, the system 3D edge read as a
                           // white ring.
        L"AngirisRenameModal",
        L"Rename plugin",
        WS_POPUP | WS_VISIBLE,
        x, y, physW, physH,
        parent, nullptr, g_hInst, nullptr);
    if (!g_rmHwnd) return false;

    int btnRowY = physH - (int)((RM_PAD + RM_BTN_H) * g_dpiScale);
    int physBtnW = (int)(RM_BTN_W * g_dpiScale);
    int physBtnH = (int)(RM_BTN_H * g_dpiScale);
    int physGap  = (int)(RM_BTN_GAP * g_dpiScale);
    int btnRowW  = physBtnW * 2 + physGap;
    int btnRowX  = (physW - btnRowW) / 2;

    // Hidden EDIT for keyboard capture. Positioned off-screen (1×1 at
    // negative coords in modal client space) so it's never visible;
    // its EN_CHANGE mirrors the buffer into g_rmText, which the paint
    // pass renders inside the black shadow well.
    // The input is a REAL, visible EDIT sitting inside the text_box.png
    // chrome. Using the native control (instead of hand-painting the text)
    // gives click-to-position, caret movement, selection, and mid-string
    // editing for free. It's placed to match the box interior the paint
    // pass draws (editX/Y/W/H there use the same constants), inset to sit
    // within the dark shadow well. Borderless — the box art is the border.
    RECT rmClient; GetClientRect(g_rmHwnd, &rmClient);
    int rmW      = rmClient.right;
    int rmEditX  = (int)(RM_PAD * g_dpiScale);
    int rmEditY  = (int)((RM_TITLE_H + RM_LABEL_H + 8) * g_dpiScale);
    int rmEditW  = rmW - 2 * rmEditX;
    int rmEditH  = (int)(RM_EDIT_H * g_dpiScale);
    int rmInset  = (int)(4 * g_dpiScale) + (int)(6 * g_dpiScale); // shadow + text pad
    int rmVInset = (int)(4 * g_dpiScale);
    int inX = rmEditX + rmInset;
    int inY = rmEditY + rmVInset;
    int inW = rmEditW - 2 * rmInset;
    int inH = rmEditH - 2 * rmVInset;

    g_rmInput = CreateWindowExW(0,
        L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        inX, inY, inW, inH,
        g_rmHwnd, (HMENU)(UINT_PTR)10, g_hInst, nullptr);
    if (g_rmInput) {
        // Match the themed reader font, sized to the box height.
        if (!g_rmEditFont) {
            int fontPx = inH - (int)(8 * g_dpiScale);
            if (fontPx < (int)(12 * g_dpiScale)) fontPx = (int)(12 * g_dpiScale);
            g_rmEditFont = CreateFontW(-fontPx, 0, 0, 0, FW_NORMAL,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                L"Segoe UI");
        }
        if (g_rmEditFont)
            SendMessage(g_rmInput, WM_SETFONT, (WPARAM)g_rmEditFont, TRUE);
        SendMessage(g_rmInput, EM_LIMITTEXT, 512, 0);
        // Set the initial text AFTER g_rmInput is assigned (creating the
        // EDIT with initial text fires EN_CHANGE during CreateWindowExW,
        // when g_rmInput isn't assigned yet).
        SetWindowTextW(g_rmInput, currentFriendly.c_str());
        g_rmText = currentFriendly;
        // Put the caret at the END so the user can edit/fix a typo rather
        // than replacing everything. (No select-all — that wiped the field
        // on the first keystroke.)
        int len = (int)currentFriendly.size();
        SendMessage(g_rmInput, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    }

    // OK on the LEFT, Cancel on the RIGHT — matches the plugin manager
    // popup's affirmative-left button order. Both use ButtonKind::Plugins
    // for the nexus_update artwork + hover glow.
    g_rmOkBtn = MkStdBtn(g_rmHwnd, L"OK", 1,
        btnRowX, btnRowY, physBtnW, physBtnH,
        true, ButtonKind::Plugins);
    g_rmCancelBtn = MkStdBtn(g_rmHwnd, L"Cancel", 2,
        btnRowX + physBtnW + physGap, btnRowY, physBtnW, physBtnH,
        true, ButtonKind::Plugins);
    // OK is the default action — Enter clicks it via IsDialogMessage.
    // Owner-draw buttons don't visualise BS_DEFPUSHBUTTON automatically,
    // but the behavior wiring still works.
    LONG_PTR okStyle = GetWindowLongPtr(g_rmOkBtn, GWL_STYLE);
    SetWindowLongPtr(g_rmOkBtn, GWL_STYLE, okStyle | BS_DEFPUSHBUTTON);

    EnableWindow(parent, FALSE);
    ShowWindow(g_rmHwnd, SW_SHOW);
    UpdateWindow(g_rmHwnd);
    SetActiveWindow(g_rmHwnd);
    SetFocus(g_rmInput);

    MSG msg;
    while (g_rmHwnd) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == 0 || got == -1) {
            if (got == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (g_rmHwnd && IsDialogMessageW(g_rmHwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_rmAccepted) {
        outNewName = g_rmResult;
        return true;
    }
    return false;
}

// ── v1.6: README picker ──────────────────────────────────────────────
// A dropdown of every plugin that has a readme (by friendly name where
// set, else DLL name). Selecting one opens it in the themed reader.
// Mirrors the excel picker's simple combobox layout.

namespace {

struct RmPickResult { int sel = -1; bool done = false; bool cancel = false; };
enum { RMP_COMBO = 300, RMP_OK = 301, RMP_CANCEL = 302 };

// The dll→readme pairs backing the current picker (index = combo item).
static std::vector<std::pair<wstring, wstring>> g_rmpEntries;

LRESULT CALLBACK RmPickProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COMMAND) {
        auto* r = (RmPickResult*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        WORD id = LOWORD(wp);
        if (r && id == RMP_OK) {
            HWND combo = GetDlgItem(hw, RMP_COMBO);
            r->sel = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
            r->done = true;
            DestroyWindow(hw);
        } else if (r && (id == RMP_CANCEL || id == IDCANCEL)) {
            r->cancel = true; r->done = true;
            DestroyWindow(hw);
        }
        return 0;
    }
    if (msg == WM_CLOSE) {
        auto* r = (RmPickResult*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        if (r) { r->cancel = true; r->done = true; }
        DestroyWindow(hw);
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

} // namespace

static void ShowReadmePicker(HWND pmHwnd) {
    g_rmpEntries = GetAllPluginReadmes();
    if (g_rmpEntries.empty()) {
        MessageBoxW(pmHwnd,
            L"No plugin READMEs are available yet. READMEs appear here after "
            L"installing a plugin that includes one.",
            L"READMEs", MB_OK | MB_ICONINFORMATION);
        return;
    }
    // Sort by display label for a stable list.
    std::sort(g_rmpEntries.begin(), g_rmpEntries.end(),
              [](const auto& a, const auto& b) {
                  wstring la = GetPluginFriendlyName(a.first);
                  if (la.empty()) la = a.first;
                  wstring lb = GetPluginFriendlyName(b.first);
                  if (lb.empty()) lb = b.first;
                  return _wcsicmp(la.c_str(), lb.c_str()) < 0;
              });

    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = RmPickProc;
        wc.hInstance     = g_hInst;
        wc.lpszClassName = L"AngirisReadmePicker";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassExW(&wc);
        reg = true;
    }

    int w = 460, h = 190;
    RECT pr; GetWindowRect(pmHwnd, &pr);
    int x = pr.left + ((pr.right - pr.left) - w) / 2;
    int y = pr.top  + ((pr.bottom - pr.top) - h) / 2;

    RmPickResult res;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME,  // topmost dropped: owned popup above owner only
        L"AngirisReadmePicker", L"Plugin READMEs",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h, pmHwnd, nullptr, g_hInst, nullptr);
    if (!dlg) return;
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&res);

    CreateWindowExW(0, L"STATIC", L"Select a plugin README to open:",
        WS_CHILD | WS_VISIBLE, 16, 14, w - 32, 20, dlg, nullptr, g_hInst, nullptr);

    HWND combo = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        16, 44, w - 32, 240, dlg, (HMENU)RMP_COMBO, g_hInst, nullptr);
    for (const auto& e : g_rmpEntries) {
        wstring label = GetPluginFriendlyName(e.first);
        if (label.empty()) label = e.first;
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)label.c_str());
    }
    SendMessageW(combo, CB_SETCURSEL, 0, 0);

    CreateWindowExW(0, L"BUTTON", L"Open",
        WS_CHILD | WS_VISIBLE, w - 210, 100, 90, 30,
        dlg, (HMENU)RMP_OK, g_hInst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE, w - 110, 100, 90, 30,
        dlg, (HMENU)RMP_CANCEL, g_hInst, nullptr);

    EnableWindow(pmHwnd, FALSE);
    MSG m;
    while (!res.done && GetMessageW(&m, nullptr, 0, 0)) {
        if (IsDialogMessageW(dlg, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    EnableWindow(pmHwnd, TRUE);
    SetActiveWindow(pmHwnd);

    if (res.cancel || res.sel < 0 || res.sel >= (int)g_rmpEntries.size())
        return;

    const auto& chosen = g_rmpEntries[res.sel];
    wstring full = chosen.second;
    if (full.size() > 1 && full[1] != L':')            // not absolute
        full = AppDir() + L"\\" + chosen.second;
    wstring title = GetPluginFriendlyName(chosen.first);
    if (title.empty()) title = chosen.first;
    ShowReadmeReader(pmHwnd, title, full);
}
