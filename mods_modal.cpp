// ═══════════════════════════════════════════════════════════════════════
//  mods_modal.cpp — Mods icon grid + modinfo editor (v1.6)
// ═══════════════════════════════════════════════════════════════════════
//
//  1. ShowModsModal — an icon grid of every folder under the mods dir
//     (btn_folder.png icons, up to 4 per row, wrapping). Mod name under the
//     icon; folders with no modinfo.json show the name greyed + red
//     "No Mod Info" beneath. LEFT-CLICK a folder opens the modinfo editor
//     (view/edit for existing, create for missing). Icons grow on hover,
//     shrink on click. Themed on bg_stone; user font, DPI/UI-scale aware.
//  2. The modinfo editor — a form of all modinfo fields with a read-only ⇄
//     edit state machine. Back/Cancel/Save are all persistent (enabled/
//     disabled rather than created/destroyed, to avoid button flashing).

#include "mods_modal.h"
#include "core.h"        // g_hInst, g_scale, JsonStr, EscapeJson, ReadTextFile, WriteTextFile
#include "config.h"      // g_cfg.d2rPath, g_cfg.fontName
#include "colors.h"      // Tok::Gold, Tok::Bronze
#include "fonts.h"       // g_fModName, g_fNavSm, MakeReaderFont, g_userFontFamilyOverride
#include "assets.h"      // AssetImage, DrawButton9Slice
#include "buttons.h"     // MkStdBtn, PaintOwnerDrawButton, ButtonKind
#include "scaling.h"     // S()
#include "mod_scan.h"    // ModInfo
#include "layout.h"      // RefreshMods

#include <windows.h>
#include <windowsx.h>    // GET_X_LPARAM / GET_Y_LPARAM
#include <vector>

using namespace Gdiplus;
using std::wstring;
using std::vector;

