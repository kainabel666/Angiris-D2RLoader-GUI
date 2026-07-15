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
#include "plugin_config.h"     // v1.3: per-mod manifest mode + sweeps
#include "plugin_manifest.h"   // v1.3-E1: friendly-name lookup
#include "core.h"            // g_hInst, g_dpiScale
#include "scaling.h"         // S(), SF()
#include "colors.h"          // Tok::Gold, Tok::crBgPanel, etc.
#include "fonts.h"           // g_fNavSm, g_fBtn

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
static wstring g_pmModName;    // display name of the selected mod, or empty
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
constexpr int PM_BTN_H        = 34;
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
constexpr int RM_BTN_H     = 34;
constexpr int RM_BTN_GAP   = 12;

// Rename modal state. Like the plugin manager popup, these are all
// file-static — only one rename modal can be active at a time and it
// always nests inside the plugin manager's modal pump.
static HWND    g_rmHwnd      = nullptr;
static HWND    g_rmEdit      = nullptr;
static HWND    g_rmOkBtn     = nullptr;
static HWND    g_rmCancelBtn = nullptr;
static HBRUSH  g_rmEditBrush = nullptr;  // bg brush for WM_CTLCOLOREDIT
static wstring g_rmDllName;              // DLL being renamed (display only)
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

// ── Filesystem helpers ───────────────────────────────────────────────

// Best-effort CreateDirectory; ignores ERROR_ALREADY_EXISTS. Used so
// callers don't have to check the error code in normal flow.
static void EnsureDirExists(const wstring& dir) {
    CreateDirectoryW(dir.c_str(), nullptr);
}

