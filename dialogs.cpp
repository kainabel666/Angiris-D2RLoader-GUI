// ═══════════════════════════════════════════════════════════════════════
//  dialogs.cpp — see dialogs.h for the public interface
// ═══════════════════════════════════════════════════════════════════════
//
//  Implements the six themed modal dialogs. Each dialog has its own
//  wndproc (file-static) that paints a stone backdrop + gold border
//  + content, owner-draws its buttons via MainProc's WM_DRAWITEM
//  pipeline, and runs a local message pump until the user clicks
//  a button or closes the window.
//
//  WM_DRAWITEM forwarding: every dialog proc's WM_DRAWITEM case
//  calls PaintOwnerDrawButton (in buttons.cpp) so dialog buttons get
//  the same NexusUpdate visuals as the main window's controls.

#include "dialogs.h"
#include "core.h"        // g_hwMain, g_hInst, g_dpiScale
#include "scaling.h"     // S, SF, U, MemDC
#include "colors.h"      // Tok::Gold, Tok::Bronze, GP/GPA
#include "assets.h"      // AssetImage, DrawAssetStretched
#include "fonts.h"       // g_fModName, g_fBtn, g_fNavSm, ...
#include "layout.h"      // LO:: constants
#include "config.h"      // g_cfg, SaveCfg
#include "update_cache.h"
#include "control_ids.h" // IDC_LAUNCHER_RESTART_BTN, IDM_*
#include "mod_types.h"
#include "launcher_self_update.h"   // g_launcherUpdateLatestTag, g_forceUpdatePrompt,
                                    // StartLauncherUpdateInstall
#include "buttons.h"     // ButtonKind, MkStdBtn, PaintOwnerDrawButton

// Owner-drawn button paint is delegated to buttons.cpp via
// PaintOwnerDrawButton. Each dialog wndproc forwards its WM_DRAWITEM
// to that helper so dialog buttons paint identically to main-window
// buttons (same asset frames, same hover/click transforms, same gold
// text treatment).

// SetPath dialog re-uses Angiris.cpp's existing SHBrowseForFolder
// helper rather than duplicating the picker logic here. Defined in
// Angiris.cpp; non-static since Phase 7a/7b extraction.
extern bool PromptForD2RPath(HWND parent);

// ─────────────────────────────────────────────────────────────────────────
//  CONFLICT DIALOG  (themed modal — "Mod Folder Already Exists")
// ─────────────────────────────────────────────────────────────────────────
//
// Shown by the zip-install worker when a dropped mod's target folder
// already exists. Three NexusUpdate-styled buttons:
//
//   Update    → keep folder, copy only files whose paths exist in dest
//                (per the user's strict reading of "Update only overwrites
//                files found in the archive that are also found in the
//                folder")
//   Overwrite → wipe folder, extract fresh
//   Cancel    → leave folder untouched, skip to next queued zip
//
// The dialog uses bg_stone.png as its background (same texture as the
// loader-options panel for visual continuity) with a gold border and
// renders all text in g_fModName so it picks up the user's selected
// font automatically.
//
// Modal mechanics: the worker thread SendMessages into MainProc, which
// calls ShowConflictDialog inline. ShowConflictDialog runs a local
// message pump so the parent stays responsive (in terms of paint /
// non-input messages) while input is gated to the dialog via
// EnableWindow(parent, FALSE).
//
// Button paint is delegated back to MainProc's WM_DRAWITEM handler —
// MainProc's WM_DRAWITEM doesn't depend on the parent being g_hwMain,
// only on the d->hwndItem button HWND, so direct re-entry works.

static const wstring* g_dlgModNamePtr = nullptr;   // shared with paint
static HWND g_conflictDlg = nullptr;               // for re-entry guard