// ─────────────────────────────────────────────────────────────────────
//  Field model.
// ─────────────────────────────────────────────────────────────────────
namespace {

struct FieldDef {
    const wchar_t* label;
    const wchar_t* key;
    bool           required;
    bool           multiline;
    bool           subtitleBefore;  // draw "Mod Updates" subtitle above this row
};

// Order: name+savepath (required), the optionals, banner (moved up under
// discord), then the two update fields under a "Mod Updates" subtitle with
// shortened labels (GitHub / Manifest).
const FieldDef kFields[] = {
    { L"name",        L"name",            true,  false, false },
    { L"savepath",    L"savepath",        true,  false, false },
    { L"title",       L"title",           false, false, false },
    { L"author",      L"author",          false, false, false },
    { L"version",     L"version",         false, false, false },
    { L"description", L"description",     false, true,  false },
    { L"overview",    L"overview",        false, true,  false },
    { L"docs",        L"docs",            false, false, false },
    { L"website",     L"website",         false, false, false },
    { L"discord",     L"discord",         false, false, false },
    { L"banner",      L"banner",          false, false, false },
    { L"GitHub",      L"update_github",   false, false, true  },  // subtitle above
    { L"Manifest",    L"update_manifest", false, false, false },
};
constexpr int kFieldCount = (int)(sizeof(kFields) / sizeof(kFields[0]));

// modinfo.json writer (mirrors config.cpp's EscapeJson approach).
wstring BuildModinfoJson(const wstring values[kFieldCount]) {
    wstring j = L"{\n";
    bool first = true;
    for (int i = 0; i < kFieldCount; ++i) {
        const wstring& v = values[i];
        if (v.empty() && !kFields[i].required) continue;
        if (!first) j += L",\n";
        first = false;
        j += L"  \"";
        j += kFields[i].key;
        j += L"\": \"";
        j += EscapeJson(v);
        j += L"\"";
    }
    j += L"\n}\n";
    return j;
}

// A folder row in the mods grid.
struct ModFolderRow {
    wstring folder;
    wstring displayName;
    bool    hasModinfo;
    wstring modinfoPath;
    RECT    rc;            // grid cell rect (physical px)
};

// ── Mods grid modal state ──
constexpr int MM_W        = 620;
constexpr int MM_H        = 720;
constexpr int MM_TITLE_TOP= 14;
constexpr int MM_TITLE_H  = 32;
constexpr int MM_NOTE_TOP = 50;
constexpr int MM_GRID_TOP = 84;
constexpr int MM_PAD      = 24;
constexpr int MM_BTN_W    = 160;
constexpr int MM_BTN_H    = 58;
constexpr int MM_BTN_BOT  = 18;

constexpr int MM_CELL_W   = 130;
constexpr int MM_CELL_H   = 168;   // icon + up to 4 wrapped name lines + "No Mod Info"
constexpr int MM_ICON_BOX = 64;
constexpr int MM_NAME_H   = 68;    // 4 lines of name text
constexpr int MM_NOINFO_H = 20;    // red "No Mod Info" line
constexpr int MM_COLS_MAX = 4;

constexpr int MM_IDC_BACK = 101;

HWND    g_mmHwnd = nullptr;
HWND    g_mmBack = nullptr;
bool    g_mmReg  = false;
vector<ModFolderRow> g_mmRows;
int     g_mmHover = -1;
int     g_mmDown  = -1;

wstring ModsDir() { return g_cfg.d2rPath + L"\\mods"; }

wstring FindModinfoIn(const wstring& modFolderPath) {
    wstring flat = modFolderPath + L"\\modinfo.json";
    if (GetFileAttributesW(flat.c_str()) != INVALID_FILE_ATTRIBUTES)
        return flat;
    WIN32_FIND_DATAW fd;
    wstring pat = modFolderPath + L"\\*";
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";
    wstring found;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        wstring cand = modFolderPath + L"\\" + fd.cFileName + L"\\modinfo.json";
        if (GetFileAttributesW(cand.c_str()) != INVALID_FILE_ATTRIBUTES) {
            found = cand; break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

void CollectMods() {
    g_mmRows.clear();
    wstring dir = ModsDir();
    WIN32_FIND_DATAW fd;
    wstring pat = dir + L"\\*";
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        ModFolderRow row;
        row.folder = fd.cFileName;
        wstring folderPath = dir + L"\\" + row.folder;
        row.modinfoPath = FindModinfoIn(folderPath);
        row.hasModinfo  = !row.modinfoPath.empty();
        if (row.hasModinfo) {
            wstring json = ReadTextFile(row.modinfoPath);
            wstring nm = JsonStr(json, L"title");
            if (nm.empty()) nm = JsonStr(json, L"name");
            if (nm.empty()) nm = row.folder;
            row.displayName = nm;
        } else {
            row.displayName = row.folder;
            row.modinfoPath = folderPath + L"\\modinfo.json";
        }
        row.rc = { 0, 0, 0, 0 };
        g_mmRows.push_back(row);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

void LayoutMods(HWND hw) {
    RECT rc; GetClientRect(hw, &rc);
    int W = rc.right;
    int cellW = (int)(MM_CELL_W * g_scale);
    int cellH = (int)(MM_CELL_H * g_scale);
    int pad   = (int)(MM_PAD * g_scale);
    int gridTop = (int)(MM_GRID_TOP * g_scale);

    int usableW = W - 2 * pad;
    int cols = (cellW > 0) ? usableW / cellW : 1;
    if (cols > MM_COLS_MAX) cols = MM_COLS_MAX;
    if (cols < 1) cols = 1;
    int gridW = cols * cellW;
    int startX = (W - gridW) / 2;

    for (size_t i = 0; i < g_mmRows.size(); ++i) {
        int col = (int)i % cols;
        int row = (int)i / cols;
        int x = startX + col * cellW;
        int y = gridTop + row * cellH;
        g_mmRows[i].rc = { x, y, x + cellW, y + cellH };
    }
}

int ModCellAt(int x, int y) {
    for (size_t i = 0; i < g_mmRows.size(); ++i) {
        const RECT& r = g_mmRows[i].rc;
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
            return (int)i;
    }
    return -1;
}

Gdiplus::Font* MakeGridFont(int px, bool bold) {
    Gdiplus::FontStyle style = bold ? FontStyleBold : FontStyleRegular;
    if (g_userFontFamilyOverride)
        return new Gdiplus::Font(g_userFontFamilyOverride, (REAL)px,
                                 (Gdiplus::FontStyle)(g_userFontStyleOverride | (bold ? FontStyleBold : 0)),
                                 UnitPixel);
    return new Gdiplus::Font(L"Segoe UI", (REAL)px, style, UnitPixel);
}

// Forward decl — the editor (defined below).
void ShowModinfoEditor(HWND parent, const ModFolderRow& row, bool startInEdit);
void RefreshModsModal();

} // namespace

// Draw one mod cell (folder icon + name, + red "No Mod Info" if missing).
static void DrawModCell(Graphics& g, const ModFolderRow& row, int idx,
                        Gdiplus::Font* nameFont, Gdiplus::Font* noInfoFont) {
    const RECT& r = row.rc;
    int cw = r.right - r.left;

    float scale = 1.0f;
    if (idx == g_mmDown)       scale = 0.93f;
    else if (idx == g_mmHover) scale = 1.08f;

    int iconBox = (int)(MM_ICON_BOX * g_scale);
    int drawBox = (int)(iconBox * scale);
    int icx = r.left + (cw - drawBox) / 2;
    int icy = r.top + (int)(6 * g_scale) + (iconBox - drawBox) / 2;

    if (Gdiplus::Bitmap* icon = AssetImage(L"btn_folder.png")) {
        // Grey the icon slightly for no-modinfo folders.
        if (!row.hasModinfo) {
            ImageAttributes ia;
            ColorMatrix cm = {
                0.5f,0.5f,0.5f,0,0,
                0.5f,0.5f,0.5f,0,0,
                0.5f,0.5f,0.5f,0,0,
                0,0,0,1,0,
                0,0,0,0,1 };
            ia.SetColorMatrix(&cm);
            g.DrawImage(icon, Rect(icx, icy, drawBox, drawBox),
                        0, 0, (INT)icon->GetWidth(), (INT)icon->GetHeight(),
                        UnitPixel, &ia);
        } else {
            g.DrawImage(icon, Rect(icx, icy, drawBox, drawBox));
        }
    } else {
        SolidBrush b(row.hasModinfo ? Color(150, 130, 80) : Color(90, 84, 72));
        g.FillRectangle(&b, icx, icy, drawBox, drawBox);
    }

    // Mod name — wraps up to 4 lines (word wrap, then ellipsis).
    Color nameColor = !row.hasModinfo ? Color(0x80, 0x78, 0x68)
                    : (idx == g_mmHover ? Tok::Gold : Color(0xD0, 0xC0, 0x98));
    SolidBrush nameBr(nameColor);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentNear);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    // No NoWrap flag → text wraps at word boundaries; the box height caps it.
    int labelY = r.top + (int)(6 * g_scale) + iconBox + (int)(4 * g_scale);
    RectF nr((REAL)r.left, (REAL)labelY, (REAL)cw, (REAL)(MM_NAME_H * g_scale));
    if (nameFont)
        g.DrawString(row.displayName.c_str(), -1, nameFont, nr, &sf, &nameBr);

    // Red "No Mod Info" beneath the name block for folders lacking modinfo.
    if (!row.hasModinfo && noInfoFont) {
        SolidBrush red(Color(0xC0, 0x50, 0x44));
        RectF ir((REAL)r.left, (REAL)(labelY + (int)(MM_NAME_H * g_scale)),
                 (REAL)cw, (REAL)(MM_NOINFO_H * g_scale));
        g.DrawString(L"No Mod Info", -1, noInfoFont, ir, &sf, &red);
    }
}

static LRESULT CALLBACK ModsProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        int W = rc.right, H = rc.bottom;
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);
        {
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            if (Gdiplus::Bitmap* stone = AssetImage(L"bg_stone.png")) {
                int sw = (int)stone->GetWidth(), sh = (int)stone->GetHeight();
                for (int yy = 0; yy < H; yy += sh)
                    for (int xx = 0; xx < W; xx += sw)
                        g.DrawImage(stone, xx, yy, sw, sh);
            } else { SolidBrush b(Color(28,24,20)); g.FillRectangle(&b,0,0,W,H); }
            if (Gdiplus::Bitmap* frame = AssetImage(L"frame_modbanner.png"))
                DrawButton9Slice(g, frame, 0, 0, W, H, 24);

            SolidBrush gold(Tok::Gold);
            StringFormat sfC; sfC.SetAlignment(StringAlignmentCenter);
            sfC.SetLineAlignment(StringAlignmentCenter);
            Gdiplus::Font* tf = g_fModName ? g_fModName : g_fNavSm;
            if (tf) g.DrawString(L"Mods", -1, tf,
                RectF((REAL)rc.left, (REAL)S(MM_TITLE_TOP),
                      (REAL)(rc.right - rc.left), (REAL)S(MM_TITLE_H)), &sfC, &gold);

            SolidBrush note(Color(0xB8, 0xA8, 0x80));
            Gdiplus::Font* nf = g_fNavSm;
            if (nf) g.DrawString(L"Left click to view/create mod info", -1, nf,
                RectF((REAL)rc.left, (REAL)S(MM_NOTE_TOP),
                      (REAL)(rc.right - rc.left), (REAL)(20 * g_scale)), &sfC, &note);

            if (g_mmRows.empty()) {
                SolidBrush e(Color(0xA0, 0x90, 0x70));
                RectF er((REAL)0, (REAL)(MM_GRID_TOP * g_scale), (REAL)W, (REAL)(40 * g_scale));
                if (nf) g.DrawString(L"(no mod folders)", -1, nf, er, &sfC, &e);
            } else {
                Gdiplus::Font* namef = MakeGridFont((int)(13 * g_scale), false);
                Gdiplus::Font* noif  = MakeGridFont((int)(12 * g_scale), true);
                for (size_t i = 0; i < g_mmRows.size(); ++i)
                    DrawModCell(g, g_mmRows[i], (int)i, namef, noif);
                delete namef; delete noif;
            }
        }
        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM); DeleteObject(memBM); DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int h = ModCellAt(x, y);
        if (h != g_mmHover) {
            g_mmHover = h;
            InvalidateRect(hw, nullptr, FALSE);
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hw, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_mmHover != -1) { g_mmHover = -1; InvalidateRect(hw, nullptr, FALSE); }
        return 0;

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        g_mmDown = ModCellAt(x, y);
        if (g_mmDown != -1) InvalidateRect(hw, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int up = ModCellAt(x, y);
        int was = g_mmDown;
        g_mmDown = -1;
        InvalidateRect(hw, nullptr, FALSE);
        if (up != -1 && up == was && up < (int)g_mmRows.size()) {
            // Existing modinfo → view (read-only); missing → create (edit).
            ModFolderRow row = g_mmRows[up];
            ShowModinfoEditor(hw, row, /*startInEdit=*/!row.hasModinfo);
        }
        return 0;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
        if (PaintOwnerDrawButton(di)) return TRUE;
        break;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        if (id == MM_IDC_BACK || id == IDCANCEL) { DestroyWindow(hw); return 0; }
        break;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(hw); return 0; }
        break;

    case WM_CLOSE: DestroyWindow(hw); return 0;

    case WM_DESTROY:
        if (HWND parent = GetWindow(hw, GW_OWNER)) {
            EnableWindow(parent, TRUE); SetActiveWindow(parent);
        }
        g_mmHwnd = nullptr; g_mmBack = nullptr;
        g_mmRows.clear(); g_mmHover = -1; g_mmDown = -1;
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

