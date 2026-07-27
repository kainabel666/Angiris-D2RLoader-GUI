// ═══════════════════════════════════════════════════════════════════════
//  about_modal.cpp — About modal (version info + links + text reader)
// ═══════════════════════════════════════════════════════════════════════
//
//  Two view modes in one window:
//
//    HUB   — the default. Shows the Angiris launcher version and the
//            detected D2RLoader version, then a prominent "Download
//            D2RLoader" button and a row of smaller link buttons
//            (README, FAQ, Discord). Download + Discord open URLs via
//            ShellExecute; README / FAQ switch the window into…
//
//    READER — a scrollable text view that renders README.txt or FAQ.txt
//            inside the modal (owner-drawn EDIT, read-only) with a
//            "Back" button returning to the hub. Keeps everything in
//            one themed window instead of bouncing out to Notepad.
//
//  Frame/stone/button chrome mirrors loader_options_modal + plugin
//  manager for visual consistency.

#include "about_modal.h"
#include "core.h"          // g_hInst, g_dpiScale, AppDir, ReadTextFile
#include "config.h"        // g_cfg (D2R path, for the D2RLoader.exe probe)
#include "scaling.h"       // S()
#include "colors.h"        // Tok::Gold, Tok::TextParchment, etc.
#include "fonts.h"         // g_fModName, g_fBtn, g_fSubLbl, g_fColHdrSm
#include "assets.h"        // AssetImage, DrawButton9Slice
#include "buttons.h"       // MkStdBtn, PaintOwnerDrawButton, ButtonKind
#include "launcher_self_update.h"  // LAUNCHER_VERSION
#include "d2rloader_update.h"      // StartD2RLoaderDownloadInstall

#include <winver.h>        // GetFileVersionInfo, VerQueryValue
#include <vector>

using namespace Gdiplus;

// ─────────────────────────────────────────────────────────────────────
//  Constants
// ─────────────────────────────────────────────────────────────────────

constexpr int AB_W              = 460;
constexpr int AB_H              = 420;
constexpr int AB_TITLE_TOP_PAD  = 12;
constexpr int AB_TITLE_H        = 34;

// Version info block (below the title).
constexpr int AB_VER_TOP        = 62;
constexpr int AB_VER_LINE_H     = 26;

// Big Download button.
constexpr int AB_DL_TOP         = 150;
constexpr int AB_DL_W           = 300;
constexpr int AB_DL_H           = 62;

// Square icon row (README / FAQ / Discord). Native art is 85×85; we
// render a bit smaller so three fit comfortably with gaps.
constexpr int AB_ICON_TOP       = 238;
constexpr int AB_ICON_W         = 72;
constexpr int AB_ICON_H         = 72;
constexpr int AB_ICON_GAP       = 28;

// Close button (bottom-anchored).
constexpr int AB_BTN_W          = 140;
constexpr int AB_BTN_H          = 50;
constexpr int AB_BTN_BOTTOM_PAD = 16;

// Reader-mode text view insets.
constexpr int AB_READER_PAD     = 18;
constexpr int AB_READER_TOP     = 56;   // below the title band

// URLs.
static const wchar_t* kUrlDiscord  = L"https://discord.gg/eEHT2kcBMf";

// Control IDs. Start at 100 to stay clear of IDOK (1) and IDCANCEL (2):
// IsDialogMessage synthesizes a WM_COMMAND with IDCANCEL when Esc is
// pressed, so any control sharing id 2 would be "clicked" by Esc. That
// bug previously fired the Download button (which had been id 2) on Esc.
enum {
    ID_CLOSE     = 100,
    ID_DOWNLOAD  = 101,
    ID_README    = 102,
    ID_FAQ       = 103,
    ID_DISCORD   = 104,
};

// ─────────────────────────────────────────────────────────────────────
//  State
// ─────────────────────────────────────────────────────────────────────

enum class AboutView { Hub, Reader };

static HWND      g_abHwnd     = nullptr;
static bool      g_abClassReg = false;
static AboutView g_abView     = AboutView::Hub;

// Hub buttons.
static HWND g_abClose    = nullptr;
static HWND g_abDownload = nullptr;
static HWND g_abReadme   = nullptr;
static HWND g_abFaq      = nullptr;
static HWND g_abDiscord  = nullptr;
// Reader text view.
static HWND g_abEdit     = nullptr;

