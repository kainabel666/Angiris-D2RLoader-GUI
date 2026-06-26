// ═══════════════════════════════════════════════════════════════════════
//  hover_tip.cpp — see hover_tip.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "hover_tip.h"
#include "core.h"        // g_dpiScale
#include "assets.h"      // AssetImage(L"bg_stone.png")
#include "fonts.h"       // g_fBtn
#include "colors.h"      // Tok::Gold, Tok::Bronze, Tok::TextParchment
#include "mod_scan.h"    // g_mods
#include "mod_types.h"   // ModInfo
#include "playtime.h"    // g_playtimes, FormatPlaytime, FormatLastPlayed

// ── State (file-local) ──────────────────────────────────────────────
//
// Single tooltip at a time, so a single HWND + two text strings cover
// the entire model. HoverTipProc reads g_hoverTipText1/2 in WM_PAINT
// (no need to bake the strings into the window's properties).

static HWND    g_hoverTipHwnd = nullptr;
static wstring g_hoverTipText1;
static wstring g_hoverTipText2;

// ── Window proc ─────────────────────────────────────────────────────
//
// Parchment background (bg_stone.png), double gold/bronze border,
// two parchment-text lines. Double-buffered into a memDC so the
// AA edges of the border don't flicker on rapid show/hide.

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
            Gdiplus::Graphics g(memDC);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

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
            Gdiplus::Pen border(Tok::Gold, 2.0f);
            g.DrawRectangle(&border, 1, 1, W - 3, H - 3);
            Gdiplus::Pen innerB(Tok::Bronze, 1.0f);
            g.DrawRectangle(&innerB, 3, 3, W - 7, H - 7);

            Gdiplus::SolidBrush textBr(Tok::TextParchment);
            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentNear);
            sf.SetLineAlignment(Gdiplus::StringAlignmentNear);
            sf.SetFormatFlags(sf.GetFormatFlags()
                              | Gdiplus::StringFormatFlagsNoWrap);

            int padX  = (int)(14 * g_dpiScale);
            int y     = (int)(12 * g_dpiScale);
            int lineH = (int)(22 * g_dpiScale);

            Gdiplus::Font* font = g_fBtn;
            if (font) {
                g.DrawString(g_hoverTipText1.c_str(), -1, font,
                    Gdiplus::RectF((REAL)padX, (REAL)y,
                                   (REAL)(W - 2 * padX), (REAL)lineH),
                    &sf, &textBr);
                y += lineH;
                g.DrawString(g_hoverTipText2.c_str(), -1, font,
                    Gdiplus::RectF((REAL)padX, (REAL)y,
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

// ── Public API ──────────────────────────────────────────────────────

void HideHoverTip() {
    if (g_hoverTipHwnd) {
        DestroyWindow(g_hoverTipHwnd);
        g_hoverTipHwnd = nullptr;
    }
}

void ShowHoverTip(int modIdx) {
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