static LRESULT CALLBACK ConflictDlgProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;  // suppress flicker; WM_PAINT fills

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        int W = rc.right, H = rc.bottom;

        // Double-buffer to avoid the asset-blit flicker when the user
        // drags between buttons.
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

        {
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

            // Stone background — same source as the loader-options panel.
            // Cropped from the upper-left region of bg_stone so the
            // texture variation reads consistent with the main window.
            Bitmap* stone = AssetImage(L"bg_stone.png");
            if (stone) {
                int sw = (int)stone->GetWidth();
                int sh = (int)stone->GetHeight();
                // Crop a tile-sized region: the dialog is much smaller
                // than the full asset, so use a top-left slice that
                // matches the loader-options panel's sampling area.
                int cropW = (sw < W) ? sw : W;
                int cropH = (sh < H) ? sh : H;
                Rect dst(0, 0, W, H);
                g.DrawImage(stone, dst, 40, 40, cropW, cropH, UnitPixel);
            } else {
                SolidBrush bg(Color(28, 24, 20));
                g.FillRectangle(&bg, 0, 0, W, H);
            }

            // Gold border (2 px) — matches the main window frame.
            Pen border(Tok::Gold, 2.0f);
            g.DrawRectangle(&border, 1, 1, W - 3, H - 3);
            // Inner darker pen for the engraved feel.
            Pen innerB(Tok::Bronze, 1.0f);
            g.DrawRectangle(&innerB, 3, 3, W - 7, H - 7);

            // Text — uses g_fModName so the user's selected font applies.
            SolidBrush textBr(Tok::TextParchment);
            SolidBrush goldBr(Tok::Gold);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentNear);
            sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsNoWrap);

            int padX = (int)(24 * g_dpiScale);
            int y    = (int)(18 * g_dpiScale);
            int lineH= (int)(30 * g_dpiScale);

            // Title — "Mod Folder Already Exists" in gold accent.
            g.DrawString(L"Mod Folder Already Exists", -1,
                         g_fModName ? g_fModName : g_fBtn,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)lineH),
                         &sf, &goldBr);
            y += lineH;

            // Mod name in quotes on its own line.
            if (g_dlgModNamePtr && !g_dlgModNamePtr->empty()) {
                wstring nm = L"\u201C" + *g_dlgModNamePtr + L"\u201D";
                g.DrawString(nm.c_str(), -1,
                             g_fModName ? g_fModName : g_fBtn,
                             RectF((REAL)padX, (REAL)y,
                                   (REAL)(W - 2 * padX), (REAL)lineH),
                             &sf, &textBr);
            }
            y += lineH;
            y += (int)(8 * g_dpiScale);

            // Per-action descriptions — small body text, multi-line.
            int descY  = y;
            int descLH = (int)(20 * g_dpiScale);
            sf.SetLineAlignment(StringAlignmentCenter);

            auto descLine = [&](const wchar_t* s) {
                g.DrawString(s, -1, g_fBtn,
                             RectF((REAL)padX, (REAL)descY,
                                   (REAL)(W - 2 * padX), (REAL)descLH),
                             &sf, &textBr);
                descY += descLH;
            };
            descLine(L"Update overwrites only files present in the archive that already exist in the folder.");
            descLine(L"Overwrite erases the folder and extracts the archive fresh.");
            descLine(L"Cancel leaves the folder untouched.");
        }

        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        // Re-enter MainProc's WM_DRAWITEM so the dialog's owner-drawn
        // buttons paint with the same NexusUpdate visuals as the main
        // window's. MainProc keys off d->hwndItem, not the parent.
        if (PaintOwnerDrawButton((DRAWITEMSTRUCT*)lp)) return TRUE;
        return DefWindowProc(hw, msg, wp, lp);
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int* res = (int*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        if (res) {
            if      (id == 1) *res = 1;   // Update
            else if (id == 2) *res = 2;   // Overwrite
            else if (id == 3) *res = 0;   // Cancel
        }
        if (id >= 1 && id <= 3) DestroyWindow(hw);
        return 0;
    }

    case WM_CLOSE: {
        int* res = (int*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        if (res) *res = 0;
        DestroyWindow(hw);
        return 0;
    }

    case WM_DESTROY: {
        if (g_conflictDlg == hw) g_conflictDlg = nullptr;
        return 0;
    }
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

// Show the modal conflict dialog on the UI thread. Blocks until the user
// picks an action; returns 0=cancel / 1=update / 2=overwrite.
int ShowConflictDialog(HWND parent, const wstring& modName) {
    // Re-entry guard: shouldn't happen (worker SendMessages serially),
    // but if it ever does, decline the inner call rather than stack two
    // modal pumps.
    if (g_conflictDlg) return 0;

    static bool classReg = false;
    if (!classReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = ConflictDlgProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"AngirisConflictDlg";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        classReg = true;
    }

    // Dialog size in PHYSICAL pixels — scaled by DPI only (not by
    // g_userScale; the dialog isn't part of the user-scaled UI body).
    int dlgW = (int)(560 * g_dpiScale);
    int dlgH = (int)(280 * g_dpiScale);

    // Center on parent. AdjustWindowRectEx adds caption + border so the
    // CLIENT area ends up at dlgW × dlgH after the WS_CAPTION decoration.
    RECT pr; GetWindowRect(parent, &pr);
    RECT clientReq = { 0, 0, dlgW, dlgH };
    AdjustWindowRectEx(&clientReq, WS_POPUP | WS_CAPTION, FALSE, WS_EX_DLGMODALFRAME);
    int winW = clientReq.right - clientReq.left;
    int winH = clientReq.bottom - clientReq.top;
    int cx = (pr.left + pr.right) / 2 - winW / 2;
    int cy = (pr.top + pr.bottom) / 2 - winH / 2;

    int result = 0;
    g_dlgModNamePtr = &modName;

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"AngirisConflictDlg",
        L"Mod Folder Already Exists",
        WS_POPUP | WS_CAPTION,
        cx, cy, winW, winH,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!dlg) {
        g_dlgModNamePtr = nullptr;
        return 0;
    }
    g_conflictDlg = dlg;
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&result);

    // Three NexusUpdate-styled buttons at the bottom of the client area.
    // The native NexusUpdate asset is 254×54 — for the dialog we scale
    // down to ~170×46 (physical px) so all three fit comfortably inside
    // the 560 px dialog without their decorative edges bleeding into
    // the bronze border. Outer buttons get an additional 5 px inward
    // nudge so the left/right border padding is symmetric (was a tight
    // fit before — at 1.0 DPI scale the row was 568 wide in a 560
    // dialog and the outer chevron ornaments bled over the frame).
    int btnW    = (int)(170 * g_dpiScale);
    int btnH    = (int)(46  * g_dpiScale);
    int btnGap  = (int)(14  * g_dpiScale);
    int nudge   = (int)( 5  * g_dpiScale);   // outer buttons toward center

    RECT cr; GetClientRect(dlg, &cr);
    int rowW  = 3 * btnW + 2 * btnGap;
    int rowX0 = (cr.right - rowW) / 2;
    int rowY  = cr.bottom - btnH - (int)(20 * g_dpiScale);

    MkStdBtn(dlg, L"Update",    1, rowX0 + nudge,                       rowY, btnW, btnH,
             true, ButtonKind::NexusUpdate);
    MkStdBtn(dlg, L"Overwrite", 2, rowX0 + (btnW + btnGap),             rowY, btnW, btnH,
             true, ButtonKind::NexusUpdate);
    MkStdBtn(dlg, L"Cancel",    3, rowX0 + 2 * (btnW + btnGap) - nudge, rowY, btnW, btnH,
             true, ButtonKind::NexusUpdate);

    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    EnableWindow(parent, FALSE);

    // Local modal pump — runs until the dialog is destroyed (one of the
    // three buttons clicked, the close box, or Esc). IsDialogMessageW
    // handles Tab cycling between the buttons automatically.
    MSG msg;
    bool done = false;
    while (!done) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {
            // WM_QUIT or error — break out without committing a choice.
            if (got == 0) PostQuitMessage((int)msg.wParam);
            done = true;
            break;
        }
        // Esc = cancel, Enter = update (the safe default).
        if (msg.message == WM_KEYDOWN && msg.hwnd == dlg) {
            if (msg.wParam == VK_ESCAPE) {
                result = 0;
                done = true;
                break;
            }
            if (msg.wParam == VK_RETURN) {
                result = 1;
                done = true;
                break;
            }
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(dlg)) done = true;
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (IsWindow(dlg)) DestroyWindow(dlg);
    g_dlgModNamePtr = nullptr;
    g_conflictDlg = nullptr;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────
