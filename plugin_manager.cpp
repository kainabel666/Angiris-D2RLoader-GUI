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

// ── Window / state (file-local) ──────────────────────────────────────

static std::vector<PluginEntry> g_pluginList;
static HWND    g_pmHwnd      = nullptr;
static HWND    g_pmList      = nullptr;
static HWND    g_pmSaveBtn   = nullptr;
static HWND    g_pmCancelBtn = nullptr;
static wstring g_pmModName;    // display name of the selected mod, or empty
static bool    g_pmClassReg   = false;

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

// ── Listbox subclass: click-anywhere-in-row toggles, space toggles ───
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
        if (idx < 0 || idx >= (int)g_pluginList.size()) return;
        g_pluginList[idx].isChecked = !g_pluginList[idx].isChecked;
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
    }
    return DefSubclassProc(hw, msg, wp, lp);
}

// ── Owner-draw rendering ─────────────────────────────────────────────

static void PMDrawItem(DRAWITEMSTRUCT* di) {
    if ((int)di->itemID < 0
        || (int)di->itemID >= (int)g_pluginList.size()) return;
    const PluginEntry& e = g_pluginList[di->itemID];

    bool selected = (di->itemState & ODS_SELECTED) != 0;
    bool focused  = (di->itemState & ODS_FOCUS)    != 0;

    // Background — selected rows get a slightly lighter panel tone so
    // keyboard navigation is visible without losing readability.
    HBRUSH bg = CreateSolidBrush(selected ? Tok::crBgPanel : Tok::crBgDeep);
    FillRect(di->hDC, &di->rcItem, bg);
    DeleteObject(bg);

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

    // Label text — "MyPlugin.dll (G)" or "MyPlugin.dll (M)".
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, Tok::crText);
    wstring label = e.fileName + (e.isGlobal ? L" (G)" : L" (M)");
    RECT textR = di->rcItem;
    textR.left = boxR.right + S(10);
    textR.right -= S(8);
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
        if (di->CtlID == 2) { PMDrawButton(di, L"Cancel");          return TRUE; }
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
    ScanPlugins(selectedMod, d2rPath);

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
    // band and the button row, padded by PM_PAD on each side.
    int listX = (int)(PM_PAD * g_dpiScale);
    int listY = (int)((PM_TITLE_H + PM_LIST_PAD_TOP) * g_dpiScale);
    int listW = physW - 2 * listX;
    int listH = physH - listY
              - (int)((PM_PAD + PM_BTN_H + PM_LIST_PAD_BOT) * g_dpiScale);

    g_pmList = CreateWindowExW(WS_EX_CLIENTEDGE,
        L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL
            | LBS_OWNERDRAWFIXED | LBS_NOTIFY | LBS_HASSTRINGS,
        listX, listY, listW, listH,
        g_pmHwnd, (HMENU)(UINT_PTR)100, g_hInst, nullptr);

    // Populate with placeholder strings so the listbox knows how many
    // items it has. The owner-draw path reads from g_pluginList by
    // index — the string content here doesn't actually paint.
    for (size_t i = 0; i < g_pluginList.size(); ++i) {
        SendMessageW(g_pmList, LB_ADDSTRING, 0,
                     (LPARAM)g_pluginList[i].fileName.c_str());
    }
    // Tell the listbox each item is PM_ROW_H tall.
    SendMessage(g_pmList, LB_SETITEMHEIGHT, 0,
                (LPARAM)(int)(PM_ROW_H * g_dpiScale));

    // Subclass for click-to-toggle + space-to-toggle.
    SetWindowSubclass(g_pmList, PMListSubclass, 1, 0);

    // Two themed owner-draw buttons at the bottom.
    int btnRowY = physH
                - (int)((PM_PAD + PM_BTN_H) * g_dpiScale);
    int physBtnW = (int)(PM_BTN_W * g_dpiScale);
    int physBtnH = (int)(PM_BTN_H * g_dpiScale);
    int physGap  = (int)(PM_BTN_GAP * g_dpiScale);

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
