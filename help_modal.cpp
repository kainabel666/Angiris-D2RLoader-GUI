// ═══════════════════════════════════════════════════════════════════════
//  help_modal.cpp   —   see help_modal.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Reader-only modal for the Help button: renders FAQ.txt in a themed,
//  wheel-scrollable EDIT, with a "Need More Help?" prompt and the
//  D2RLoader Discord button below it, then Close at the bottom.
//
//  Deliberately parallels about_modal.cpp's reader (same EDIT theming,
//  same AboutEditProc-style wheel subclass, same frame_modbanner chrome
//  and nested pump) rather than sharing code — the two modals differ
//  enough in layout that a shared abstraction would be more tangled than
//  two focused files.

#include "help_modal.h"
#include "core.h"          // g_hInst, g_dpiScale, AppDir, ReadTextFile
#include "scaling.h"       // S()
#include "colors.h"        // Tok::Gold, Tok::Bronze, Tok::TextParchment
#include "fonts.h"         // g_fModName, g_fNavSm, g_fBtn
#include "assets.h"        // AssetImage, DrawButton9Slice
#include "buttons.h"       // MkStdBtn, PaintOwnerDrawButton, ButtonKind

using namespace Gdiplus;

namespace {
    constexpr int HM_W              = 560;  // wider than About — FAQ is dense
    constexpr int HM_H              = 620;
    constexpr int HM_TITLE_TOP_PAD  = 14;
    constexpr int HM_TITLE_H        = 36;
    constexpr int HM_READER_PAD     = 20;   // side inset for the EDIT
    constexpr int HM_READER_TOP     = 58;   // below the title band
    constexpr int HM_PROMPT_H       = 43;   // "Need More Help?" line height
    constexpr int HM_PROMPT_GAP     = 12;   // reader → prompt row
    constexpr int HM_DISCORD_SZ     = 43;   // square discord button (was 85, −50%)
    constexpr int HM_PROMPT_TO_DISC = 14;   // gap between prompt text and button
    constexpr int HM_CLOSE_W        = 140;
    constexpr int HM_CLOSE_H        = 50;
    constexpr int HM_BOTTOM_PAD     = 16;
    constexpr int HM_ROW_TO_CLOSE   = 16;   // prompt/discord row → Close

    const wchar_t* kUrlDiscord = L"https://discord.gg/eEHT2kcBMf";

    // IDs start at 100 to avoid IDOK(1)/IDCANCEL(2): IsDialogMessage
    // synthesizes WM_COMMAND/IDCANCEL on Esc, which would otherwise
    // "click" whatever control shares id 2.
    constexpr int HM_IDC_CLOSE   = 100;
    constexpr int HM_IDC_DISCORD = 101;
}

static HWND    g_hmHwnd        = nullptr;
static HWND    g_hmEdit        = nullptr;
static HWND    g_hmDiscord     = nullptr;
static HWND    g_hmClose       = nullptr;
static WNDPROC g_hmEditPrev    = nullptr;
static bool    g_hmClassReg    = false;

// Wheel-scroll subclass — a child EDIT under the cursor gets WM_MOUSEWHEEL
// itself, so scroll it here. ~3 lines per notch. (Same technique as the
// About modal's AboutEditProc.)
static LRESULT CALLBACK HelpEditProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_MOUSEWHEEL) {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        int lines = -(delta / WHEEL_DELTA) * 3;
        SendMessageW(h, EM_LINESCROLL, 0, (LPARAM)lines);
        return 0;
    }
    return CallWindowProcW(g_hmEditPrev, h, m, wp, lp);
}

// Load FAQ.txt into the reader, normalizing bare LF to CRLF (EDIT wants
// CRLF or it renders everything on one line).
static void LoadFaqInto(HWND edit) {
    wstring path = AppDir() + L"\\FAQ.txt";
    wstring text = ReadTextFile(path);
    if (text.empty()) {
        text = L"Could not open FAQ.txt.\r\n\r\n"
               L"The file may be missing from the launcher folder. You can "
               L"still reach the community via the Discord button below.";
    }
    wstring norm;
    norm.reserve(text.size() + 64);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\n' && (i == 0 || text[i-1] != L'\r')) norm += L"\r\n";
        else norm += text[i];
    }
    if (edit) {
        SetWindowTextW(edit, norm.c_str());
        SendMessageW(edit, EM_SETSEL, 0, 0);
        SendMessageW(edit, EM_SCROLLCARET, 0, 0);
    }
}

