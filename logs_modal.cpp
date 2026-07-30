// ═══════════════════════════════════════════════════════════════════════
//  logs_modal.cpp — Logs viewer modal (v1.6)
// ═══════════════════════════════════════════════════════════════════════
//
//  Shows the logs files as an icon grid (btn_docs.png icons, up to 4 per
//  row, wrapping responsively). Each cell shows the filename (no extension)
//  beneath the icon, Explorer-style. LEFT-CLICK an icon copies that file to
//  the clipboard (CF_HDROP) so Ctrl+V attaches it in Discord. Icons grow on
//  hover and shrink on click. Themed on bg_stone; labels use the user font
//  and scale with DPI and the UI-scale slider.

#include "logs_modal.h"
#include "core.h"        // g_hInst, g_scale
#include "config.h"      // g_cfg.d2rPath, g_cfg.fontName
#include "colors.h"      // Tok::Gold, Tok::Bronze
#include "fonts.h"       // g_fModName, g_fNavSm, g_userFontFamilyOverride
#include "assets.h"      // AssetImage, DrawButton9Slice
#include "buttons.h"     // MkStdBtn, PaintOwnerDrawButton, ButtonKind
#include "scaling.h"     // S()

#include <windows.h>
#include <windowsx.h>    // GET_X_LPARAM / GET_Y_LPARAM
#include <shlobj.h>      // DROPFILES / CF_HDROP
#include <vector>

using namespace Gdiplus;
using std::wstring;
using std::vector;

namespace {
    constexpr int LG_W          = 620;
    constexpr int LG_H          = 640;
    constexpr int LG_TITLE_TOP  = 14;
    constexpr int LG_TITLE_H    = 32;
    constexpr int LG_NOTE_TOP   = 50;
    constexpr int LG_GRID_TOP   = 84;
    constexpr int LG_PAD        = 24;
    constexpr int LG_BTN_W      = 160;
    constexpr int LG_BTN_H      = 58;
    constexpr int LG_BTN_BOT    = 18;

    constexpr int LG_CELL_W     = 120;
    constexpr int LG_CELL_H     = 104;
    constexpr int LG_ICON_BOX   = 64;
    constexpr int LG_LABEL_H    = 34;
    constexpr int LG_COLS_MAX   = 4;

    constexpr int LG_IDC_CLOSE  = 101;

    struct LogCell {
        wstring fullPath;
        wstring label;
        RECT    rc;
    };

    HWND    g_lgHwnd  = nullptr;
    HWND    g_lgClose = nullptr;
    bool    g_lgReg   = false;
    vector<LogCell> g_lgCells;
    int     g_lgHover = -1;
    int     g_lgDown  = -1;

    wstring LogsDir() { return g_cfg.d2rPath + L"\\logs"; }

    wstring StripExt(const wstring& name) {
        size_t dot = name.find_last_of(L'.');
        return (dot == wstring::npos) ? name : name.substr(0, dot);
    }