// Scan a single plugins folder pair (active + Disabled subfolder) and
// append entries to g_pluginList. Creates the Disabled subfolder if
// it doesn't exist so the user has a consistent place to look on
// disk after launching the manager.
static void ScanPluginFolder(const wstring& baseDir, bool isGlobal) {
    // Skip entirely if the base folder doesn't exist (e.g. mod has no
    // .mpq subfolder, or the user hasn't created plugins\ in d2rPath).
    DWORD attr = GetFileAttributesW(baseDir.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) return;

    wstring disabledDir = baseDir + L"\\Disabled";
    EnsureDirExists(disabledDir);

    auto scanOne = [&](const wstring& dir, bool active) {
        WIN32_FIND_DATAW fd;
        wstring pattern = dir + L"\\*.dll";
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
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

// Populate g_pluginList for (mod, d2rPath). Mod plugins are listed
// first (they're more immediately relevant to the user's current
// session); global plugins come after.
static void ScanPlugins(const ModInfo* mod, const wstring& d2rPath) {
    g_pluginList.clear();

    // (M) entries — per-mod plugins. Per spec the path is literally
    // <modDir>\<folder>.mpq\Plugins regardless of whether the mod
    // also has a flat layout; mods using the flat layout simply have
    // no per-mod plugins.
    if (mod) {
        wstring modPlugins = mod->dir + L"\\" + mod->folder + L".mpq\\Plugins";
        ScanPluginFolder(modPlugins, /*isGlobal=*/false);
    }

    // (G) entries — global plugins.
    wstring globalPlugins = d2rPath + L"\\plugins";
    ScanPluginFolder(globalPlugins, /*isGlobal=*/true);
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

// v1.3-E1: build the displayed text for a plugin row. If the launcher's
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
        // and (read-only) Manifest rows ignore the click — the row may
        // still get the SELECTED highlight from stock listbox handling,
        // which is fine.
        if (row.kind != PMRow::Kind::Plugin) return;
        if (row.pluginIdx < 0
            || row.pluginIdx >= (int)g_pluginList.size()) return;
        g_pluginList[row.pluginIdx].isChecked =
            !g_pluginList[row.pluginIdx].isChecked;
        RECT r;
        if (SendMessage(hw, LB_GETITEMRECT, idx, (LPARAM)&r) != LB_ERR) {
            InvalidateRect(hw, &r, FALSE);
        }
    };

    switch (msg) {
    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int idx = (int)SendMessage(hw, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
        // ITEMFROMPOINT returns hi-word = 1 when outside any item.
        if (HIWORD(idx) == 0) {
            toggle(LOWORD(idx));
        }
        break;   // fall through to DefSubclassProc for normal selection
    }
    case WM_KEYDOWN:
        if (wp == VK_SPACE) {
            int idx = (int)SendMessage(hw, LB_GETCURSEL, 0, 0);
            if (idx != LB_ERR) { toggle(idx); return 0; }
        }
        break;

    case WM_CONTEXTMENU: {
        // v1.3-E2: right-click → Rename menu. lParam packs screen
        // coordinates. lParam == -1 means the user invoked the menu
        // via the keyboard (Shift+F10), in which case we use the
        // currently-focused row's rect as the anchor.
        POINT scr = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int rowIdx = -1;
        if (lp == (LPARAM)-1) {
            rowIdx = (int)SendMessage(hw, LB_GETCURSEL, 0, 0);
            if (rowIdx != LB_ERR) {
                RECT r;
                SendMessage(hw, LB_GETITEMRECT, rowIdx, (LPARAM)&r);
                scr.x = r.left;
                scr.y = r.bottom;
                ClientToScreen(hw, &scr);
            } else {
                rowIdx = -1;
            }
        } else {
            POINT cli = scr;
            ScreenToClient(hw, &cli);
            int packed = (int)SendMessage(hw, LB_ITEMFROMPOINT, 0,
                                          MAKELPARAM(cli.x, cli.y));
            if (HIWORD(packed) == 0) rowIdx = LOWORD(packed);
        }
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

        // Visually focus the right-clicked row so the user has clear
        // confirmation of which DLL is about to be renamed.
        SendMessage(hw, LB_SETCURSEL, rowIdx, 0);
        InvalidateRect(hw, nullptr, FALSE);

        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, L"Rename...");
        int cmd = TrackPopupMenu(menu,
                                 TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                 scr.x, scr.y, 0, hw, nullptr);
        DestroyMenu(menu);
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
static void PMDrawItem(DRAWITEMSTRUCT* di) {
    if ((int)di->itemID < 0
        || (int)di->itemID >= (int)g_pmRows.size()) return;
    const PMRow& row = g_pmRows[di->itemID];

    bool selected = (di->itemState & ODS_SELECTED) != 0;
    bool focused  = (di->itemState & ODS_FOCUS)    != 0;

    // ── Section header ─────────────────────────────────────────────
    if (row.kind == PMRow::Kind::SectionHeader) {
        // No selection highlight — headers should look fixed.
        HBRUSH bg = CreateSolidBrush(Tok::crBgDeep);
        FillRect(di->hDC, &di->rcItem, bg);
        DeleteObject(bg);

        // Subtle underline rule under the text so the header reads
        // as a section divider rather than a row.
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
        // No focus rect — headers shouldn't draw focus.
        return;
    }

    // Background — selected rows get a slightly lighter panel tone so
    // keyboard navigation is visible without losing readability.
    HBRUSH bg = CreateSolidBrush(selected ? Tok::crBgPanel : Tok::crBgDeep);
    FillRect(di->hDC, &di->rcItem, bg);
    DeleteObject(bg);

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

        // Focus rect still drawn so keyboard nav stays visible.
        if (focused) DrawFocusRect(di->hDC, &di->rcItem);
        return;
    }

    // ── Plugin row (toggleable; legacy behavior) ───────────────────
    if (row.kind != PMRow::Kind::Plugin) return;          // defensive
    if (row.pluginIdx < 0
        || row.pluginIdx >= (int)g_pluginList.size()) return;
    const PluginEntry& e = g_pluginList[row.pluginIdx];

    // Checkbox box at the left of the row.
    int boxSize = S(PM_CHECKBOX_SIZE);
    int boxX = di->rcItem.left + S(8);
    int boxY = di->rcItem.top
             + (di->rcItem.bottom - di->rcItem.top - boxSize) / 2;
    RECT boxR = { boxX, boxY, boxX + boxSize, boxY + boxSize };

    HBRUSH boxFill = CreateSolidBrush(e.isChecked
                                      ? Tok::crGoldBright
                                      : Tok::crBgPanel);
    FillRect(di->hDC, &boxR, boxFill);
    DeleteObject(boxFill);
    HPEN boxPen = CreatePen(PS_SOLID, 1,
                             e.isChecked ? Tok::crGold : Tok::crBronzeDim);
    HPEN oldPen = (HPEN)SelectObject(di->hDC, boxPen);
    HBRUSH oldBr = (HBRUSH)SelectObject(di->hDC, GetStockObject(NULL_BRUSH));
    Rectangle(di->hDC, boxR.left, boxR.top, boxR.right, boxR.bottom);
    SelectObject(di->hDC, oldPen);
    SelectObject(di->hDC, oldBr);
    DeleteObject(boxPen);

    // If checked, draw a small ink check mark inside the gold square.
    if (e.isChecked) {
        HPEN tickPen = CreatePen(PS_SOLID, 2, RGB(0x20, 0x18, 0x08));
        HPEN prevPen = (HPEN)SelectObject(di->hDC, tickPen);
        POINT pts[3] = {
            { boxR.left + boxSize / 5,       boxR.top + boxSize / 2 },
            { boxR.left + boxSize * 2 / 5,   boxR.top + boxSize * 3 / 4 },
            { boxR.right - boxSize / 5,      boxR.top + boxSize / 4 },
        };
        Polyline(di->hDC, pts, 3);
        SelectObject(di->hDC, prevPen);
        DeleteObject(tickPen);
    }

    // Label text — filename only (no (G)/(M) tag; headers carry that).
    // v1.3-E1: substitutes the friendly name from plugin_manifest.json
    // if one is defined for this DLL filename.
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, Tok::crText);
    RECT textR = di->rcItem;
    textR.left = boxR.right + S(10);
    textR.right -= S(8);
    wstring label = BuildDisplayLabel(e.fileName);
    DrawTextW(di->hDC, label.c_str(), -1, &textR,
              DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // Focus rectangle when the row is focused.
    if (focused) {
        DrawFocusRect(di->hDC, &di->rcItem);
    }
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

// ── Window proc ──────────────────────────────────────────────────────

static LRESULT CALLBACK PluginManagerProc(HWND hw, UINT msg,
                                          WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);

        // Solid dark fill.
        HBRUSH bgBr = CreateSolidBrush(Tok::crBgPanel);
        FillRect(hdc, &rc, bgBr);
        DeleteObject(bgBr);

        // Double border: outer gold, inner bronze.
        HPEN outPen = CreatePen(PS_SOLID, 2, Tok::crGold);
        HPEN inPen  = CreatePen(PS_SOLID, 1, Tok::crBronzeDim);
        HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
        HPEN op = (HPEN)SelectObject(hdc, outPen);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, nb);
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(hdc, inPen);
        Rectangle(hdc, rc.left + 3, rc.top + 3, rc.right - 3, rc.bottom - 3);
        SelectObject(hdc, op);
        SelectObject(hdc, ob);
        DeleteObject(outPen);
        DeleteObject(inPen);

        // Title — uses g_fNavSm + GDI+ so AA + ClearType matches the
        // launcher's body. Falls back silently if fonts haven't loaded.
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        Gdiplus::SolidBrush titleBr(Tok::Gold);
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        wstring title = L"Plugin Manager";
        if (!g_pmModName.empty()) title += L" \u2014 " + g_pmModName;

        Gdiplus::Font* tf = g_fNavSm;
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
            RECT cr; GetClientRect(hw, &cr);
            int listX = (int)(PM_PAD * g_dpiScale);
            int listY = (int)((PM_TITLE_H + PM_LIST_PAD_TOP) * g_dpiScale);
            int listW = cr.right - 2 * listX;
            int listH = cr.bottom - listY
                      - (int)((PM_PAD + PM_BTN_H + PM_LIST_PAD_BOT) * g_dpiScale);
            Gdiplus::RectF msgRect((REAL)listX, (REAL)listY,
                                   (REAL)listW, (REAL)listH);
            Gdiplus::SolidBrush msgBr(Tok::Gold);
            if (tf) {
                g.DrawString(g_pmEmptyMessage.c_str(), -1, tf,
                             msgRect, &sf, &msgBr);
            }
        }

        EndPaint(hw, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
        if (di->CtlID == 100) {              // listbox row
            PMDrawItem(di);
            return TRUE;
        }
        if (di->CtlID == 1) { PMDrawButton(di, L"Save Selection"); return TRUE; }
        if (di->CtlID == 2) {
            // v1.3: in manifest mode the popup is read-only — relabel
            // the dismiss button "Close" so the user doesn't expect
            // a "Cancel any changes I made" semantic. In legacy mode
            // the label stays "Cancel".
            PMDrawButton(di, g_pmConfigMode ? L"Close" : L"Cancel");
            return TRUE;
        }
        return 0;
    }

    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, Tok::crBgDeep);
        SetTextColor(hdc, Tok::crText);
        static HBRUSH bb = nullptr;
        if (!bb) bb = CreateSolidBrush(Tok::crBgDeep);
        return (LRESULT)bb;
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
            // with grey-outs for whatever's still missing.
            wstring modPluginsActive = selectedMod->dir + L"\\"
                                     + selectedMod->folder + L".mpq\\Plugins";
            configFound = RunPluginRecoverySweep(modPluginsActive,
                                                   d2rPath,
                                                   manifest.plugins);
            BuildConfigRows(manifest, configFound);
        }
    } else {
        // Legacy mode: scan disk, build header-grouped row list.
        ScanPlugins(selectedMod, d2rPath);
        BuildLegacyRows();
    }

    // v1.3-E1 discovery: pre-populate plugin_manifest.json with empty
    // entries for every DLL we just learned about. Existing entries —
    // including ones whose DLL is no longer found — are never modified.
    // This makes it easy for users to hand-edit the manifest: they just
    // open it in a text editor and fill in friendly names beside
    // pre-populated keys.
    //
    // Scope: every DLL in the four plugin folders relevant to the active
    // mod, PLUS every entry in the per-mod plugin_config.json if one
    // exists (those may be missing from disk but the user might still
    // want to assign friendly names so future renames are ready).
    //
    // Scanning all four folders independently rather than reading
    // g_pluginList in legacy mode + manifest.plugins in config mode
    // means disabled DLLs are discovered the same way in both modes —
    // there's no mode-specific blind spot.
    {
        auto enumerateDlls = [](const wstring& folder, vector<wstring>& out) {
            DWORD attr = GetFileAttributesW(folder.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES) return;
            if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) return;
            WIN32_FIND_DATAW fd;
            wstring pattern = folder + L"\\*.dll";
            HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return;
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                out.push_back(fd.cFileName);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        };

        vector<wstring> seen;

        // Per-mod config entries first (if any) — covers manifest-only
        // DLLs that aren't on disk yet but the modder declared anyway.
        if (manifest.present) {
            for (const auto& p : manifest.plugins) seen.push_back(p);
        }

        // Global plugins: active + disabled. Both are scanned in both
        // modes (in config mode, MoveGlobalPluginsToDisabled has just
        // emptied the active folder, but the disabled folder now holds
        // those plugins — discovery still picks them up).
        enumerateDlls(d2rPath + L"\\plugins",            seen);
        enumerateDlls(d2rPath + L"\\plugins\\Disabled",  seen);

        // Mod-local plugins: active + disabled. Only when a mod is
        // selected; no .mpq folder = no scan, gracefully.
        if (selectedMod) {
            wstring modPlugins = selectedMod->dir + L"\\"
                               + selectedMod->folder + L".mpq\\Plugins";
            enumerateDlls(modPlugins,                seen);
            enumerateDlls(modPlugins + L"\\Disabled", seen);
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
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"AngirisPluginManager",
        L"Plugin Manager",
        WS_POPUP | WS_VISIBLE,
        x, y, physW, physH,
        parent, nullptr, g_hInst, nullptr);
    if (!g_pmHwnd) return;

    // Owner-drawn LISTBOX. Sized to fill the area between the title
    // band and the button row, padded by PM_PAD on each side. Skipped
    // in empty-manifest mode — WM_PAINT renders the message instead.
    int listX = (int)(PM_PAD * g_dpiScale);
    int listY = (int)((PM_TITLE_H + PM_LIST_PAD_TOP) * g_dpiScale);
    int listW = physW - 2 * listX;
    int listH = physH - listY
              - (int)((PM_PAD + PM_BTN_H + PM_LIST_PAD_BOT) * g_dpiScale);

    if (!g_pmConfigEmpty) {
        g_pmList = CreateWindowExW(WS_EX_CLIENTEDGE,
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
        // Single Close button, centered. Reuse the Cancel control ID
        // (2) so the existing WM_COMMAND handler dismisses it for free.
        int btnX = (physW - physBtnW) / 2;
        g_pmCancelBtn = CreateWindowW(L"BUTTON", L"Close",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            btnX, btnRowY, physBtnW, physBtnH,
            g_pmHwnd, (HMENU)(UINT_PTR)2, g_hInst, nullptr);
    } else {
        int btnRowW   = physBtnW * 2 + physGap;
        int btnRowX   = (physW - btnRowW) / 2;
        int cancelX   = btnRowX;
        int saveX     = btnRowX + physBtnW + physGap;

        g_pmCancelBtn = CreateWindowW(L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            cancelX, btnRowY, physBtnW, physBtnH,
            g_pmHwnd, (HMENU)(UINT_PTR)2, g_hInst, nullptr);
        g_pmSaveBtn = CreateWindowW(L"BUTTON", L"Save Selection",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            saveX, btnRowY, physBtnW, physBtnH,
            g_pmHwnd, (HMENU)(UINT_PTR)1, g_hInst, nullptr);
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

        // Background fill — same panel tone the plugin manager uses
        // so the two popups feel like one design language.
        HBRUSH bg = CreateSolidBrush(Tok::crBgDeep);
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        // Outer border for "modal" weight.
        HPEN borderPen = CreatePen(PS_SOLID, 1, Tok::crBronzeDim);
        HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
        HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBr);
        DeleteObject(borderPen);

        // GDI+ title text — bigger and gold, like the plugin manager.
        Gdiplus::Graphics g(hdc);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        Gdiplus::SolidBrush titleBr(Tok::Gold);
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        wstring title = L"Rename " + g_rmDllName;
        Gdiplus::Font* tf = g_fNavSm;
        if (tf) {
            g.DrawString(title.c_str(), -1, tf,
                Gdiplus::RectF((REAL)rc.left, (REAL)S(8),
                               (REAL)(rc.right - rc.left),
                               (REAL)S(RM_TITLE_H - 8)),
                &sf, &titleBr);
        }

        // Short descriptor between title and edit field.
        Gdiplus::SolidBrush bodyBr(Gdiplus::Color(190, 200, 200, 200));
        if (tf) {
            int labelY = (int)((RM_TITLE_H + 6) * g_dpiScale);
            int labelH = (int)(RM_LABEL_H * g_dpiScale);
            g.DrawString(L"Display name in the launcher:", -1, tf,
                Gdiplus::RectF((REAL)((int)(RM_PAD * g_dpiScale)),
                               (REAL)labelY,
                               (REAL)(rc.right - 2 * (int)(RM_PAD * g_dpiScale)),
                               (REAL)labelH),
                &sf, &bodyBr);
        }

        EndPaint(hw, &ps);
        return 0;
    }

    case WM_CTLCOLOREDIT: {
        // Theme the input field — dark panel bg, light gold text.
        HDC hdcEdit = (HDC)wp;
        SetBkColor(hdcEdit, Tok::crBgPanel);
        SetTextColor(hdcEdit, Tok::crGoldBright);
        if (!g_rmEditBrush) {
            g_rmEditBrush = CreateSolidBrush(Tok::crBgPanel);
        }
        return (LRESULT)g_rmEditBrush;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
        if (di->CtlID == 1) { PMDrawButton(di, L"OK");     return TRUE; }
        if (di->CtlID == 2) { PMDrawButton(di, L"Cancel"); return TRUE; }
        return 0;
    }

    case WM_COMMAND: {
        WORD id   = LOWORD(wp);
        WORD code = HIWORD(wp);
        if (code == BN_CLICKED) {
            if (id == 1) {
                // OK — capture text. Trim leading/trailing whitespace
                // so accidental trailing spaces don't show up in the
                // rendered "Friendly (dll)" label.
                wchar_t buf[1024] = {};
                GetWindowTextW(g_rmEdit, buf, 1024);
                wstring v = buf;
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
        // Esc cancels regardless of focus. IsDialogMessage handles
        // VK_RETURN → click default button (OK), so we don't need an
        // explicit case for it here.
        if (wp == VK_ESCAPE) {
            g_rmAccepted = false;
            DestroyWindow(hw);
            return 0;
        }
        break;

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
        if (g_rmEditBrush) {
            DeleteObject(g_rmEditBrush);
            g_rmEditBrush = nullptr;
        }
        g_rmHwnd      = nullptr;
        g_rmEdit      = nullptr;
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
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"AngirisRenameModal",
        L"Rename plugin",
        WS_POPUP | WS_VISIBLE,
        x, y, physW, physH,
        parent, nullptr, g_hInst, nullptr);
    if (!g_rmHwnd) return false;

    // Edit control — themed via WM_CTLCOLOREDIT. WS_TABSTOP so Tab
    // navigates Edit → OK → Cancel. ES_AUTOHSCROLL lets long names
    // scroll horizontally without breaking the single-line layout.
    int editX = (int)(RM_PAD * g_dpiScale);
    int editY = (int)((RM_TITLE_H + RM_LABEL_H + 10) * g_dpiScale);
    int editW = physW - 2 * editX;
    int editH = (int)(RM_EDIT_H * g_dpiScale);

    g_rmEdit = CreateWindowExW(WS_EX_CLIENTEDGE,
        L"EDIT", currentFriendly.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        editX, editY, editW, editH,
        g_rmHwnd, (HMENU)(UINT_PTR)10, g_hInst, nullptr);
    if (g_rmEdit) {
        // Pre-select all text so the user can immediately type a
        // replacement. -1 from start of selection means "to end".
        SendMessage(g_rmEdit, EM_SETSEL, 0, -1);
        // Cap input length defensively so a paste-bomb can't produce
        // a megabyte-long row label.
        SendMessage(g_rmEdit, EM_LIMITTEXT, 512, 0);
    }

    int btnRowY = physH - (int)((RM_PAD + RM_BTN_H) * g_dpiScale);
    int physBtnW = (int)(RM_BTN_W * g_dpiScale);
    int physBtnH = (int)(RM_BTN_H * g_dpiScale);
    int physGap  = (int)(RM_BTN_GAP * g_dpiScale);
    int btnRowW  = physBtnW * 2 + physGap;
    int btnRowX  = (physW - btnRowW) / 2;

    // Cancel on the LEFT, OK on the RIGHT — matches the platform
    // convention and the plugin manager popup.
    g_rmCancelBtn = CreateWindowW(L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        btnRowX, btnRowY, physBtnW, physBtnH,
        g_rmHwnd, (HMENU)(UINT_PTR)2, g_hInst, nullptr);
    g_rmOkBtn = CreateWindowW(L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
        btnRowX + physBtnW + physGap, btnRowY, physBtnW, physBtnH,
        g_rmHwnd, (HMENU)(UINT_PTR)1, g_hInst, nullptr);

    SetFocus(g_rmEdit);

    EnableWindow(parent, FALSE);
    ShowWindow(g_rmHwnd, SW_SHOW);
    UpdateWindow(g_rmHwnd);

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