//  LAUNCHER UPDATE DIALOG  (themed modal — Update / Skip Version / Ignore)
// ─────────────────────────────────────────────────────────────────────────
//
// Same paint/dispatch pattern as ConflictDlg: stone background, gold +
// bronze border, three NexusUpdate-styled buttons, Esc = Ignore,
// Enter = Update. State travels via two file-scope pointers so the
// paint code can show the live tag values; the result lands in an int
// pointed to by GWLP_USERDATA so the modal pump can return it.

static const wstring* g_dlgLatestTagPtr  = nullptr;
static const wstring* g_dlgCurrentTagPtr = nullptr;
static HWND           g_launcherUpdateDlg = nullptr;

static LRESULT CALLBACK LauncherUpdateDlgProc(HWND hw, UINT msg,
                                              WPARAM wp, LPARAM lp) {
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
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

            Bitmap* stone = AssetImage(L"bg_stone.png");
            if (stone) {
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
            Pen border(Tok::Gold, 2.0f);
            g.DrawRectangle(&border, 1, 1, W - 3, H - 3);
            Pen innerB(Tok::Bronze, 1.0f);
            g.DrawRectangle(&innerB, 3, 3, W - 7, H - 7);

            SolidBrush textBr(Tok::TextParchment);
            SolidBrush goldBr(Tok::Gold);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentNear);
            sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsNoWrap);

            int padX  = (int)(24 * g_dpiScale);
            int y     = (int)(18 * g_dpiScale);
            int lineH = (int)(30 * g_dpiScale);

            Font* titleFont = g_fModName ? g_fModName : g_fBtn;
            g.DrawString(L"Launcher Update Available", -1, titleFont,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)lineH),
                         &sf, &goldBr);
            y += lineH;

            // Version line: "Version <latest> is available (current <vN>)"
            wstring versionLine = L"Version ";
            versionLine += (g_dlgLatestTagPtr && !g_dlgLatestTagPtr->empty())
                ? *g_dlgLatestTagPtr : L"?";
            versionLine += L" is available (you have ";
            versionLine += (g_dlgCurrentTagPtr && !g_dlgCurrentTagPtr->empty())
                ? *g_dlgCurrentTagPtr : L"?";
            versionLine += L")";
            g.DrawString(versionLine.c_str(), -1, titleFont,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)lineH),
                         &sf, &textBr);
            y += lineH;
            y += (int)(8 * g_dpiScale);

            // Per-action descriptions — same descLine pattern as
            // ShowConflictDialog so the visual rhythm matches.
            int descY  = y;
            int descLH = (int)(20 * g_dpiScale);
            sf.SetLineAlignment(StringAlignmentCenter);
            auto descLine = [&](const wchar_t* s) {
                g.DrawString(s, -1, g_fBtn,
                             RectF((REAL)padX, (REAL)descY,
                                   (REAL)(W - 2 * padX), (REAL)descLH),
                             &sf, &textBr);
                descY += descLH;
            };
            descLine(L"Update downloads the new release and replaces the running launcher.");
            descLine(L"Skip Version stops prompting for this release; you'll be notified for newer ones.");
            descLine(L"Ignore continues normally and re-prompts on the next launch.");
        }
        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_DRAWITEM:
        // Re-enter MainProc's WM_DRAWITEM so the buttons paint as
        // NexusUpdate just like ConflictDlg's do. Owner-drawn buttons
        // key off d->hwndItem, so the dispatching window doesn't have
        // to be g_hwMain.
        if (PaintOwnerDrawButton((DRAWITEMSTRUCT*)lp)) return TRUE;
        return DefWindowProc(hw, msg, wp, lp);

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int* res = (int*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        if (res) {
            if      (id == 1) *res = 1;   // Update
            else if (id == 2) *res = 2;   // Skip Version
            else if (id == 3) *res = 0;   // Ignore
        }
        if (id >= 1 && id <= 3) DestroyWindow(hw);
        return 0;
    }

    case WM_CLOSE: {
        int* res = (int*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        if (res) *res = 0;
        DestroyWindow(hw);
        return 0;
    }

    case WM_DESTROY:
        if (g_launcherUpdateDlg == hw) g_launcherUpdateDlg = nullptr;
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

int ShowLauncherUpdateDialog(HWND parent, const wstring& latestTag) {
    if (g_launcherUpdateDlg) return 0;   // re-entry guard

    static bool classReg = false;
    if (!classReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = LauncherUpdateDlgProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"AngirisLauncherUpdateDlg";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        classReg = true;
    }

    int dlgW = (int)(560 * g_dpiScale);
    int dlgH = (int)(260 * g_dpiScale);

    RECT pr; GetWindowRect(parent, &pr);
    RECT clientReq = { 0, 0, dlgW, dlgH };
    AdjustWindowRectEx(&clientReq, WS_POPUP | WS_CAPTION, FALSE, WS_EX_DLGMODALFRAME);
    int winW = clientReq.right - clientReq.left;
    int winH = clientReq.bottom - clientReq.top;
    int cx = (pr.left + pr.right) / 2 - winW / 2;
    int cy = (pr.top + pr.bottom) / 2 - winH / 2;

    int result = 0;
    wstring currentVer = LAUNCHER_VERSION;
    g_dlgLatestTagPtr  = &latestTag;
    g_dlgCurrentTagPtr = &currentVer;

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"AngirisLauncherUpdateDlg",
        L"Launcher Update Available",
        WS_POPUP | WS_CAPTION,
        cx, cy, winW, winH,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!dlg) {
        g_dlgLatestTagPtr = g_dlgCurrentTagPtr = nullptr;
        return 0;
    }
    g_launcherUpdateDlg = dlg;
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&result);

    // Same three-button row geometry as the conflict dialog so the
    // two share a visual rhythm.
    int btnW   = (int)(170 * g_dpiScale);
    int btnH   = (int)(46  * g_dpiScale);
    int btnGap = (int)(14  * g_dpiScale);
    int nudge  = (int)( 5  * g_dpiScale);

    RECT cr; GetClientRect(dlg, &cr);
    int rowW  = 3 * btnW + 2 * btnGap;
    int rowX0 = (cr.right - rowW) / 2;
    int rowY  = cr.bottom - btnH - (int)(20 * g_dpiScale);

    MkStdBtn(dlg, L"Update",       1, rowX0 + nudge,                       rowY, btnW, btnH,
             true, ButtonKind::NexusUpdate);
    MkStdBtn(dlg, L"Skip Version", 2, rowX0 + (btnW + btnGap),             rowY, btnW, btnH,
             true, ButtonKind::NexusUpdate);
    MkStdBtn(dlg, L"Ignore",       3, rowX0 + 2 * (btnW + btnGap) - nudge, rowY, btnW, btnH,
             true, ButtonKind::NexusUpdate);

    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    EnableWindow(parent, FALSE);

    MSG msg;
    bool done = false;
    while (!done) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage((int)msg.wParam);
            done = true; break;
        }
        // Esc = Ignore, Enter = Update.
        if (msg.message == WM_KEYDOWN && msg.hwnd == dlg) {
            if (msg.wParam == VK_ESCAPE) { result = 0; done = true; break; }
            if (msg.wParam == VK_RETURN) { result = 1; done = true; break; }
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(dlg)) done = true;
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (IsWindow(dlg)) DestroyWindow(dlg);
    g_dlgLatestTagPtr = g_dlgCurrentTagPtr = nullptr;
    g_launcherUpdateDlg = nullptr;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────
