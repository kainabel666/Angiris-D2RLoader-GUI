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
#include "plugin_manifest.h"   // v1.3: friendly-name lookup
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
static HWND    g_pmList      = nullptr;
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
constexpr int PM_TITLE_H      = 40;
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
static HWND    g_rmInput     = nullptr;  // hidden off-screen EDIT — keyboard capture only
static HWND    g_rmOkBtn     = nullptr;
static HWND    g_rmCancelBtn = nullptr;
static wstring g_rmDllName;              // DLL being renamed (display only)
static wstring g_rmText;                 // current input text (mirrored from g_rmInput)
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
    bool hasMod    = firstGlobal > 0;
    bool hasGlobal = firstGlobal < g_pluginList.size();

    if (hasMod) {
        PMRow h; h.kind = PMRow::Kind::SectionHeader; h.text = L"Mod plugins";
        g_pmRows.push_back(h);
        for (size_t i = 0; i < firstGlobal; ++i) {
            PMRow r;
            r.kind      = PMRow::Kind::Plugin;
            r.pluginIdx = (int)i;
            r.text      = g_pluginList[i].fileName;
            g_pmRows.push_back(r);
        }
    }
    if (hasGlobal) {
        PMRow h; h.kind = PMRow::Kind::SectionHeader; h.text = L"Global plugins";
        g_pmRows.push_back(h);
        for (size_t i = firstGlobal; i < g_pluginList.size(); ++i) {
            PMRow r;
            r.kind      = PMRow::Kind::Plugin;
            r.pluginIdx = (int)i;
            r.text      = g_pluginList[i].fileName;
            g_pmRows.push_back(r);
        }
    }
}