// Original EDIT window proc, saved when we subclass g_abEdit so the
// mouse wheel scrolls the text even when the EDIT (not the parent
// modal) is under the cursor. Restored implicitly on destroy.
static WNDPROC g_abEditPrevProc = nullptr;
static HFONT   g_abReaderFont   = nullptr;   // Georgia HFONT for the reader

// Cached D2RLoader version string, resolved once per open.
static wstring g_abLoaderVer;

// Install status line, shown under the version block while a download +
// install runs and after it finishes. Empty = nothing to show.
static wstring g_abStatus;

// ─────────────────────────────────────────────────────────────────────
//  D2RLoader version probe
// ─────────────────────────────────────────────────────────────────────

// Read the file-version resource ("x.y.z.w") stamped into D2RLoader.exe.
// Not every D2RLoader build carries a version resource; on any failure
// this returns empty and the caller shows "Not detected".
static wstring ProbeD2RLoaderVersion() {
    if (g_cfg.d2rPath.empty()) return L"";
    wstring exe = g_cfg.d2rPath + L"\\D2RLoader.exe";

    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &dummy);
    if (size == 0) return L"";

    std::vector<BYTE> buf(size);
    if (!GetFileVersionInfoW(exe.c_str(), 0, size, buf.data())) return L"";

    VS_FIXEDFILEINFO* ffi = nullptr;
    UINT ffiLen = 0;
    if (!VerQueryValueW(buf.data(), L"\\", (LPVOID*)&ffi, &ffiLen) || !ffi) {
        return L"";
    }

    WORD major = HIWORD(ffi->dwFileVersionMS);
    WORD minor = LOWORD(ffi->dwFileVersionMS);
    WORD build = HIWORD(ffi->dwFileVersionLS);
    WORD rev   = LOWORD(ffi->dwFileVersionLS);

    wchar_t out[48];
    // Trim a trailing ".0" revision for cleaner display, but keep it if
    // it carries information.
    if (rev == 0) {
        swprintf(out, 48, L"%u.%u.%u", major, minor, build);
    } else {
        swprintf(out, 48, L"%u.%u.%u.%u", major, minor, build, rev);
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────
//  Button show/hide per view
// ─────────────────────────────────────────────────────────────────────

static void ApplyViewVisibility() {
    bool hub = (g_abView == AboutView::Hub);
    auto show = [](HWND h, bool v) {
        if (h) ShowWindow(h, v ? SW_SHOW : SW_HIDE);
    };
    show(g_abDownload, hub);
    show(g_abReadme,   hub);
    show(g_abFaq,      hub);
    show(g_abDiscord,  hub);
    show(g_abEdit,   !hub);
    // Close is visible in both views.
}

// Load a text file into the reader EDIT and switch to reader view.
static void EnterReader(const wchar_t* fileName) {
    wstring path = AppDir() + L"\\" + fileName;
    wstring text = ReadTextFile(path);
    if (text.empty()) {
        text = wstring(L"Could not open ") + fileName + L".\r\n\r\n"
             + L"The file may be missing from the launcher folder.";
    }
    // EDIT wants CRLF; ReadTextFile may hand back bare LF. Normalize.
    wstring norm;
    norm.reserve(text.size() + 64);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\n' && (i == 0 || text[i-1] != L'\r')) {
            norm += L"\r\n";
        } else {
            norm += text[i];
        }
    }
    if (g_abEdit) {
        SetWindowTextW(g_abEdit, norm.c_str());
        SendMessageW(g_abEdit, EM_SETSEL, 0, 0);
        SendMessageW(g_abEdit, EM_SCROLLCARET, 0, 0);
    }
    g_abView = AboutView::Reader;
    ApplyViewVisibility();
    InvalidateRect(g_abHwnd, nullptr, FALSE);
}

static void ExitReader() {
    g_abView = AboutView::Hub;
    ApplyViewVisibility();
    InvalidateRect(g_abHwnd, nullptr, FALSE);
}

// Subclass proc for the reader EDIT. A child EDIT under the cursor
// receives WM_MOUSEWHEEL itself (not the parent), so forwarding from the
// modal's WndProc never fires while the pointer is over the text. Handling
// it here scrolls the EDIT wherever the cursor is. ~3 lines per notch.
static LRESULT CALLBACK AboutEditProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_MOUSEWHEEL) {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        int lines = -(delta / WHEEL_DELTA) * 3;
        SendMessageW(h, EM_LINESCROLL, 0, (LPARAM)lines);
        return 0;
    }
    return CallWindowProcW(g_abEditPrevProc, h, m, wp, lp);
}