//  NO MODINFO DIALOG  (themed modal — "Zip didn't contain modinfo.json")
// ─────────────────────────────────────────────────────────────────────────
//
// Shown when a dropped zip has no modinfo.json anywhere in its tree.
// Single OK button, message names the offending zip so the user can tell
// it apart in a multi-drop. Returns nothing — the worker just skips
// this zip and moves to the next.

static const wstring* g_dlgZipNamePtr = nullptr;   // shared with paint
static HWND g_noModInfoDlg = nullptr;

static LRESULT CALLBACK NoModInfoDlgProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
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
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

            Bitmap* stone = AssetImage(L"bg_stone.png");
            if (stone) {
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
            Pen border(Tok::Gold, 2.0f);
            g.DrawRectangle(&border, 1, 1, W - 3, H - 3);
            Pen innerB(Tok::Bronze, 1.0f);
            g.DrawRectangle(&innerB, 3, 3, W - 7, H - 7);

            SolidBrush textBr(Tok::TextParchment);
            SolidBrush goldBr(Tok::Gold);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsNoWrap);

            int padX = (int)(24 * g_dpiScale);
            int y    = (int)(24 * g_dpiScale);
            int lineH= (int)(30 * g_dpiScale);

            // Title — "Invalid Mod Archive" in gold.
            g.DrawString(L"Invalid Mod Archive", -1,
                         g_fModName ? g_fModName : g_fBtn,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)lineH),
                         &sf, &goldBr);
            y += lineH;
            y += (int)(10 * g_dpiScale);

            // Body — wrapped over two lines. Don't NoWrap here because the
            // zip filename can be long and we want it to break naturally
            // rather than overflow the rect.
            StringFormat sfWrap;
            sfWrap.SetAlignment(StringAlignmentCenter);
            sfWrap.SetLineAlignment(StringAlignmentNear);
            int bodyH = (int)(60 * g_dpiScale);
            wstring msgText = (g_dlgZipNamePtr ? *g_dlgZipNamePtr : wstring())
                + L" did not contain a modinfo.json file, please contact the mod author.";
            g.DrawString(msgText.c_str(), -1, g_fBtn,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)bodyH),
                         &sfWrap, &textBr);
        }
        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_DRAWITEM:
        if (PaintOwnerDrawButton((DRAWITEMSTRUCT*)lp)) return TRUE;
        return DefWindowProc(hw, msg, wp, lp);

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == 1) DestroyWindow(hw);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hw);
        return 0;

    case WM_DESTROY:
        if (g_noModInfoDlg == hw) g_noModInfoDlg = nullptr;
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