// Manifest mode: g_pluginList stays empty (no toggleable state). Build
// one "Mod plugins" header followed by one Manifest row per manifest
// entry, marking entries whose corresponding `found` flag is false so
// PMDrawItem can grey them out.
static void BuildConfigRows(const PluginConfig& mf,
                              const vector<bool>&   found) {
    g_pmRows.clear();
    if (mf.plugins.empty()) return;   // empty manifest → no rows; WM_PAINT handles the message

    PMRow h; h.kind = PMRow::Kind::SectionHeader; h.text = L"Mod plugins";
    g_pmRows.push_back(h);

    for (size_t i = 0; i < mf.plugins.size(); ++i) {
        PMRow r;
        r.kind      = PMRow::Kind::Manifest;
        r.text      = mf.plugins[i];
        r.isMissing = (i < found.size()) ? !found[i] : true;
        g_pmRows.push_back(r);
    }
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
        if (idx < 0 || idx >= (int)g_pmRows.size()) return;
        const PMRow& row = g_pmRows[idx];
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
        if (rowIdx < 0 || rowIdx >= (int)g_pmRows.size()) return 0;

        // Section headers can't be renamed — only plugin filename rows
        // (legacy or manifest-mode read-only entries) are valid targets.
        const PMRow& row = g_pmRows[rowIdx];
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
static void PMDrawItem(DRAWITEMSTRUCT* di) {
    if ((int)di->itemID < 0
        || (int)di->itemID >= (int)g_pmRows.size()) return;
    const PMRow& row = g_pmRows[di->itemID];

    bool selected = (di->itemState & ODS_SELECTED) != 0;
    bool focused  = (di->itemState & ODS_FOCUS)    != 0;

    // Compute this row's offset within the popup client area so we can
    // sample bg_stone.png at the same coordinates used by the popup's
    // own WM_PAINT (which crops the stone at 40,40 into rect(0,0,W,H)).
    // The listbox lives at (PM_PAD, PM_TITLE_H + PM_LIST_PAD_TOP), so
    // adding those to di->rcItem gives us the row's popup coords.
    int listX_phys = (int)(PM_PAD * g_dpiScale);
    int listY_phys = (int)((PM_TITLE_H + PM_LIST_PAD_TOP) * g_dpiScale);
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
    // before (manifest-empty mode) g_pmList may be null — in that case a
    // repaint of the popup shows the message; nothing to refill.
    if (g_pmList) {
        SendMessageW(g_pmList, LB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < g_pmRows.size(); ++i)
            SendMessageW(g_pmList, LB_ADDSTRING, 0,
                         (LPARAM)g_pmRows[i].text.c_str());
        SendMessageW(g_pmList, LB_SETITEMHEIGHT, 0,
                     (LPARAM)(int)(PM_ROW_H * g_dpiScale));
        InvalidateRect(g_pmList, nullptr, TRUE);
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

            // Title — uses g_fNavSm + GDI+ so AA + ClearType matches the
            // launcher's body. Falls back silently if fonts haven't loaded.
            Gdiplus::SolidBrush titleBr(Tok::Gold);
            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentCenter);
            sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

            wstring title = L"Plugin Manager";
            if (!g_pmModName.empty()) title += L" \u2014 " + g_pmModName;

            Gdiplus::Font* tf = g_fModName ? g_fModName : g_fNavSm;
            if (tf) {
                g.DrawString(title.c_str(), -1, tf,
                    Gdiplus::RectF((REAL)rc.left, (REAL)S(10),
                                   (REAL)(rc.right - rc.left),
                                   (REAL)S(PM_TITLE_H - 10)),
                    &sf, &titleBr);
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
        if (di->CtlID == 100) {              // listbox row
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
        if (g_pmList) {
            RemoveWindowSubclass(g_pmList, PMListSubclass, 1);
        }
        g_pmHwnd      = nullptr;
        g_pmList      = nullptr;
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

    // Owner-drawn LISTBOX. Sized to fill the area between the title
    // band and the button row, padded by PM_PAD on each side. Skipped
    // in empty-manifest mode — WM_PAINT renders the message instead.
    int listX = (int)(PM_PAD * g_dpiScale);
    int listY = (int)((PM_TITLE_H + PM_LIST_PAD_TOP) * g_dpiScale);
    int listW = physW - 2 * listX;
    int listH = physH - listY
              - (int)((PM_PAD + PM_BTN_H + PM_LIST_PAD_BOT) * g_dpiScale);

    if (!g_pmConfigEmpty) {
        g_pmList = CreateWindowExW(0,   // v1.3: no CLIENTEDGE — stone bg + parent chrome only
            L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL
                | LBS_OWNERDRAWFIXED | LBS_NOTIFY | LBS_HASSTRINGS,
            listX, listY, listW, listH,
            g_pmHwnd, (HMENU)(UINT_PTR)100, g_hInst, nullptr);

        // Populate with placeholder strings so the listbox knows how many
        // items it has. The owner-draw path reads from g_pmRows by index
        // — the string content here doesn't actually paint.
        for (size_t i = 0; i < g_pmRows.size(); ++i) {
            SendMessageW(g_pmList, LB_ADDSTRING, 0,
                         (LPARAM)g_pmRows[i].text.c_str());
        }
        // Tell the listbox each item is PM_ROW_H tall.
        SendMessage(g_pmList, LB_SETITEMHEIGHT, 0,
                    (LPARAM)(int)(PM_ROW_H * g_dpiScale));

        // Subclass for click-to-toggle + space-to-toggle.
        SetWindowSubclass(g_pmList, PMListSubclass, 1, 0);
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
        // Manifest mode: Close + READMEs, side by side, centered. (READMEs
        // must share the bottom row — placing it above would put it behind
        // the listbox, which fills the space up to this row.)
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

            // Input text — pale gold, left-aligned, vertically centered.
            // Static caret drawn as a thin gold vertical bar at the end
            // of whatever's been typed.
            {
                Gdiplus::Font* inputFont = g_fBtn ? g_fBtn : g_fNavSm;
                int textPad = S(10);
                Gdiplus::RectF textRect((REAL)(editX + textPad),
                                        (REAL)editY,
                                        (REAL)(editW - 2 * textPad),
                                        (REAL)editH);
                Gdiplus::SolidBrush textBr(Tok::GoldBright);
                Gdiplus::StringFormat sfIn;
                sfIn.SetAlignment(Gdiplus::StringAlignmentNear);
                sfIn.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                sfIn.SetFormatFlags(sfIn.GetFormatFlags()
                                    | Gdiplus::StringFormatFlagsNoWrap);
                if (inputFont && !g_rmText.empty()) {
                    g.DrawString(g_rmText.c_str(), -1, inputFont,
                                 textRect, &sfIn, &textBr);
                }

                // Caret: measure text width, draw a 2px vertical bar
                // at the trailing edge. Empty input → caret at leftmost
                // text position.
                if (inputFont) {
                    Gdiplus::REAL caretRelX = 0;
                    if (!g_rmText.empty()) {
                        Gdiplus::RectF bbox;
                        g.MeasureString(g_rmText.c_str(), -1, inputFont,
                                        Gdiplus::PointF(textRect.X, textRect.Y),
                                        &sfIn, &bbox);
                        caretRelX = bbox.Width;
                    }
                    int caretX = (int)(textRect.X + caretRelX);
                    int caretTop = editY + S(6);
                    int caretH = editH - S(12);
                    g.FillRectangle(&textBr, caretX, caretTop,
                                    S(2), caretH);
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

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
        // OK / Cancel buttons — nexus_update art, hover glow, click shrink.
        if (PaintOwnerDrawButton(di)) return TRUE;
        return 0;
    }

    case WM_COMMAND: {
        WORD id   = LOWORD(wp);
        WORD code = HIWORD(wp);
        // Hidden EDIT (id=10) is our keyboard-capture surface. Its
        // EN_CHANGE fires whenever text changes; mirror into g_rmText
        // so the paint pass shows the updated content.
        if (id == 10 && code == EN_CHANGE) {
            wchar_t buf[1024] = {};
            GetWindowTextW(g_rmInput, buf, 1024);
            g_rmText = buf;
            // Invalidate ONLY the input-text box, not the whole window.
            // A full-window InvalidateRect repainted the owner-drawn OK /
            // Cancel buttons on every keystroke, making them flicker. The
            // edit rect mirrors the paint case's editX/Y/W/H computation.
            RECT cr; GetClientRect(hw, &cr);
            int editX = (int)(RM_PAD * g_dpiScale);
            int editY = (int)((RM_TITLE_H + RM_LABEL_H + 8) * g_dpiScale);
            int editW = cr.right - 2 * editX;
            int editH = (int)(RM_EDIT_H * g_dpiScale);
            RECT inputRc = { editX, editY, editX + editW, editY + editH };
            InvalidateRect(hw, &inputRc, FALSE);
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
        // handling. Character input + backspace go to the hidden EDIT
        // natively (its EN_CHANGE mirrors into g_rmText).
        if (wp == VK_ESCAPE) {
            g_rmAccepted = false;
            DestroyWindow(hw);
            return 0;
        }
        break;

    case WM_LBUTTONDOWN:
        // Keep focus on the hidden EDIT so typing continues to work
        // after a click anywhere in the modal.
        if (g_rmInput) SetFocus(g_rmInput);
        break;

    case WM_ACTIVATE:
        // Restore focus to the hidden EDIT when the modal becomes
        // active (e.g. bringing the launcher back from the background).
        if (LOWORD(wp) != WA_INACTIVE && g_rmInput) {
            SetFocus(g_rmInput);
        }
        break;

    case WM_SETFOCUS:
        // Forward any focus the modal receives to the hidden EDIT.
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
    g_rmInput = CreateWindowExW(0,
        L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        -100, -100, 1, 1,
        g_rmHwnd, (HMENU)(UINT_PTR)10, g_hInst, nullptr);
    if (g_rmInput) {
        SendMessage(g_rmInput, EM_LIMITTEXT, 512, 0);
        // Set the initial text AFTER g_rmInput is assigned. Creating the
        // EDIT with initial text fires EN_CHANGE during CreateWindowExW —
        // at which point g_rmInput isn't assigned yet, so the EN_CHANGE
        // handler reads a stale handle and wipes g_rmText. Setting it here
        // fires EN_CHANGE with g_rmInput valid, correctly mirroring the
        // pre-populated friendly name into g_rmText for the paint pass.
        SetWindowTextW(g_rmInput, currentFriendly.c_str());
        g_rmText = currentFriendly;   // ensure the buffer matches immediately
        // Select-all so the first keystroke replaces the pre-populated
        // friendly name.
        SendMessage(g_rmInput, EM_SETSEL, 0, -1);
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
