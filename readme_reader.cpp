// ═══════════════════════════════════════════════════════════════════════
//  readme_reader.cpp — see readme_reader.h
// ═══════════════════════════════════════════════════════════════════════

#include "readme_reader.h"
#include "core.h"          // g_hInst, g_scale, ReadTextFile
#include "scaling.h"       // S()
#include "colors.h"        // Tok::Gold, Tok::Bronze
#include "fonts.h"         // g_fModName, g_fNavSm, MakeReaderFont
#include "assets.h"        // AssetImage, DrawButton9Slice
#include "buttons.h"       // MkStdBtn, PaintOwnerDrawButton, ButtonKind

using namespace Gdiplus;

namespace {
    constexpr int RR_W            = 620;
    constexpr int RR_H            = 640;
    constexpr int RR_TITLE_TOP    = 14;
    constexpr int RR_TITLE_H      = 36;
    constexpr int RR_READER_PAD   = 20;
    constexpr int RR_READER_TOP   = 58;
    constexpr int RR_CLOSE_W      = 140;
    constexpr int RR_CLOSE_H      = 50;
    constexpr int RR_BOTTOM_PAD   = 16;

    constexpr int RR_IDC_CLOSE    = 100;   // off IDOK/IDCANCEL

    HWND    g_rrHwnd     = nullptr;
    HWND    g_rrEdit     = nullptr;
    HWND    g_rrClose    = nullptr;
    WNDPROC g_rrEditPrev = nullptr;
    HFONT   g_rrFont     = nullptr;
    bool    g_rrClassReg = false;
    wstring g_rrTitle;
}

// Wheel-scroll subclass (the child EDIT gets the wheel messages).
static LRESULT CALLBACK RrEditProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_MOUSEWHEEL) {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        int lines = -(delta / WHEEL_DELTA) * 3;
        SendMessageW(h, EM_LINESCROLL, 0, (LPARAM)lines);
        return 0;
    }
    return CallWindowProcW(g_rrEditPrev, h, m, wp, lp);
}

static void LoadFileInto(HWND edit, const wstring& path) {
    wstring text = ReadTextFile(path);
    if (text.empty())
        text = L"Could not open this README.\r\n\r\nThe file may be missing "
               L"or empty.";
    // LF → CRLF so the EDIT renders line breaks.
    wstring norm; norm.reserve(text.size() + 64);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\n' && (i == 0 || text[i-1] != L'\r')) norm += L"\r\n";
        else norm += text[i];
    }
    SetWindowTextW(edit, norm.c_str());
    SendMessageW(edit, EM_SETSEL, 0, 0);
    SendMessageW(edit, EM_SCROLLCARET, 0, 0);
}

static LRESULT CALLBACK RrProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
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
                int cw = (sw < W) ? sw : W, ch = (sh < H) ? sh : H;
                g.DrawImage(stone, Rect(0, 0, W, H), 40, 40, cw, ch, UnitPixel);
            } else {
                SolidBrush bg(Color(28, 24, 20));
                g.FillRectangle(&bg, 0, 0, W, H);
            }
            if (Gdiplus::Bitmap* frame = AssetImage(L"frame_modbanner.png")) {
                DrawButton9Slice(g, frame, 0, 0, W, H, 24);
            } else {
                Pen fb(Tok::Bronze, 1.0f);
                g.DrawRectangle(&fb, 1, 1, W - 3, H - 3);
            }

            SolidBrush gold(Tok::Gold);
            StringFormat sfC;
            sfC.SetAlignment(StringAlignmentCenter);
            sfC.SetLineAlignment(StringAlignmentCenter);
            Gdiplus::Font* tf = g_fModName ? g_fModName : g_fNavSm;
            if (tf) {
                g.DrawString(g_rrTitle.c_str(), -1, tf,
                    RectF((REAL)rc.left, (REAL)S(RR_TITLE_TOP),
                          (REAL)(rc.right - rc.left), (REAL)S(RR_TITLE_H)),
                    &sfC, &gold);
            }
        }
        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, RGB(0xD8, 0xC7, 0xA0));
        SetBkColor(dc, RGB(12, 10, 8));
        static HBRUSH s_br = CreateSolidBrush(RGB(12, 10, 8));
        return (LRESULT)s_br;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;
        if (PaintOwnerDrawButton(d)) return TRUE;
        break;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        if (id == RR_IDC_CLOSE || id == IDCANCEL) { DestroyWindow(hw); return 0; }
        break;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(hw); return 0; }
        break;

    case WM_CLOSE:
        DestroyWindow(hw);
        return 0;

    case WM_DESTROY:
        if (HWND parent = GetWindow(hw, GW_OWNER)) {
            EnableWindow(parent, TRUE);
            SetActiveWindow(parent);
        }
        if (g_rrFont) { DeleteObject(g_rrFont); g_rrFont = nullptr; }
        g_rrHwnd = nullptr; g_rrEdit = nullptr; g_rrClose = nullptr;
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

void ShowReadmeReader(HWND parent, const wstring& title, const wstring& filePath) {
    if (g_rrHwnd) return;
    g_rrTitle = title.empty() ? L"README" : title;

    if (!g_rrClassReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = RrProc;
        wc.hInstance     = g_hInst;
        wc.lpszClassName = L"AngirisReadmeReader";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        g_rrClassReg = true;
    }

    int physW = (int)(RR_W * g_scale);
    int physH = (int)(RR_H * g_scale);
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - physW) / 2;
    int y = pr.top  + ((pr.bottom - pr.top) - physH) / 2;

    g_rrHwnd = CreateWindowExW(
        0,  // owned popup: stays above its owner (launcher) without pinning over other apps
        L"AngirisReadmeReader", L"README",
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, physW, physH, parent, nullptr, g_hInst, nullptr);
    if (!g_rrHwnd) return;

    int closeH   = (int)(RR_CLOSE_H * g_scale);
    int closeTop = physH - (int)(RR_BOTTOM_PAD * g_scale) - closeH;

    int edX = (int)(RR_READER_PAD * g_scale);
    int edY = (int)(RR_READER_TOP * g_scale);
    int edW = physW - 2 * edX;
    int edH = closeTop - (int)(RR_READER_PAD * g_scale) - edY;
    if (edH < (int)(80 * g_scale)) edH = (int)(80 * g_scale);

    g_rrEdit = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        edX, edY, edW, edH, g_rrHwnd, nullptr, g_hInst, nullptr);
    if (g_rrEdit) {
        g_rrFont = MakeReaderFont(11);
        SendMessageW(g_rrEdit, WM_SETFONT, (WPARAM)g_rrFont, TRUE);
        g_rrEditPrev = (WNDPROC)SetWindowLongPtrW(
            g_rrEdit, GWLP_WNDPROC, (LONG_PTR)RrEditProc);
        LoadFileInto(g_rrEdit, filePath);
    }

    int physCloseW = (int)(RR_CLOSE_W * g_scale);
    int closeX = (physW - physCloseW) / 2;
    g_rrClose = MkStdBtn(g_rrHwnd, L"Close", RR_IDC_CLOSE,
                         closeX, closeTop, physCloseW, closeH,
                         true, ButtonKind::Plugins);

    EnableWindow(parent, FALSE);
    ShowWindow(g_rrHwnd, SW_SHOW);
    UpdateWindow(g_rrHwnd);
    SetActiveWindow(g_rrHwnd);

    MSG msg;
    while (g_rrHwnd) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == 0 || got == -1) { if (got == 0) PostQuitMessage((int)msg.wParam); break; }
        if (g_rrHwnd && IsDialogMessageW(g_rrHwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}