namespace {
void RefreshModsModal() {
    if (!g_mmHwnd) return;
    CollectMods();
    LayoutMods(g_mmHwnd);
    InvalidateRect(g_mmHwnd, nullptr, FALSE);
}
} // namespace

void ShowModsModal(HWND parent) {
    if (g_mmHwnd) return;
    if (g_cfg.d2rPath.empty()) {
        MessageBoxW(parent, L"Set your Diablo II: Resurrected path first.",
                    L"Mods", MB_OK | MB_ICONWARNING);
        return;
    }

    if (!g_mmReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = ModsProc;
        wc.hInstance     = g_hInst;
        wc.lpszClassName = L"AngirisModsModal";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        g_mmReg = true;
    }

    int physW = (int)(MM_W * g_scale), physH = (int)(MM_H * g_scale);
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - physW) / 2;
    int y = pr.top  + ((pr.bottom - pr.top) - physH) / 2;

    g_mmHwnd = CreateWindowExW(
        0,
        L"AngirisModsModal", L"Mods",
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, physW, physH, parent, nullptr, g_hInst, nullptr);
    if (!g_mmHwnd) return;

    CollectMods();
    LayoutMods(g_mmHwnd);

    int backH   = (int)(MM_BTN_H * g_scale);
    int backTop = physH - (int)(MM_BTN_BOT * g_scale) - backH;
    int physBackW = (int)(MM_BTN_W * g_scale);
    int backX = (physW - physBackW) / 2;
    g_mmBack = MkStdBtn(g_mmHwnd, L"Back", MM_IDC_BACK,
                        backX, backTop, physBackW, backH, true, ButtonKind::Plugins);

    EnableWindow(parent, FALSE);
    ShowWindow(g_mmHwnd, SW_SHOW);
    UpdateWindow(g_mmHwnd);
    SetActiveWindow(g_mmHwnd);

    MSG m;
    while (g_mmHwnd) {
        BOOL got = GetMessageW(&m, nullptr, 0, 0);
        if (got == 0 || got == -1) { if (got == 0) PostQuitMessage((int)m.wParam); break; }
        if (g_mmHwnd && IsDialogMessageW(g_mmHwnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Modinfo editor.
// ═══════════════════════════════════════════════════════════════════════
namespace {

constexpr int ED_W          = 580;
constexpr int ED_H          = 760;
constexpr int ED_TITLE_TOP  = 12;
constexpr int ED_TITLE_H    = 32;
constexpr int ED_PAD        = 20;
constexpr int ED_FIELD_TOP  = 96;
constexpr int ED_LABEL_W    = 148;   // wider so "description"/"savepath" fit on one line
constexpr int ED_ROW_H      = 26;
constexpr int ED_ROW_GAP    = 5;
constexpr int ED_ML_H       = 58;   // taller multiline (description/overview)
constexpr int ED_SUBTITLE_H = 26;   // "Mod Updates" subtitle band
constexpr int ED_BTN_W      = 150;  // larger Back/Cancel/Save
constexpr int ED_BTN_H      = 54;
constexpr int ED_BTN_BOT    = 16;

constexpr int ED_IDC_FIELD0 = 200;
constexpr int ED_IDC_EDIT   = 100;
constexpr int ED_IDC_SAVE   = 101;
constexpr int ED_IDC_BACK   = 102;

HWND    g_edHwnd = nullptr;
HWND    g_edEdit = nullptr;
HWND    g_edSave = nullptr;
HWND    g_edBack = nullptr;
HWND    g_edFields[kFieldCount] = {};
bool    g_edEditing = false;
bool    g_edCreating = false;
ModFolderRow g_edRow;
wstring g_edOriginal[kFieldCount];
HFONT   g_edFont = nullptr;

wstring EdGetField(int i) {
    if (!g_edFields[i]) return L"";
    wchar_t buf[2048] = {};
    GetWindowTextW(g_edFields[i], buf, 2048);
    return buf;
}

void EdSetReadOnly(bool ro) {
    for (int i = 0; i < kFieldCount; ++i)
        if (g_edFields[i])
            SendMessageW(g_edFields[i], EM_SETREADONLY, ro ? TRUE : FALSE, 0);
}

void EdLoadValues() {
    wstring json = g_edRow.hasModinfo ? ReadTextFile(g_edRow.modinfoPath) : L"";
    for (int i = 0; i < kFieldCount; ++i) {
        wstring v = json.empty() ? L"" : JsonStr(json, kFields[i].key);
        if (v.empty() && wcscmp(kFields[i].key, L"docs") == 0)
            v = json.empty() ? L"" : JsonStr(json, L"documents");
        g_edOriginal[i] = v;
        if (g_edFields[i]) SetWindowTextW(g_edFields[i], v.c_str());
    }
}

// Reflect the current state onto the three persistent buttons (no create/
// destroy — just enable/disable, which avoids the button flashing).
void EdUpdateButtons() {
    if (g_edEdit) EnableWindow(g_edEdit, g_edEditing ? FALSE : TRUE);
    if (g_edSave) EnableWindow(g_edSave, g_edEditing ? TRUE : FALSE);
    // Back label stays "Back"; in edit mode the same button acts as Cancel.
    if (g_edBack) SetWindowTextW(g_edBack, g_edEditing ? L"Cancel" : L"Back");
    if (g_edEdit) InvalidateRect(g_edEdit, nullptr, TRUE);
    if (g_edSave) InvalidateRect(g_edSave, nullptr, TRUE);
    if (g_edBack) InvalidateRect(g_edBack, nullptr, TRUE);
}

void EdEnterEdit() {
    g_edEditing = true;
    EdSetReadOnly(false);
    EdUpdateButtons();
    if (g_edFields[0]) SetFocus(g_edFields[0]);
    InvalidateRect(g_edHwnd, nullptr, FALSE);
}

void EdDoSave() {
    wstring name = EdGetField(0);
    wstring save = EdGetField(1);
    auto trimmed = [](const wstring& s) {
        size_t a = s.find_first_not_of(L" \t\r\n");
        if (a == wstring::npos) return wstring();
        size_t b = s.find_last_not_of(L" \t\r\n");
        return s.substr(a, b - a + 1);
    };
    if (trimmed(name).empty() || trimmed(save).empty()) {
        MessageBoxW(g_edHwnd,
            L"Both \"name\" and \"savepath\" are required and cannot be blank.",
            L"modinfo", MB_OK | MB_ICONWARNING);
        return;
    }

    wstring values[kFieldCount];
    for (int i = 0; i < kFieldCount; ++i) values[i] = EdGetField(i);
    wstring json = BuildModinfoJson(values);
    WriteTextFile(g_edRow.modinfoPath, json);

    g_edRow.hasModinfo = true;
    g_edCreating = false;
    for (int i = 0; i < kFieldCount; ++i) g_edOriginal[i] = values[i];
    g_edEditing = false;
    EdSetReadOnly(true);
    EdUpdateButtons();

    RefreshModsModal();
    RefreshMods();
    InvalidateRect(g_edHwnd, nullptr, FALSE);
}

void EdDoCancel() {
    if (g_edCreating) { DestroyWindow(g_edHwnd); return; }
    for (int i = 0; i < kFieldCount; ++i)
        if (g_edFields[i]) SetWindowTextW(g_edFields[i], g_edOriginal[i].c_str());
    g_edEditing = false;
    EdSetReadOnly(true);
    EdUpdateButtons();
    InvalidateRect(g_edHwnd, nullptr, FALSE);
}

} // namespace

static LRESULT CALLBACK ModinfoEditorProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        int W = rc.right, H = rc.bottom;
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);
        {
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            // Tile stone across the ENTIRE client so no bare bar shows at
            // the bottom (the old single stretched DrawImage left a black
            // band when the window was taller than the source art).
            if (Gdiplus::Bitmap* stone = AssetImage(L"bg_stone.png")) {
                int sw=(int)stone->GetWidth(), sh=(int)stone->GetHeight();
                for (int yy = 0; yy < H; yy += sh)
                    for (int xx = 0; xx < W; xx += sw)
                        g.DrawImage(stone, xx, yy, sw, sh);
            } else { SolidBrush b(Color(28,24,20)); g.FillRectangle(&b,0,0,W,H); }
            if (Gdiplus::Bitmap* frame = AssetImage(L"frame_modbanner.png"))
                DrawButton9Slice(g, frame, 0,0, W,H, 24);

            SolidBrush gold(Tok::Gold);
            StringFormat sfC; sfC.SetAlignment(StringAlignmentCenter);
            sfC.SetLineAlignment(StringAlignmentCenter);
            Gdiplus::Font* tf = g_fModName ? g_fModName : g_fNavSm;
            wstring title = g_edCreating ? L"Create modinfo" : L"modinfo";
            if (tf) g.DrawString(title.c_str(), -1, tf,
                RectF((REAL)rc.left, (REAL)S(ED_TITLE_TOP),
                      (REAL)(rc.right-rc.left), (REAL)S(ED_TITLE_H)), &sfC, &gold);

            // Field labels + the "Mod Updates" subtitle.
            SolidBrush lbl(Color(0xC8,0xB8,0x90));
            StringFormat sfL; sfL.SetLineAlignment(StringAlignmentCenter);
            Gdiplus::Font* ff = g_fNavSm;
            if (ff) {
                int yrow = (int)(ED_FIELD_TOP * g_scale);
                for (int i = 0; i < kFieldCount; ++i) {
                    if (kFields[i].subtitleBefore) {
                        // Space above + the subtitle band.
                        yrow += (int)(10 * g_scale);
                        StringFormat sfS; sfS.SetAlignment(StringAlignmentCenter);
                        sfS.SetLineAlignment(StringAlignmentCenter);
                        g.DrawString(L"Mod Updates", -1,
                            (g_fModName ? g_fModName : ff),
                            RectF((REAL)(ED_PAD*g_scale), (REAL)yrow,
                                  (REAL)(W - 2*(int)(ED_PAD*g_scale)),
                                  (REAL)(ED_SUBTITLE_H*g_scale)), &sfS, &gold);
                        yrow += (int)(ED_SUBTITLE_H * g_scale);
                    }
                    wstring l = wstring(kFields[i].label) + L":";
                    g.DrawString(l.c_str(), -1, ff,
                        RectF((REAL)(ED_PAD*g_scale), (REAL)yrow,
                              (REAL)(ED_LABEL_W*g_scale), (REAL)(ED_ROW_H*g_scale)),
                        &sfL, &lbl);
                    int rh = kFields[i].multiline ? ED_ML_H : ED_ROW_H;
                    yrow += (int)((rh + ED_ROW_GAP) * g_scale);
                }
            }
        }
        BitBlt(hdc, 0,0, W,H, memDC, 0,0, SRCCOPY);
        SelectObject(memDC, oldBM); DeleteObject(memBM); DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, RGB(0xE0, 0xD2, 0xB0));
        SetBkColor(dc, RGB(16, 13, 10));
        static HBRUSH s_br = CreateSolidBrush(RGB(16, 13, 10));
        return (LRESULT)s_br;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
        if (PaintOwnerDrawButton(di)) return TRUE;
        break;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        if (id == ED_IDC_EDIT) { if (!g_edEditing) EdEnterEdit(); return 0; }
        if (id == ED_IDC_SAVE) { if (g_edEditing) EdDoSave(); return 0; }
        if (id == ED_IDC_BACK || id == IDCANCEL) {
            if (g_edEditing) EdDoCancel();
            else DestroyWindow(hw);
            return 0;
        }
        break;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            if (g_edEditing) EdDoCancel();
            else DestroyWindow(hw);
            return 0;
        }
        break;

    case WM_CLOSE: DestroyWindow(hw); return 0;

    case WM_DESTROY:
        if (HWND parent = GetWindow(hw, GW_OWNER)) {
            EnableWindow(parent, TRUE); SetActiveWindow(parent);
        }
        if (g_edFont) { DeleteObject(g_edFont); g_edFont = nullptr; }
        g_edHwnd = nullptr; g_edEdit = nullptr; g_edSave = nullptr; g_edBack = nullptr;
        for (int i = 0; i < kFieldCount; ++i) g_edFields[i] = nullptr;
        g_edEditing = false; g_edCreating = false;
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