// ─────────────────────────────────────────────────────────────────────
//  Paint
// ─────────────────────────────────────────────────────────────────────

static void PaintHub(Graphics& g, int W, int H) {
    SolidBrush gold(Tok::Gold);
    SolidBrush parch(Tok::TextParchment);
    SolidBrush dim(Tok::BronzeDim);

    StringFormat sfC;
    sfC.SetAlignment(StringAlignmentCenter);
    sfC.SetLineAlignment(StringAlignmentCenter);
    sfC.SetFormatFlags(sfC.GetFormatFlags() | StringFormatFlagsNoWrap);

    // ── Version lines ──────────────────────────────────────────────
    Gdiplus::Font* verFont   = g_fColHdrSm ? g_fColHdrSm : g_fBtn;
    Gdiplus::Font* labelFont = g_fSubLbl   ? g_fSubLbl   : g_fBtn;

    // Angiris launcher version.
    {
        wstring line = wstring(L"Angiris  v") + LAUNCHER_VERSION;
        RectF r(0, (REAL)S(AB_VER_TOP), (REAL)W, (REAL)S(AB_VER_LINE_H));
        if (verFont) g.DrawString(line.c_str(), -1, verFont, r, &sfC, &gold);
    }
    // D2RLoader version (or "Not detected"). The exe's version resource
    // carries only the numeric x.y.z; D2RLoader's public builds are
    // beta, so we append " - beta" when a version was actually read.
    {
        wstring line;
        SolidBrush* br = &parch;
        if (g_abLoaderVer.empty()) {
            line = L"D2RLoader  Not detected";
            br = &dim;
        } else {
            line = L"D2RLoader  v" + g_abLoaderVer + L" - beta";
        }
        RectF r(0, (REAL)S(AB_VER_TOP + AB_VER_LINE_H),
                (REAL)W, (REAL)S(AB_VER_LINE_H));
        if (labelFont) g.DrawString(line.c_str(), -1, labelFont, r, &sfC, br);
    }

    // Install status line (download progress / result), between the
    // version block and the Download button. Gold so it reads as active.
    if (!g_abStatus.empty() && labelFont) {
        RectF r(0, (REAL)S(AB_VER_TOP + AB_VER_LINE_H * 2 + 4),
                (REAL)W, (REAL)S(AB_VER_LINE_H));
        g.DrawString(g_abStatus.c_str(), -1, labelFont, r, &sfC, &gold);
    }
}

static void PaintReader(Graphics& g, int W, int H) {
    // The EDIT child paints the body; here we just draw a small caption
    // above it so the user knows which document they're reading. The
    // caption text is derived from the current window title suffix set
    // in EnterReader via SetWindowText on the title band — but to keep
    // it simple we render nothing extra; the title band already shows
    // "About — README" / "About — FAQ".
    (void)g; (void)W; (void)H;
}