void ShowNoModInfoDialog(HWND parent, const wstring& zipName) {
    if (g_noModInfoDlg) return;
    static bool classReg = false;
    if (!classReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = NoModInfoDlgProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"AngirisNoModInfoDlg";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        classReg = true;
    }

    int dlgW = (int)(560 * g_dpiScale);
    int dlgH = (int)(220 * g_dpiScale);
    RECT pr; GetWindowRect(parent, &pr);
    RECT clientReq = { 0, 0, dlgW, dlgH };
    AdjustWindowRectEx(&clientReq, WS_POPUP | WS_CAPTION, FALSE, WS_EX_DLGMODALFRAME);
    int winW = clientReq.right - clientReq.left;
    int winH = clientReq.bottom - clientReq.top;
    int cx = (pr.left + pr.right) / 2 - winW / 2;
    int cy = (pr.top + pr.bottom) / 2 - winH / 2;

    g_dlgZipNamePtr = &zipName;
    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"AngirisNoModInfoDlg",
        L"Invalid Mod Archive",
        WS_POPUP | WS_CAPTION,
        cx, cy, winW, winH,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!dlg) { g_dlgZipNamePtr = nullptr; return; }
    g_noModInfoDlg = dlg;

    // Single centered OK button.
    int btnW = (int)(160 * g_dpiScale);
    int btnH = (int)(46  * g_dpiScale);
    RECT cr; GetClientRect(dlg, &cr);
    int btnX = (cr.right - btnW) / 2;
    int btnY = cr.bottom - btnH - (int)(20 * g_dpiScale);
    MkStdBtn(dlg, L"OK", 1, btnX, btnY, btnW, btnH, true, ButtonKind::NexusUpdate);

    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    EnableWindow(parent, FALSE);

    MSG msg;
    while (IsWindow(dlg)) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (msg.message == WM_KEYDOWN && msg.hwnd == dlg) {
            if (msg.wParam == VK_ESCAPE || msg.wParam == VK_RETURN) {
                DestroyWindow(dlg);
                break;
            }
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (IsWindow(dlg)) DestroyWindow(dlg);
    g_dlgZipNamePtr = nullptr;
    g_noModInfoDlg = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────
//  UNINSTALL CONFIRM DIALOG  (themed modal — "Delete <mod>?")
// ─────────────────────────────────────────────────────────────────────────
//
// Shown from the mod-list right-click context menu when the user picks
// Uninstall. Two NexusUpdate-styled buttons:
//   Cancel  → leaves the mod alone (default; Esc and the close-X also
//             route here)
//   Delete  → returns 1 to the caller, which then DeleteFolderRecursive's
//             the mod's directory
//
// Cancel is positioned on the left and is the default focus, so a
// reflexive Enter press dismisses the dialog without deleting. Delete
// requires an explicit click — there's no keyboard shortcut for it,
// because muscle memory should not be able to nuke a mod by accident.

static const wstring* g_dlgUninstallNamePtr = nullptr;   // shared with paint
static HWND g_uninstallDlg = nullptr;

static LRESULT CALLBACK UninstallDlgProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
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
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

            Bitmap* stone = AssetImage(L"bg_stone.png");
            if (stone) {
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
            Pen border(Tok::Gold, 2.0f);
            g.DrawRectangle(&border, 1, 1, W - 3, H - 3);
            Pen innerB(Tok::Bronze, 1.0f);
            g.DrawRectangle(&innerB, 3, 3, W - 7, H - 7);

            SolidBrush textBr(Tok::TextParchment);
            SolidBrush goldBr(Tok::Gold);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsNoWrap);

            int padX = (int)(24 * g_dpiScale);
            int y    = (int)(22 * g_dpiScale);
            int lineH= (int)(30 * g_dpiScale);

            g.DrawString(L"Delete Mod", -1,
                         g_fModName ? g_fModName : g_fBtn,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)lineH),
                         &sf, &goldBr);
            y += lineH;

            // Mod name in quotes — visually anchors which mod is on the
            // chopping block, even when modnames are long.
            if (g_dlgUninstallNamePtr && !g_dlgUninstallNamePtr->empty()) {
                wstring nm = L"\u201C" + *g_dlgUninstallNamePtr + L"\u201D";
                g.DrawString(nm.c_str(), -1,
                             g_fModName ? g_fModName : g_fBtn,
                             RectF((REAL)padX, (REAL)y,
                                   (REAL)(W - 2 * padX), (REAL)lineH),
                             &sf, &textBr);
            }
            y += lineH;
            y += (int)(8 * g_dpiScale);

            StringFormat sfWrap;
            sfWrap.SetAlignment(StringAlignmentCenter);
            sfWrap.SetLineAlignment(StringAlignmentNear);
            int bodyH = (int)(60 * g_dpiScale);
            g.DrawString(
                L"This will permanently delete the mod's folder and all its files. This cannot be undone.",
                -1, g_fBtn,
                RectF((REAL)padX, (REAL)y,
                      (REAL)(W - 2 * padX), (REAL)bodyH),
                &sfWrap, &textBr);
        }
        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_DRAWITEM:
        if (PaintOwnerDrawButton((DRAWITEMSTRUCT*)lp)) return TRUE;
        return DefWindowProc(hw, msg, wp, lp);

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int* res = (int*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        if (res) {
            if      (id == 1) *res = 0;   // Cancel
            else if (id == 2) *res = 1;   // Delete
        }
        if (id == 1 || id == 2) DestroyWindow(hw);
        return 0;
    }

    case WM_CLOSE: {
        int* res = (int*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        if (res) *res = 0;
        DestroyWindow(hw);
        return 0;
    }

    case WM_DESTROY:
        if (g_uninstallDlg == hw) g_uninstallDlg = nullptr;
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

int ShowUninstallConfirmDialog(HWND parent, const wstring& modName) {
    if (g_uninstallDlg) return 0;
    static bool classReg = false;
    if (!classReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = UninstallDlgProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"AngirisUninstallDlg";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        classReg = true;
    }

    int dlgW = (int)(560 * g_dpiScale);
    int dlgH = (int)(260 * g_dpiScale);
    RECT pr; GetWindowRect(parent, &pr);
    RECT clientReq = { 0, 0, dlgW, dlgH };
    AdjustWindowRectEx(&clientReq, WS_POPUP | WS_CAPTION, FALSE, WS_EX_DLGMODALFRAME);
    int winW = clientReq.right - clientReq.left;
    int winH = clientReq.bottom - clientReq.top;
    int cx = (pr.left + pr.right) / 2 - winW / 2;
    int cy = (pr.top + pr.bottom) / 2 - winH / 2;

    int result = 0;
    g_dlgUninstallNamePtr = &modName;
    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"AngirisUninstallDlg",
        L"Delete Mod",
        WS_POPUP | WS_CAPTION,
        cx, cy, winW, winH,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!dlg) { g_dlgUninstallNamePtr = nullptr; return 0; }
    g_uninstallDlg = dlg;
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&result);

    int btnW   = (int)(170 * g_dpiScale);
    int btnH   = (int)(46  * g_dpiScale);
    int btnGap = (int)(16  * g_dpiScale);
    RECT cr; GetClientRect(dlg, &cr);
    int rowW = 2 * btnW + btnGap;
    int rowX = (cr.right - rowW) / 2;
    int rowY = cr.bottom - btnH - (int)(20 * g_dpiScale);
    // Cancel on the left so the user's reflexive default action sits
    // closer to where they'd naturally land. id=1 (Cancel) is also the
    // initial focus so Enter triggers Cancel.
    HWND bCancel = MkStdBtn(dlg, L"Cancel", 1, rowX,                  rowY,
                            btnW, btnH, true, ButtonKind::NexusUpdate);
    MkStdBtn(dlg, L"Delete",                2, rowX + btnW + btnGap,  rowY,
             btnW, btnH, true, ButtonKind::NexusUpdate);

    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    EnableWindow(parent, FALSE);
    SetFocus(bCancel);    // Enter → Cancel, the safe default

    MSG msg;
    while (IsWindow(dlg)) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (msg.message == WM_KEYDOWN && msg.hwnd == dlg) {
            if (msg.wParam == VK_ESCAPE) {
                result = 0;
                DestroyWindow(dlg);
                break;
            }
            // Deliberately no Enter shortcut for Delete — destructive
            // action must be an explicit click. (Enter while Cancel
            // has focus naturally routes to Cancel via IsDialogMessage.)
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (IsWindow(dlg)) DestroyWindow(dlg);
    g_dlgUninstallNamePtr = nullptr;
    g_uninstallDlg = nullptr;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────
//  SET PATH DIALOG  (themed modal — "D2R install folder not configured")
// ─────────────────────────────────────────────────────────────────────────
//
// Shown when a zip is dropped but the launcher doesn't know where D2R
// lives. Two buttons:
//   Set Path  → runs the SHBrowseForFolder picker (same as the rail
//               "..." button); on success the worker continues with the
//               current zip
//   Cancel    → skips this zip; remaining queued zips will hit the same
//               dialog again on their turn (unless one of them set the
//               path)
//
// Returns 1 if a path was set (worker continues with the zip), 0 if
// cancelled (worker skips the zip).

static HWND g_setPathDlg = nullptr;

static LRESULT CALLBACK SetPathDlgProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
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
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

            Bitmap* stone = AssetImage(L"bg_stone.png");
            if (stone) {
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
            Pen border(Tok::Gold, 2.0f);
            g.DrawRectangle(&border, 1, 1, W - 3, H - 3);
            Pen innerB(Tok::Bronze, 1.0f);
            g.DrawRectangle(&innerB, 3, 3, W - 7, H - 7);

            SolidBrush textBr(Tok::TextParchment);
            SolidBrush goldBr(Tok::Gold);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsNoWrap);

            int padX = (int)(24 * g_dpiScale);
            int y    = (int)(22 * g_dpiScale);
            int lineH= (int)(30 * g_dpiScale);

            g.DrawString(L"D2R Install Folder Not Set", -1,
                         g_fModName ? g_fModName : g_fBtn,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)lineH),
                         &sf, &goldBr);
            y += lineH;
            y += (int)(12 * g_dpiScale);

            StringFormat sfWrap;
            sfWrap.SetAlignment(StringAlignmentCenter);
            sfWrap.SetLineAlignment(StringAlignmentNear);
            int bodyH = (int)(60 * g_dpiScale);
            g.DrawString(L"The launcher needs to know where Diablo II: Resurrected is installed before it can extract mods. Set the folder now to continue installing, or cancel to skip this archive.",
                         -1, g_fBtn,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)bodyH),
                         &sfWrap, &textBr);
        }
        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_DRAWITEM:
        if (PaintOwnerDrawButton((DRAWITEMSTRUCT*)lp)) return TRUE;
        return DefWindowProc(hw, msg, wp, lp);

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int* res = (int*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        if (res) {
            if      (id == 1) *res = 1;   // Set Path
            else if (id == 2) *res = 0;   // Cancel
        }
        if (id == 1 || id == 2) DestroyWindow(hw);
        return 0;
    }

    case WM_CLOSE: {
        int* res = (int*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        if (res) *res = 0;
        DestroyWindow(hw);
        return 0;
    }

    case WM_DESTROY:
        if (g_setPathDlg == hw) g_setPathDlg = nullptr;
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

// Returns 1 if user picked "Set Path" AND successfully chose a folder
// (g_cfg.d2rPath is now populated). Returns 0 otherwise (Cancel, Esc,
// or the SHBrowseForFolder picker was itself cancelled).
int ShowSetPathDialog(HWND parent) {
    if (g_setPathDlg) return 0;
    static bool classReg = false;
    if (!classReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = SetPathDlgProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"AngirisSetPathDlg";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        classReg = true;
    }

    int dlgW = (int)(580 * g_dpiScale);
    int dlgH = (int)(260 * g_dpiScale);
    RECT pr; GetWindowRect(parent, &pr);
    RECT clientReq = { 0, 0, dlgW, dlgH };
    AdjustWindowRectEx(&clientReq, WS_POPUP | WS_CAPTION, FALSE, WS_EX_DLGMODALFRAME);
    int winW = clientReq.right - clientReq.left;
    int winH = clientReq.bottom - clientReq.top;
    int cx = (pr.left + pr.right) / 2 - winW / 2;
    int cy = (pr.top + pr.bottom) / 2 - winH / 2;

    int result = 0;
    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"AngirisSetPathDlg",
        L"D2R Install Folder Not Set",
        WS_POPUP | WS_CAPTION,
        cx, cy, winW, winH,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!dlg) return 0;
    g_setPathDlg = dlg;
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&result);

    int btnW = (int)(170 * g_dpiScale);
    int btnH = (int)(46  * g_dpiScale);
    int btnGap = (int)(16 * g_dpiScale);
    RECT cr; GetClientRect(dlg, &cr);
    int rowW = 2 * btnW + btnGap;
    int rowX = (cr.right - rowW) / 2;
    int rowY = cr.bottom - btnH - (int)(20 * g_dpiScale);
    MkStdBtn(dlg, L"Set Path", 1, rowX,                  rowY, btnW, btnH,
             true, ButtonKind::NexusUpdate);
    MkStdBtn(dlg, L"Cancel",   2, rowX + btnW + btnGap,  rowY, btnW, btnH,
             true, ButtonKind::NexusUpdate);

    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    EnableWindow(parent, FALSE);

    MSG msg;
    while (IsWindow(dlg)) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (msg.message == WM_KEYDOWN && msg.hwnd == dlg) {
            if (msg.wParam == VK_ESCAPE) {
                result = 0;
                DestroyWindow(dlg);
                break;
            }
            if (msg.wParam == VK_RETURN) {
                result = 1;
                DestroyWindow(dlg);
                break;
            }
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (IsWindow(dlg)) DestroyWindow(dlg);
    g_setPathDlg = nullptr;

    // If user picked "Set Path", run the folder picker. Returns 1 only if
    // a folder was actually chosen — Cancel from the picker rolls back
    // to 0 and the worker will skip this zip.
    if (result == 1) {
        bool got = PromptForD2RPath(parent);
        if (!got) result = 0;
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────
//  PROGRESS DIALOG  (themed modeless — install in progress with 5-stage bar)
// ─────────────────────────────────────────────────────────────────────────
//
// Shown when the install worker starts on its first zip, hidden when the
// queue drains. Disables the main window so the rest of the UI is paused
// for the duration of the install. The 5-stage bar uses progress_bar_0
// through progress_bar_4 assets (the 5 rows of the supplied artwork —
// empty / ~25% / ~50% / ~75% / full).
//
// Stage mapping (per zip):
//   0  starting
//   1  extracted to temp
//   2  located modinfo
//   3  copying files
//   4  done
//
// Worker advances stages via SendMessage(MSG_ZIP_PROGRESS_UPDATE). The
// UI thread handler updates the globals and invalidates the dialog's
// client area.

// Progress dialog state (file-local). UpdateProgressDialog writes; the
// proc + ShowProgressDialog + HideProgressDialog + GetProgressDialog
// read. None of these are exposed externally — Angiris.cpp uses the
// GetProgressDialog() accessor to read the HWND only.
static HWND     g_progressDlg        = nullptr;
static int      g_progressStage      = 0;     // 0..4 progress bar stage
static int      g_progressZipIdx     = 0;     // current zip in batch (1-based)
static int      g_progressZipTotal   = 0;     // total zips in batch
static wstring  g_progressZipName;            // "<archive>.zip" being installed
static wstring  g_progressStageLabel;         // "Extracting archive...", etc.

// Progress state declarations live further up in the file (near the
// g_progressDlg HWND declaration) so the launcher self-update worker
// can write into them — that worker is defined before this point.

static LRESULT CALLBACK ProgressDlgProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
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
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

            // Themed background — same stone + double border as the
            // other install dialogs.
            Bitmap* stone = AssetImage(L"bg_stone.png");
            if (stone) {
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
            Pen border(Tok::Gold, 2.0f);
            g.DrawRectangle(&border, 1, 1, W - 3, H - 3);
            Pen innerB(Tok::Bronze, 1.0f);
            g.DrawRectangle(&innerB, 3, 3, W - 7, H - 7);

            SolidBrush textBr(Tok::TextParchment);
            SolidBrush goldBr(Tok::Gold);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsNoWrap);

            int padX  = (int)(24 * g_dpiScale);
            int y     = (int)(22 * g_dpiScale);
            int lineH = (int)(30 * g_dpiScale);

            // Title line — zip install: "Installing Mod" (with N-of-M
            // suffix when batching). Launcher self-update no longer
            // uses this dialog; it has its own popup window.
            wstring title = L"Installing Mod";
            if (g_progressZipTotal > 1) {
                wchar_t buf[64];
                swprintf_s(buf, 64, L"Installing Mod %d of %d",
                           g_progressZipIdx, g_progressZipTotal);
                title = buf;
            }
            g.DrawString(title.c_str(), -1,
                         g_fModName ? g_fModName : g_fBtn,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)lineH),
                         &sf, &goldBr);
            y += lineH;

            // Filename line
            g.DrawString(g_progressZipName.c_str(), -1,
                         g_fModName ? g_fModName : g_fBtn,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)lineH),
                         &sf, &textBr);
            y += lineH;
            y += (int)(10 * g_dpiScale);

            // Stage label
            g.DrawString(g_progressStageLabel.c_str(), -1, g_fBtn,
                         RectF((REAL)padX, (REAL)y,
                               (REAL)(W - 2 * padX), (REAL)lineH),
                         &sf, &textBr);
            y += lineH;
            y += (int)(8 * g_dpiScale);

            // ── 5-stage progress bar ──────────────────────────────────────
            // Each of progress_bar_0.png .. progress_bar_4.png is a full
            // bar drawn at the same logical width; only the fill amount
            // differs. We just pick the asset matching g_progressStage.
            int barX = padX;
            int barW = W - 2 * padX;
            int barH = (int)(34 * g_dpiScale);
            int barY = y;
            int stage = g_progressStage;
            if (stage < 0) stage = 0;
            if (stage > 4) stage = 4;
            wchar_t assetName[32];
            swprintf_s(assetName, 32, L"progress_bar_%d.png", stage);
            Bitmap* barBM = AssetImage(assetName);
            if (barBM) {
                // Render the asset at the dialog's bar geometry, using
                // 9-slice if the asset is much wider than the target so
                // the decorative end caps stay crisp.
                int aw = (int)barBM->GetWidth();
                int ah = (int)barBM->GetHeight();
                // 9-slice with 60 px corner inset (matches the chevron
                // ornament width in the supplied artwork). Falls back
                // to a stretched draw if the asset is too small for the
                // inset.
                int inset = 60;
                if (aw > inset * 2 + 10 && ah > 0) {
                    DrawButton9Slice(g, barBM, barX, barY, barW, barH, inset);
                } else {
                    InterpolationMode prev = g.GetInterpolationMode();
                    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                    g.DrawImage(barBM, barX, barY, barW, barH);
                    g.SetInterpolationMode(prev);
                }
            } else {
                // Asset-less fallback — a programmatic bar so the dialog
                // is still useful while the user is preparing the .png
                // assets. Track + fill, no theming.
                Pen trackPen(Tok::Bronze, 1.0f);
                g.DrawRectangle(&trackPen, barX, barY, barW - 1, barH - 1);
                int fillW = (barW - 4) * stage / 4;
                SolidBrush fillBr(Tok::Gold);
                if (fillW > 0)
                    g.FillRectangle(&fillBr, barX + 2, barY + 2, fillW, barH - 4);
            }
        }
        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_CLOSE:
        // Suppress — the user shouldn't close the progress dialog
        // mid-install. It closes itself when the queue drains.
        return 0;

    case WM_DESTROY:
        if (g_progressDlg == hw) g_progressDlg = nullptr;
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