namespace {

void ShowModinfoEditor(HWND parent, const ModFolderRow& row, bool startInEdit) {
    if (g_edHwnd) return;
    g_edRow = row;
    g_edCreating = startInEdit;
    g_edEditing = false;

    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = ModinfoEditorProc;
        wc.hInstance     = g_hInst;
        wc.lpszClassName = L"AngirisModinfoEditor";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        reg = true;
    }

    int physW = (int)(ED_W * g_scale), physH = (int)(ED_H * g_scale);
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - physW) / 2;
    int y = pr.top  + ((pr.bottom - pr.top) - physH) / 2;

    g_edHwnd = CreateWindowExW(
        0,
        L"AngirisModinfoEditor",
        g_edCreating ? L"Create modinfo" : L"modinfo",
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, physW, physH, parent, nullptr, g_hInst, nullptr);
    if (!g_edHwnd) return;

    // Top Edit button.
    int ebW = (int)(128 * g_scale), ebH = (int)(46 * g_scale);
    int ebX = physW - (int)(ED_PAD * g_scale) - ebW;
    int ebY = (int)(46 * g_scale);
    g_edEdit = MkStdBtn(g_edHwnd, L"Edit", ED_IDC_EDIT,
                        ebX, ebY, ebW, ebH, true, ButtonKind::Plugins);

    // Field EDIT boxes (slightly smaller font so Description fits).
    g_edFont = MakeReaderFont(9);
    int yrow = (int)(ED_FIELD_TOP * g_scale);
    int boxX = (int)((ED_PAD + ED_LABEL_W) * g_scale);
    int boxW = physW - boxX - (int)(ED_PAD * g_scale);
    for (int i = 0; i < kFieldCount; ++i) {
        if (kFields[i].subtitleBefore)
            yrow += (int)(10 * g_scale) + (int)(ED_SUBTITLE_H * g_scale);
        bool ml = kFields[i].multiline;
        int rh = ml ? ED_ML_H : ED_ROW_H;
        DWORD style = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL;
        if (ml) style = WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL;
        g_edFields[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            style, boxX, yrow, boxW, (int)(rh * g_scale),
            g_edHwnd, (HMENU)(UINT_PTR)(ED_IDC_FIELD0 + i), g_hInst, nullptr);
        if (g_edFields[i])
            SendMessageW(g_edFields[i], WM_SETFONT, (WPARAM)g_edFont, TRUE);
        yrow += (int)((rh + ED_ROW_GAP) * g_scale);
    }

    EdLoadValues();

    // Persistent bottom buttons: Back/Cancel + Save, both always present;
    // state toggles their enabled/label (no destroy/recreate = no flashing).
    int bh = (int)(ED_BTN_H * g_scale);
    int bw = (int)(ED_BTN_W * g_scale);
    int by = physH - (int)(ED_BTN_BOT * g_scale) - bh;
    int gap = (int)(12 * g_scale);
    int rowW = bw * 2 + gap;
    int rowX = (physW - rowW) / 2;
    g_edBack = MkStdBtn(g_edHwnd, L"Back", ED_IDC_BACK,
                        rowX, by, bw, bh, true, ButtonKind::Plugins);
    g_edSave = MkStdBtn(g_edHwnd, L"Save", ED_IDC_SAVE,
                        rowX + bw + gap, by, bw, bh, true, ButtonKind::Plugins);

    if (g_edCreating) {
        g_edEditing = true;
        EdSetReadOnly(false);
    } else {
        EdSetReadOnly(true);
    }
    EdUpdateButtons();

    EnableWindow(parent, FALSE);
    ShowWindow(g_edHwnd, SW_SHOW);
    UpdateWindow(g_edHwnd);
    SetActiveWindow(g_edHwnd);

    MSG m;
    while (g_edHwnd) {
        BOOL got = GetMessageW(&m, nullptr, 0, 0);
        if (got == 0 || got == -1) { if (got == 0) PostQuitMessage((int)m.wParam); break; }
        if (g_edHwnd && IsDialogMessageW(g_edHwnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

} // namespace
