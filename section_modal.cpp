// ═══════════════════════════════════════════════════════════════════════
//  section_modal.cpp   —   see section_modal.h
// ═══════════════════════════════════════════════════════════════════════

#include "section_modal.h"
#include "core.h"          // g_hInst, g_dpiScale
#include "scaling.h"       // S(), SF()
#include "colors.h"        // Tok::Gold, Tok::Bronze
#include "fonts.h"         // g_fModName, g_fNavSm
#include "assets.h"        // AssetImage, DrawButton9Slice
#include "buttons.h"       // MkStdBtn, PaintOwnerDrawButton, ButtonKind
#include "tool_resolver.h" // LaunchTool

using namespace Gdiplus;

// ── Geometry (logical px; scaled through S() at Win32 boundaries) ────────
namespace {
    // Narrow buttons throughout. The window width adapts to the column
    // count: a single-column list gets a slim window; two columns widen it.
    constexpr int SM_TITLE_TOP_PAD  = 14;
    constexpr int SM_TITLE_H        = 36;
    constexpr int SM_TITLE_BOT_PAD  = 10;
    constexpr int SM_ITEM_W         = 240;  // narrow item button
    constexpr int SM_ITEM_H         = 52;
    constexpr int SM_ITEM_VGAP      = 12;   // vertical gap between rows
    constexpr int SM_ITEM_HGAP      = 16;   // horizontal gap between columns
    constexpr int SM_ITEMS_TO_CLOSE = 18;   // gap from last row to Close
    constexpr int SM_CLOSE_W        = 140;
    constexpr int SM_CLOSE_H        = 50;
    constexpr int SM_BOTTOM_PAD     = 16;
    constexpr int SM_SIDE_PAD       = 34;   // frame filigree inset each side

    // 1 column for short lists, 2 for longer ones. Above this count the
    // grid switches to two columns.
    constexpr int SM_ONE_COL_MAX    = 4;

    constexpr int SM_MAX_ITEMS      = 16;   // generous headroom for growth

    // Local button ids. Kept clear of IDOK(1)/IDCANCEL(2): IsDialogMessage
    // turns Esc into a WM_COMMAND/IDCANCEL, which would "click" any control
    // sharing id 1 or 2.
    constexpr int SM_IDC_ITEM_FIRST = 100;
    constexpr int SM_IDC_CLOSE      = 200;
}

// Grid geometry derived from the item count: how many columns, how many
// rows (the taller column when odd), and the resulting window dimensions.
// One source of truth for both the height/width calc and button placement.
struct SectionGrid {
    int cols;       // 1 or 2
    int rows;       // rows in the tallest column
    int winW;       // logical window width
    int winH;       // logical window height
    int gridW;      // width of the button block (cols * item + gaps)
    int gridLeft;   // logical x of the block's left edge (centered)
    int firstRowY;  // logical y of the first row
};

static SectionGrid ComputeSectionGrid(int itemCount) {
    SectionGrid g = {};
    if (itemCount < 1) itemCount = 1;

    g.cols = (itemCount > SM_ONE_COL_MAX) ? 2 : 1;
    g.rows = (itemCount + g.cols - 1) / g.cols;   // ceil — taller col first

    g.gridW = g.cols * SM_ITEM_W + (g.cols - 1) * SM_ITEM_HGAP;
    g.winW  = g.gridW + 2 * SM_SIDE_PAD;

    g.firstRowY = SM_TITLE_TOP_PAD + SM_TITLE_H + SM_TITLE_BOT_PAD;
    int rowsH = g.rows * SM_ITEM_H + (g.rows - 1) * SM_ITEM_VGAP;
    g.winH = g.firstRowY + rowsH
             + SM_ITEMS_TO_CLOSE + SM_CLOSE_H + SM_BOTTOM_PAD;

    g.gridLeft = (g.winW - g.gridW) / 2;
    return g;
}

// ── Active-content binding (pointer-swapped per open, like the loader
//    modal's g_activeRows). Valid only while the modal is up. ────────────
static const SectionItem* g_smItems     = nullptr;
static int                g_smItemCount  = 0;
static const wchar_t*     g_smTitle      = L"";