HWND ShowProgressDialog(HWND parent) {
    if (g_progressDlg) return g_progressDlg;

    static bool classReg = false;
    if (!classReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = ProgressDlgProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"AngirisProgressDlg";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        classReg = true;
    }

    int dlgW = (int)(620 * g_dpiScale);
    int dlgH = (int)(260 * g_dpiScale);
    RECT pr; GetWindowRect(parent, &pr);
    // Borderless (no WS_CAPTION) — the title is rendered in the body so
    // there's no chrome to close accidentally.
    int cx = (pr.left + pr.right) / 2 - dlgW / 2;
    int cy = (pr.top + pr.bottom) / 2 - dlgH / 2;

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"AngirisProgressDlg",
        L"Installing Mods",
        WS_POPUP | WS_CLIPCHILDREN,
        cx, cy, dlgW, dlgH,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!dlg) return nullptr;
    g_progressDlg = dlg;
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    EnableWindow(parent, FALSE);   // pause the main UI
    return dlg;
}

void UpdateProgressDialog(const ProgressUpdate& p) {
    g_progressStage      = p.stage;
    g_progressZipIdx     = p.zipIdx;
    g_progressZipTotal   = p.zipTotal;
    g_progressZipName    = p.zipName;
    g_progressStageLabel = p.stageLabel;

    if (g_progressDlg) {
        InvalidateRect(g_progressDlg, nullptr, FALSE);
        UpdateWindow(g_progressDlg);   // synchronous repaint — important
                                       // because the worker is waiting on
                                       // this SendMessage round-trip and
                                       // we want the new stage visible
                                       // before it proceeds.
    }
}

void HideProgressDialog() {
    if (!g_progressDlg) return;
    HWND p = (HWND)GetWindowLongPtrW(g_progressDlg, GWLP_HWNDPARENT);
    if (p) EnableWindow(p, TRUE);
    DestroyWindow(g_progressDlg);
    g_progressDlg = nullptr;
    g_progressZipName.clear();
    g_progressStageLabel.clear();
    g_progressZipIdx = 0;
    g_progressZipTotal = 0;
    g_progressStage = 0;
    if (p) SetForegroundWindow(p);
}

// File-local progress dialog state is private; expose just enough for the
// host code to pick a sensible parent for any modal that wants to stack
// over the progress dialog while it's up.
HWND GetProgressDialog() { return g_progressDlg; }