    void CollectLogs() {
        g_lgCells.clear();
        wstring dir = LogsDir();
        WIN32_FIND_DATAW fd;
        wstring pat = dir + L"\\*";
        HANDLE h = FindFirstFileW(pat.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            LogCell c;
            c.fullPath = dir + L"\\" + fd.cFileName;
            c.label    = StripExt(fd.cFileName);
            c.rc = { 0, 0, 0, 0 };
            g_lgCells.push_back(c);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    void LayoutCells(HWND hw) {
        RECT rc; GetClientRect(hw, &rc);
        int W = rc.right;
        int cellW = (int)(LG_CELL_W * g_scale);
        int cellH = (int)(LG_CELL_H * g_scale);
        int pad   = (int)(LG_PAD * g_scale);
        int gridTop = (int)(LG_GRID_TOP * g_scale);

        int usableW = W - 2 * pad;
        int cols = (cellW > 0) ? usableW / cellW : 1;
        if (cols > LG_COLS_MAX) cols = LG_COLS_MAX;
        if (cols < 1) cols = 1;

        int gridW = cols * cellW;
        int startX = (W - gridW) / 2;

        for (size_t i = 0; i < g_lgCells.size(); ++i) {
            int col = (int)i % cols;
            int row = (int)i / cols;
            int x = startX + col * cellW;
            int y = gridTop + row * cellH;
            g_lgCells[i].rc = { x, y, x + cellW, y + cellH };
        }
    }

    int CellAt(int x, int y) {
        for (size_t i = 0; i < g_lgCells.size(); ++i) {
            const RECT& r = g_lgCells[i].rc;
            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
                return (int)i;
        }
        return -1;
    }

    void CopyFileToClipboard(HWND owner, const wstring& path) {
        size_t bytes = sizeof(DROPFILES) + (path.size() + 2) * sizeof(wchar_t);
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
        if (!hg) return;
        auto* df = (DROPFILES*)GlobalLock(hg);
        df->pFiles = sizeof(DROPFILES);
        df->fWide  = TRUE;
        wchar_t* dst = (wchar_t*)((BYTE*)df + sizeof(DROPFILES));
        memcpy(dst, path.c_str(), path.size() * sizeof(wchar_t));
        dst[path.size()]     = L'\0';
        dst[path.size() + 1] = L'\0';
        GlobalUnlock(hg);
        if (OpenClipboard(owner)) {
            EmptyClipboard();
            SetClipboardData(CF_HDROP, hg);
            CloseClipboard();
        } else {
            GlobalFree(hg);
        }
    }

    Gdiplus::Font* MakeGridFont(int px) {
        if (g_userFontFamilyOverride)
            return new Gdiplus::Font(g_userFontFamilyOverride, (REAL)px,
                                     (Gdiplus::FontStyle)g_userFontStyleOverride,
                                     UnitPixel);
        return new Gdiplus::Font(L"Segoe UI", (REAL)px, FontStyleRegular, UnitPixel);
    }
}

static void DrawLogCell(Graphics& g, const LogCell& cell, int idx,
                        Gdiplus::Font* labelFont) {
    const RECT& r = cell.rc;
    int cw = r.right - r.left;

    float scale = 1.0f;
    if (idx == g_lgDown)       scale = 0.93f;
    else if (idx == g_lgHover) scale = 1.08f;

    int iconBox = (int)(LG_ICON_BOX * g_scale);
    int drawBox = (int)(iconBox * scale);
    int icx = r.left + (cw - drawBox) / 2;
    int icy = r.top + (int)(6 * g_scale) + (iconBox - drawBox) / 2;

    if (Gdiplus::Bitmap* icon = AssetImage(L"btn_docs.png")) {
        g.DrawImage(icon, Rect(icx, icy, drawBox, drawBox));
    } else {
        SolidBrush b(Color(120, 110, 90));
        g.FillRectangle(&b, icx, icy, drawBox, drawBox);
    }

    SolidBrush txt(idx == g_lgHover ? Tok::Gold : Color(0xD0, 0xC0, 0x98));
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentNear);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    RectF lr((REAL)r.left, (REAL)(r.top + (int)(6 * g_scale) + iconBox),
             (REAL)cw, (REAL)(LG_LABEL_H * g_scale));
    if (labelFont)
        g.DrawString(cell.label.c_str(), -1, labelFont, lr, &sf, &txt);
}

static LRESULT CALLBACK LogsProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
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
            } else {
                SolidBrush bg(Color(28, 24, 20));
                g.FillRectangle(&bg, 0, 0, W, H);
            }
            if (Gdiplus::Bitmap* frame = AssetImage(L"frame_modbanner.png"))
                DrawButton9Slice(g, frame, 0, 0, W, H, 24);

            SolidBrush gold(Tok::Gold);
            StringFormat sfC; sfC.SetAlignment(StringAlignmentCenter);
            sfC.SetLineAlignment(StringAlignmentCenter);
            Gdiplus::Font* tf = g_fModName ? g_fModName : g_fNavSm;
            if (tf) g.DrawString(L"Logs", -1, tf,
                RectF((REAL)rc.left, (REAL)S(LG_TITLE_TOP),
                      (REAL)(rc.right - rc.left), (REAL)S(LG_TITLE_H)), &sfC, &gold);

            SolidBrush note(Color(0xB8, 0xA8, 0x80));
            Gdiplus::Font* nf = g_fNavSm;
            if (nf) g.DrawString(L"Left click on icon to copy file to clipboard", -1, nf,
                RectF((REAL)rc.left, (REAL)S(LG_NOTE_TOP),
                      (REAL)(rc.right - rc.left), (REAL)(20 * g_scale)), &sfC, &note);

            if (g_lgCells.empty()) {
                SolidBrush e(Color(0xA0, 0x90, 0x70));
                RectF er((REAL)0, (REAL)(LG_GRID_TOP * g_scale),
                         (REAL)W, (REAL)(40 * g_scale));
                if (nf) g.DrawString(L"(no log files)", -1, nf, er, &sfC, &e);
            } else {
                int labelPx = (int)(13 * g_scale);
                Gdiplus::Font* lf = MakeGridFont(labelPx);
                for (size_t i = 0; i < g_lgCells.size(); ++i)
                    DrawLogCell(g, g_lgCells[i], (int)i, lf);
                delete lf;
            }
        }
        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM); DeleteObject(memBM); DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int h = CellAt(x, y);
        if (h != g_lgHover) {
            g_lgHover = h;
            InvalidateRect(hw, nullptr, FALSE);
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hw, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_lgHover != -1) { g_lgHover = -1; InvalidateRect(hw, nullptr, FALSE); }
        return 0;

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        g_lgDown = CellAt(x, y);
        if (g_lgDown != -1) InvalidateRect(hw, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int up = CellAt(x, y);
        if (up != -1 && up == g_lgDown && up < (int)g_lgCells.size())
            CopyFileToClipboard(hw, g_lgCells[up].fullPath);
        g_lgDown = -1;
        InvalidateRect(hw, nullptr, FALSE);
        return 0;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;
        if (PaintOwnerDrawButton(d)) return TRUE;
        break;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        if (id == LG_IDC_CLOSE || id == IDCANCEL) { DestroyWindow(hw); return 0; }
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
        g_lgHwnd = nullptr; g_lgClose = nullptr;
        g_lgCells.clear(); g_lgHover = -1; g_lgDown = -1;
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

void ShowLogsModal(HWND parent) {
    if (g_lgHwnd) return;
    if (g_cfg.d2rPath.empty()) {
        MessageBoxW(parent, L"Set your Diablo II: Resurrected path first.",
                    L"Logs", MB_OK | MB_ICONWARNING);
        return;
    }
    CreateDirectoryW(LogsDir().c_str(), nullptr);

    if (!g_lgReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = LogsProc;
        wc.hInstance     = g_hInst;
        wc.lpszClassName = L"AngirisLogsModal";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        g_lgReg = true;
    }

    int physW = (int)(LG_W * g_scale), physH = (int)(LG_H * g_scale);
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - physW) / 2;
    int y = pr.top  + ((pr.bottom - pr.top) - physH) / 2;

    g_lgHwnd = CreateWindowExW(
        0,
        L"AngirisLogsModal", L"Logs",
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, physW, physH, parent, nullptr, g_hInst, nullptr);
    if (!g_lgHwnd) return;

    CollectLogs();
    LayoutCells(g_lgHwnd);

    int closeH   = (int)(LG_BTN_H * g_scale);
    int closeTop = physH - (int)(LG_BTN_BOT * g_scale) - closeH;
    int physCloseW = (int)(LG_BTN_W * g_scale);
    int closeX = (physW - physCloseW) / 2;
    g_lgClose = MkStdBtn(g_lgHwnd, L"Close", LG_IDC_CLOSE,
                         closeX, closeTop, physCloseW, closeH,
                         true, ButtonKind::Plugins);

    EnableWindow(parent, FALSE);
    ShowWindow(g_lgHwnd, SW_SHOW);
    UpdateWindow(g_lgHwnd);
    SetActiveWindow(g_lgHwnd);

    MSG m;
    while (g_lgHwnd) {
        BOOL got = GetMessageW(&m, nullptr, 0, 0);
        if (got == 0 || got == -1) { if (got == 0) PostQuitMessage((int)m.wParam); break; }
        if (g_lgHwnd && IsDialogMessageW(g_lgHwnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}