static LRESULT CALLBACK AboutProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

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

            // Stone bg.
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

            // Frame chrome.
            if (Gdiplus::Bitmap* frame = AssetImage(L"frame_modbanner.png")) {
                DrawButton9Slice(g, frame, 0, 0, W, H, 24);
            } else {
                Pen fallback(Tok::Bronze, 1.0f);
                g.DrawRectangle(&fallback, 1, 1, W - 3, H - 3);
            }

            // Title band.
            SolidBrush titleBr(Tok::Gold);
            StringFormat sfT;
            sfT.SetAlignment(StringAlignmentCenter);
            sfT.SetLineAlignment(StringAlignmentCenter);
            Gdiplus::Font* titleFont = g_fModName ? g_fModName : g_fBtn;
            const wchar_t* title =
                (g_abView == AboutView::Reader) ? L"About — Document"
                                                : L"About";
            if (titleFont) {
                g.DrawString(title, -1, titleFont,
                    RectF((REAL)0, (REAL)S(AB_TITLE_TOP_PAD),
                          (REAL)W, (REAL)S(AB_TITLE_H)),
                    &sfT, &titleBr);
            }

            if (g_abView == AboutView::Hub) PaintHub(g, W, H);
            else                            PaintReader(g, W, H);
        }

        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case MSG_D2RLOADER_INSTALL_PROGRESS: {
        switch ((int)wp) {
            case D2RL_STAGE_DOWNLOADING: g_abStatus = L"Downloading D2RLoader\u2026"; break;
            case D2RL_STAGE_VERIFYING:   g_abStatus = L"Verifying download\u2026";    break;
            case D2RL_STAGE_EXTRACTING:  g_abStatus = L"Extracting\u2026";            break;
            case D2RL_STAGE_INSTALLING:  g_abStatus = L"Installing\u2026";            break;
            case D2RL_STAGE_MERGING:     g_abStatus = L"Updating config\u2026";       break;
        }
        InvalidateRect(hw, nullptr, FALSE);
        return 0;
    }

    case MSG_D2RLOADER_INSTALL_DONE: {
        switch ((int)wp) {
            case D2RL_OK:
                g_abStatus = L"D2RLoader updated successfully.";
                // Re-probe so the version line refreshes.
                g_abLoaderVer = ProbeD2RLoaderVersion();
                break;
            case D2RL_ERR_NO_PATH:
                g_abStatus = L"D2R folder not set \u2014 pick it first.";
                break;
            case D2RL_ERR_DOWNLOAD:
                g_abStatus = L"Download failed. Check your connection.";
                break;
            case D2RL_ERR_EXTRACT:
                g_abStatus = L"Could not extract the download.";
                break;
            case D2RL_ERR_CHECKSUM:
                g_abStatus = L"Download failed verification \u2014 "
                             L"nothing was installed.";
                break;
            case D2RL_ERR_COPY:
                g_abStatus = L"Some files couldn't be written "
                             L"(is D2R/D2RLoader running?).";
                break;
            case D2RL_ERR_BUSY:
                g_abStatus = L"An update is already in progress.";
                break;
            default:
                g_abStatus = L"Update finished.";
                break;
        }
        if (g_abDownload) EnableWindow(g_abDownload, TRUE);
        InvalidateRect(hw, nullptr, FALSE);
        return 0;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        // Theme the reader EDIT — parchment text on a near-black well.
        HDC dc = (HDC)wp;
        SetTextColor(dc, RGB(0xD8, 0xC7, 0xA0));
        SetBkColor(dc, RGB(12, 10, 8));
        static HBRUSH s_br = CreateSolidBrush(RGB(12, 10, 8));
        return (LRESULT)s_br;
    }

    case WM_COMMAND: {
        WORD id   = LOWORD(wp);
        WORD code = HIWORD(wp);
        // IsDialogMessage turns Esc into a WM_COMMAND/IDCANCEL. Treat it
        // exactly like Esc in WM_KEYDOWN: back out of the reader, else
        // close. (Belt-and-suspenders alongside the WM_KEYDOWN handler.)
        if (id == IDCANCEL) {
            if (g_abView == AboutView::Reader) ExitReader();
            else                               DestroyWindow(hw);
            return 0;
        }
        if (code == BN_CLICKED) {
            switch (id) {
                case ID_CLOSE:
                    // From a document, Close returns to the About hub;
                    // from the hub, it closes the modal. Mirrors Esc.
                    if (g_abView == AboutView::Reader) ExitReader();
                    else                               DestroyWindow(hw);
                    return 0;
                case ID_DOWNLOAD: {
                    // Block the install while D2R / D2RLoader is running —
                    // their open file handles would make the copy fail
                    // partway. Offer to close them for the user ("Close
                    // Now") rather than making them alt-tab out manually.
                    if (IsD2RLoaderRunning()) {
                        int choice = MessageBoxW(hw,
                            L"D2R or D2RLoader is currently running and "
                            L"must be closed before updating.\n\n"
                            L"Close it now?\n\n"
                            L"(Any unsaved game progress will be lost.)",
                            L"Close D2RLoader to continue",
                            MB_YESNO | MB_ICONWARNING);
                        if (choice != IDYES) {
                            g_abStatus = L"Update cancelled.";
                            InvalidateRect(hw, nullptr, FALSE);
                            return 0;
                        }
                        // User agreed — terminate, then give the OS a
                        // moment to release file handles before we verify.
                        TerminateD2RLoaderProcesses();
                        for (int i = 0; i < 20 && IsD2RLoaderRunning(); ++i) {
                            Sleep(100);   // up to ~2s
                        }
                        if (IsD2RLoaderRunning()) {
                            MessageBoxW(hw,
                                L"Couldn't fully close D2R/D2RLoader.\n\n"
                                L"Try closing it manually, then click "
                                L"Latest D2RLoader again.",
                                L"Still running",
                                MB_OK | MB_ICONWARNING);
                            g_abStatus = L"Close D2R/D2RLoader, then retry.";
                            InvalidateRect(hw, nullptr, FALSE);
                            return 0;
                        }
                    }
                    // Field is clear — kick off the download + install on
                    // a background thread. Progress/done arrive via the
                    // MSG_D2RLOADER_* messages handled below. Disable the
                    // button while it runs so we don't spawn a second one.
                    if (!IsD2RLoaderInstallRunning()) {
                        g_abStatus = L"Starting download\u2026";
                        if (g_abDownload) EnableWindow(g_abDownload, FALSE);
                        InvalidateRect(hw, nullptr, FALSE);
                        StartD2RLoaderDownloadInstall(hw);
                    }
                    return 0;
                }
                case ID_DISCORD:
                    ShellExecuteW(hw, L"open", kUrlDiscord,
                                  nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                case ID_README:  EnterReader(L"README.txt"); return 0;
                case ID_FAQ:     EnterReader(L"FAQ.txt");     return 0;
            }
        }
        break;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;
        if (PaintOwnerDrawButton(d)) return TRUE;
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            // Esc backs out of the reader first, then closes the modal.
            if (g_abView == AboutView::Reader) { ExitReader(); return 0; }
            DestroyWindow(hw);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hw);
        return 0;

    case WM_DESTROY: {
        HWND parent = GetWindow(hw, GW_OWNER);
        if (parent) {
            EnableWindow(parent, TRUE);
            SetForegroundWindow(parent);
        }
        g_abHwnd     = nullptr;
        g_abClose    = nullptr;
        g_abDownload = nullptr;
        g_abReadme   = nullptr;
        g_abFaq      = nullptr;
        g_abDiscord  = nullptr;
        g_abEdit     = nullptr;
        g_abEditPrevProc = nullptr;
        if (g_abReaderFont) { DeleteObject(g_abReaderFont); g_abReaderFont = nullptr; }
        g_abView     = AboutView::Hub;
        return 0;
    }
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

// ─────────────────────────────────────────────────────────────────────
//  Spawner
// ─────────────────────────────────────────────────────────────────────

void ShowAboutModal(HWND parent) {
    if (g_abHwnd) return;

    if (!g_abClassReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = AboutProc;
        wc.hInstance     = g_hInst;
        wc.lpszClassName = L"AngirisAboutModal";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        g_abClassReg = true;
    }

    g_abView       = AboutView::Hub;
    g_abLoaderVer  = ProbeD2RLoaderVersion();
    g_abStatus.clear();

    RECT pr; GetWindowRect(parent, &pr);
    int physW = (int)(AB_W * g_scale);
    int physH = (int)(AB_H * g_scale);
    int x = pr.left + ((pr.right  - pr.left) - physW) / 2;
    int y = pr.top  + ((pr.bottom - pr.top ) - physH) / 2;

    g_abHwnd = CreateWindowExW(
        WS_EX_TOPMOST,     // no DLGMODALFRAME — frame_modbanner is the border
        L"AngirisAboutModal",
        L"About",
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, physW, physH,
        parent, nullptr, g_hInst, nullptr);
    if (!g_abHwnd) return;

    auto SP = [&](int lx, int ly, int lw, int lh) -> RECT {
        return RECT{ (int)(lx * g_scale), (int)(ly * g_scale),
                     (int)((lx + lw) * g_scale), (int)((ly + lh) * g_scale) };
    };

    // ── Hub buttons ────────────────────────────────────────────────
    // Big Download button, centered near the top of the action area.
    {
        int lx = (AB_W - AB_DL_W) / 2;
        RECT r = SP(lx, AB_DL_TOP, AB_DL_W, AB_DL_H);
        g_abDownload = MkStdBtn(g_abHwnd, L"Latest D2RLoader", ID_DOWNLOAD,
                                r.left, r.top, r.right - r.left, r.bottom - r.top,
                                true, ButtonKind::Plugins);
    }
    // Square icon row: README / FAQ / Discord — reuses the mod-
    // description link button kinds (btn_docs / btn_faq / btn_discord).
    // The HWND text is a single-char ID used only by the missing-asset
    // fallback path; the icon art carries the real meaning.
    {
        int totalW = AB_ICON_W * 3 + AB_ICON_GAP * 2;
        int startX = (AB_W - totalW) / 2;
        int x1 = startX;
        int x2 = x1 + AB_ICON_W + AB_ICON_GAP;
        int x3 = x2 + AB_ICON_W + AB_ICON_GAP;
        RECT r1 = SP(x1, AB_ICON_TOP, AB_ICON_W, AB_ICON_H);
        RECT r2 = SP(x2, AB_ICON_TOP, AB_ICON_W, AB_ICON_H);
        RECT r3 = SP(x3, AB_ICON_TOP, AB_ICON_W, AB_ICON_H);
        g_abReadme  = MkStdBtn(g_abHwnd, L"D", ID_README,
                               r1.left, r1.top, r1.right - r1.left, r1.bottom - r1.top,
                               true, ButtonKind::ModLinkDocs);
        g_abFaq     = MkStdBtn(g_abHwnd, L"?", ID_FAQ,
                               r2.left, r2.top, r2.right - r2.left, r2.bottom - r2.top,
                               true, ButtonKind::ModLinkFaq);
        g_abDiscord = MkStdBtn(g_abHwnd, L"X", ID_DISCORD,
                               r3.left, r3.top, r3.right - r3.left, r3.bottom - r3.top,
                               true, ButtonKind::ModLinkDiscord);
    }

    // ── Close button (bottom-anchored, both views) ─────────────────
    {
        int physBtnW = (int)(AB_BTN_W * g_scale);
        int physBtnH = (int)(AB_BTN_H * g_scale);
        int btnX = (physW - physBtnW) / 2;
        int btnY = physH - (int)(AB_BTN_BOTTOM_PAD * g_scale) - physBtnH;
        g_abClose = MkStdBtn(g_abHwnd, L"Close", ID_CLOSE,
                             btnX, btnY, physBtnW, physBtnH,
                             true, ButtonKind::Plugins);
    }

    // ── Reader-mode text view (created hidden) ─────────────────────
    // Fills the area between the title band and the Close button. There's
    // no Back button — Close returns to the hub from the reader (and Esc
    // does too), so a separate control would just overlap the text.
    // Scrolls via the mouse wheel; the native scrollbar is suppressed so
    // it doesn't clash with the themed chrome.
    {
        int edX = (int)(AB_READER_PAD * g_scale);
        int edY = (int)(AB_READER_TOP * g_scale);
        int edW = physW - 2 * edX;
        int closeTop = physH - (int)((AB_BTN_BOTTOM_PAD + AB_BTN_H) * g_scale);
        int edH = closeTop - edY - (int)(AB_READER_PAD * g_scale);
        // No WS_VSCROLL — the wheel subclass below handles scrolling and
        // ES_AUTOVSCROLL keeps the content scrollable without the
        // un-themed native bar.
        g_abEdit = CreateWindowExW(0,
            L"EDIT", L"",
            WS_CHILD | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            edX, edY, edW, edH,
            g_abHwnd, nullptr, g_hInst, nullptr);
        if (g_abEdit) {
            // Themed reading font (Georgia) rather than the stock GUI font.
            // The user's display font is intentionally not used — see
            // MakeReaderFont. Freed on WM_DESTROY.
            g_abReaderFont = MakeReaderFont(11);
            SendMessageW(g_abEdit, WM_SETFONT, (WPARAM)g_abReaderFont, TRUE);
            // Subclass so the wheel scrolls the EDIT when it's under the
            // cursor (the child, not the parent, gets those messages).
            g_abEditPrevProc = (WNDPROC)SetWindowLongPtrW(
                g_abEdit, GWLP_WNDPROC, (LONG_PTR)AboutEditProc);
        }
    }

    ApplyViewVisibility();

    EnableWindow(parent, FALSE);
    ShowWindow(g_abHwnd, SW_SHOW);
    UpdateWindow(g_abHwnd);
    SetActiveWindow(g_abHwnd);

    MSG msg;
    while (g_abHwnd) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == 0 || got == -1) {
            if (got == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (g_abHwnd && IsDialogMessageW(g_abHwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}