static LRESULT CALLBACK HelpModalProc(HWND hw, UINT msg,
                                      WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        int W = rc.right, H = rc.bottom;

        HDC memDC     = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);
        {
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

            // Stone background.
            if (Gdiplus::Bitmap* stone = AssetImage(L"bg_stone.png")) {
                int sw = (int)stone->GetWidth();
                int sh = (int)stone->GetHeight();
                int cw = (sw < W) ? sw : W;
                int ch = (sh < H) ? sh : H;
                g.DrawImage(stone, Rect(0, 0, W, H), 40, 40, cw, ch, UnitPixel);
            } else {
                SolidBrush bg(Color(28, 24, 20));
                g.FillRectangle(&bg, 0, 0, W, H);
            }

            // Frame chrome.
            if (Gdiplus::Bitmap* frame = AssetImage(L"frame_modbanner.png")) {
                DrawButton9Slice(g, frame, 0, 0, W, H, 24);
            } else {
                Pen fallback(Tok::Bronze, 1.0f);
                g.DrawRectangle(&fallback, 1, 1, W - 3, H - 3);
            }

            StringFormat sfC;
            sfC.SetAlignment(StringAlignmentCenter);
            sfC.SetLineAlignment(StringAlignmentCenter);

            // Title.
            SolidBrush gold(Tok::Gold);
            Gdiplus::Font* titleFont = g_fModName ? g_fModName : g_fNavSm;
            if (titleFont) {
                g.DrawString(L"Help \x2014 FAQ", -1, titleFont,
                    RectF((REAL)rc.left, (REAL)S(HM_TITLE_TOP_PAD),
                          (REAL)(rc.right - rc.left), (REAL)S(HM_TITLE_H)),
                    &sfC, &gold);
            }

            // "Need More Help?" prompt + Discord button share one row,
            // centered as a unit below the reader. Measure the prompt so we
            // can place text and button together (ShowHelpModal positions
            // the button to match this same math).
            int closeH   = S(HM_CLOSE_H);
            int discSz   = S(HM_DISCORD_SZ);
            int promptH  = S(HM_PROMPT_H);
            int closeTop = H - S(HM_BOTTOM_PAD) - closeH;
            int rowTop   = closeTop - S(HM_ROW_TO_CLOSE) - promptH;

            SolidBrush parch(Tok::TextParchment);
            Gdiplus::Font* promptFont = g_fNavSm ? g_fNavSm : g_fBtn;
            const wchar_t* promptText = L"Need More Help?";
            if (promptFont) {
                RectF meas;
                g.MeasureString(promptText, -1, promptFont,
                                RectF(0, 0, 4096, (REAL)promptH), &sfC, &meas);
                int textW = (int)(meas.Width + 0.5f);
                int gap   = S(HM_PROMPT_TO_DISC);
                int rowW  = textW + gap + discSz;
                int rowX  = (W - rowW) / 2;

                // Text left-aligned within its slot (so it hugs the button).
                StringFormat sfL;
                sfL.SetAlignment(StringAlignmentNear);
                sfL.SetLineAlignment(StringAlignmentCenter);
                g.DrawString(promptText, -1, promptFont,
                    RectF((REAL)rowX, (REAL)rowTop, (REAL)textW, (REAL)promptH),
                    &sfL, &parch);
                // (Discord button is a child HWND positioned in ShowHelpModal
                //  to sit at rowX + textW + gap, vertically centered in row.)
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
        // Parchment text on a near-black well (matches the About reader).
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
        if (id == IDCANCEL)       { DestroyWindow(hw); return 0; }  // Esc
        if (id == HM_IDC_CLOSE)   { DestroyWindow(hw); return 0; }
        if (id == HM_IDC_DISCORD) {
            ShellExecuteW(hw, L"open", kUrlDiscord, nullptr, nullptr,
                          SW_SHOWNORMAL);
            return 0;
        }
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
        g_hmHwnd    = nullptr;
        g_hmEdit    = nullptr;
        g_hmDiscord = nullptr;
        g_hmClose   = nullptr;
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

void ShowHelpModal(HWND parent) {
    if (g_hmHwnd) return;

    if (!g_hmClassReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = HelpModalProc;
        wc.hInstance     = g_hInst;
        wc.lpszClassName = L"AngirisHelpModal";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        g_hmClassReg = true;
    }

    int physW = (int)(HM_W * g_dpiScale);
    int physH = (int)(HM_H * g_dpiScale);

    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right  - pr.left) - physW) / 2;
    int y = pr.top  + ((pr.bottom - pr.top ) - physH) / 2;

    g_hmHwnd = CreateWindowExW(
        WS_EX_TOPMOST,
        L"AngirisHelpModal", L"Help",
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, physW, physH,
        parent, nullptr, g_hInst, nullptr);
    if (!g_hmHwnd) return;

    // Layout (bottom-anchored): Close at the bottom; the "Need More Help?"
    // prompt + Discord button share one centered row above it; the reader
    // EDIT fills from the title band down to that row. Compute bottom-up.
    int closeH   = (int)(HM_CLOSE_H * g_dpiScale);
    int discSz   = (int)(HM_DISCORD_SZ * g_dpiScale);
    int promptH  = (int)(HM_PROMPT_H * g_dpiScale);
    int closeTop = physH - (int)(HM_BOTTOM_PAD * g_dpiScale) - closeH;
    int rowTop   = closeTop - (int)(HM_ROW_TO_CLOSE * g_dpiScale) - promptH;

    int edX = (int)(HM_READER_PAD * g_dpiScale);
    int edY = (int)(HM_READER_TOP * g_dpiScale);
    int edW = physW - 2 * edX;
    int edH = rowTop - (int)(HM_PROMPT_GAP * g_dpiScale) - edY;
    if (edH < (int)(80 * g_dpiScale)) edH = (int)(80 * g_dpiScale);

    g_hmEdit = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        edX, edY, edW, edH,
        g_hmHwnd, nullptr, g_hInst, nullptr);
    if (g_hmEdit) {
        HFONT hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        SendMessageW(g_hmEdit, WM_SETFONT, (WPARAM)hf, TRUE);
        g_hmEditPrev = (WNDPROC)SetWindowLongPtrW(
            g_hmEdit, GWLP_WNDPROC, (LONG_PTR)HelpEditProc);
        LoadFaqInto(g_hmEdit);
    }

    // Discord button — sits to the RIGHT of the prompt text, the pair
    // centered as a unit. Measure the prompt the same way the paint code
    // does so the two agree on placement.
    int discX = (physW - discSz) / 2;   // fallback if measuring fails
    {
        HDC screen = GetDC(g_hmHwnd);
        if (screen) {
            Graphics mg(screen);
            Gdiplus::Font* pf = g_fNavSm ? g_fNavSm : g_fBtn;
            if (pf) {
                StringFormat sf;
                sf.SetAlignment(StringAlignmentNear);
                sf.SetLineAlignment(StringAlignmentCenter);
                RectF meas;
                mg.MeasureString(L"Need More Help?", -1, pf,
                                 RectF(0, 0, 4096, (REAL)promptH), &sf, &meas);
                int textW = (int)(meas.Width + 0.5f);
                int gap   = (int)(HM_PROMPT_TO_DISC * g_dpiScale);
                int rowW  = textW + gap + discSz;
                int rowX  = (physW - rowW) / 2;
                discX = rowX + textW + gap;
            }
            ReleaseDC(g_hmHwnd, screen);
        }
    }
    int discY = rowTop + (promptH - discSz) / 2;   // vertically centered in row
    g_hmDiscord = MkStdBtn(g_hmHwnd, L"X", HM_IDC_DISCORD,
                           discX, discY, discSz, discSz,
                           true, ButtonKind::ModLinkDiscord);

    // Close — centered, bottom.
    int physCloseW = (int)(HM_CLOSE_W * g_dpiScale);
    int closeX = (physW - physCloseW) / 2;
    g_hmClose = MkStdBtn(g_hmHwnd, L"Close", HM_IDC_CLOSE,
                         closeX, closeTop, physCloseW, closeH,
                         true, ButtonKind::Plugins);

    EnableWindow(parent, FALSE);
    ShowWindow(g_hmHwnd, SW_SHOW);
    UpdateWindow(g_hmHwnd);
    SetActiveWindow(g_hmHwnd);

    MSG msg;
    while (g_hmHwnd) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == 0 || got == -1) {
            if (got == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (g_hmHwnd && IsDialogMessageW(g_hmHwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}