static HWND  g_smHwnd     = nullptr;
static HWND  g_smItemBtns[SM_MAX_ITEMS] = { nullptr };
static HWND  g_smCloseBtn = nullptr;
static bool  g_smClassReg = false;

// Run the item's action (open url or launch tool), then close the modal —
// the user picked something, so the picker's job is done.
static void RunSectionItem(HWND hw, int idx) {
    if (!g_smItems || idx < 0 || idx >= g_smItemCount) return;
    const SectionItem& it = g_smItems[idx];
    switch (it.kind) {
    case SectionActionKind::OpenUrl:
        if (it.url && *it.url)
            ShellExecuteW(hw, L"open", it.url, nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case SectionActionKind::LaunchTool:
        if (it.toolCachedPath)
            LaunchTool(hw, *it.toolCachedPath,
                       it.toolExeHint  ? it.toolExeHint  : L"",
                       it.toolFriendly ? it.toolFriendly : L"");
        break;
    }
    DestroyWindow(hw);   // dismiss after acting
}

static LRESULT CALLBACK SectionModalProc(HWND hw, UINT msg,
                                         WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;   // WM_PAINT fully repaints; skip the flash

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

            // Stone background — sampled at (40,40) to match the cadence of
            // the other popups and the main window.
            if (Gdiplus::Bitmap* stone = AssetImage(L"bg_stone.png")) {
                int sw = (int)stone->GetWidth();
                int sh = (int)stone->GetHeight();
                int cropW = (sw < W) ? sw : W;
                int cropH = (sh < H) ? sh : H;
                Rect dst(0, 0, W, H);
                g.DrawImage(stone, dst, 40, 40, cropW, cropH, UnitPixel);
            } else {
                SolidBrush bg(Color(28, 24, 20));
                g.FillRectangle(&bg, 0, 0, W, H);
            }

            // Frame chrome — 9-slice frame_modbanner (corner 24).
            if (Gdiplus::Bitmap* frame = AssetImage(L"frame_modbanner.png")) {
                DrawButton9Slice(g, frame, 0, 0, W, H, 24);
            } else {
                Pen fallback(Tok::Bronze, 1.0f);
                g.DrawRectangle(&fallback, 1, 1, W - 3, H - 3);
            }

            // Title — centered across the top strip.
            SolidBrush titleBr(Tok::Gold);
            StringFormat sfT;
            sfT.SetAlignment(StringAlignmentCenter);
            sfT.SetLineAlignment(StringAlignmentCenter);
            Gdiplus::Font* titleFont = g_fModName ? g_fModName : g_fNavSm;
            if (titleFont) {
                g.DrawString(g_smTitle, -1, titleFont,
                    RectF((REAL)rc.left, (REAL)S(SM_TITLE_TOP_PAD),
                          (REAL)(rc.right - rc.left), (REAL)S(SM_TITLE_H)),
                    &sfT, &titleBr);
            }
            // Item + Close buttons are child HWNDs; they paint themselves
            // via WM_DRAWITEM. Nothing else to draw here.
        }
        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;
        if (PaintOwnerDrawButton(d)) return TRUE;
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDCANCEL) {   // Esc via IsDialogMessage
            DestroyWindow(hw);
            return 0;
        }
        if (id == SM_IDC_CLOSE) {
            DestroyWindow(hw);
            return 0;
        }
        if (id >= SM_IDC_ITEM_FIRST && id < SM_IDC_ITEM_FIRST + SM_MAX_ITEMS) {
            RunSectionItem(hw, id - SM_IDC_ITEM_FIRST);
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
        // Re-enable + refocus the parent, and clear our globals so a
        // subsequent open starts clean.
        if (HWND parent = GetWindow(hw, GW_OWNER)) {
            EnableWindow(parent, TRUE);
            SetActiveWindow(parent);
        }
        g_smHwnd = nullptr;
        for (int i = 0; i < SM_MAX_ITEMS; ++i) g_smItemBtns[i] = nullptr;
        g_smCloseBtn = nullptr;
        g_smItems = nullptr;
        g_smItemCount = 0;
        g_smTitle = L"";
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

void ShowSectionModal(HWND parent, const wchar_t* title,
                      const SectionItem* items, int itemCount) {
    if (g_smHwnd) return;                 // already open
    if (itemCount > SM_MAX_ITEMS) itemCount = SM_MAX_ITEMS;
    if (itemCount < 0) itemCount = 0;

    if (!g_smClassReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = SectionModalProc;
        wc.hInstance     = g_hInst;
        wc.lpszClassName = L"AngirisSectionModal";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        g_smClassReg = true;
    }

    // Bind active content BEFORE creating the window (WM_PAINT can fire
    // during CreateWindow/ShowWindow).
    g_smItems     = items;
    g_smItemCount = itemCount;
    g_smTitle     = title ? title : L"";

    // Grid drives both window size and button placement (single source).
    SectionGrid grid = ComputeSectionGrid(itemCount);
    int physW = (int)(grid.winW * g_dpiScale);
    int physH = (int)(grid.winH * g_dpiScale);

    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right  - pr.left) - physW) / 2;
    int y = pr.top  + ((pr.bottom - pr.top ) - physH) / 2;

    g_smHwnd = CreateWindowExW(
        0,  // owned popup: stays above its owner (launcher) without pinning over other apps                    // frame_modbanner is the border
        L"AngirisSectionModal",
        g_smTitle,
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, physW, physH,
        parent, nullptr, g_hInst, nullptr);
    if (!g_smHwnd) {
        g_smItems = nullptr; g_smItemCount = 0; g_smTitle = L"";
        return;
    }

    // Item buttons — column-major fill so the LEFT column fills first and
    // holds the extra when the count is odd (e.g. 5 items → 3 left, 2 right).
    int physItemW = (int)(SM_ITEM_W * g_dpiScale);
    int physItemH = (int)(SM_ITEM_H * g_dpiScale);
    int colPitch  = (int)((SM_ITEM_W + SM_ITEM_HGAP) * g_dpiScale);
    int rowPitch  = (int)((SM_ITEM_H + SM_ITEM_VGAP) * g_dpiScale);
    int gridLeftPhys = (int)(grid.gridLeft * g_dpiScale);
    int firstYPhys   = (int)(grid.firstRowY * g_dpiScale);
    for (int i = 0; i < itemCount; ++i) {
        int col = i / grid.rows;          // column-major: 0..rows-1 in col 0
        int row = i % grid.rows;
        int bx = gridLeftPhys + col * colPitch;
        int by = firstYPhys   + row * rowPitch;
        g_smItemBtns[i] = MkStdBtn(g_smHwnd,
                                   items[i].label,
                                   SM_IDC_ITEM_FIRST + i,
                                   bx, by, physItemW, physItemH,
                                   true, ButtonKind::NexusUpdate);
    }

    // Close button — centered, bottom-anchored.
    int physCloseW = (int)(SM_CLOSE_W * g_dpiScale);
    int physCloseH = (int)(SM_CLOSE_H * g_dpiScale);
    int closeX = (physW - physCloseW) / 2;
    int closeY = physH - (int)(SM_BOTTOM_PAD * g_dpiScale) - physCloseH;
    g_smCloseBtn = MkStdBtn(g_smHwnd, L"Close", SM_IDC_CLOSE,
                            closeX, closeY, physCloseW, physCloseH,
                            true, ButtonKind::Plugins);

    // Modal loop — parent disabled, nested pump until the popup closes.
    EnableWindow(parent, FALSE);
    ShowWindow(g_smHwnd, SW_SHOW);
    UpdateWindow(g_smHwnd);
    SetActiveWindow(g_smHwnd);

    MSG msg;
    while (g_smHwnd) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == 0 || got == -1) {
            if (got == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (g_smHwnd && IsDialogMessageW(g_smHwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}
