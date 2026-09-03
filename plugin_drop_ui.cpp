// ═══════════════════════════════════════════════════════════════════════
//  plugin_drop_ui.cpp — UI glue for drag-drop plugin install (v1.6, Step 3)
// ═══════════════════════════════════════════════════════════════════════
//
//  Bridges the UI-agnostic routing core (plugin_install.cpp) to the
//  launcher's windows: supplies the prompt callbacks (no-manifest notice,
//  mod picker, encrypted-mpq notice, overwrite choice, error) and the
//  entry points the WM_DROPFILES handlers call.
//
//  The overwrite prompt and mod picker are themed to match the launcher's
//  other modals: stone background, gold title, and MkStdBtn buttons.

#include "plugin_install.h"
#include "core.h"          // g_hInst, g_scale
#include "config.h"        // g_cfg
#include "mod_scan.h"      // g_mods, ModInfo
#include "plugin_config.h" // LoadPluginConfig, PluginConfig (allowlist)
#include "plugin_drop_ui.h"
#include "colors.h"        // Tok::Gold
#include "fonts.h"         // g_fModName, g_fNavSm
#include "assets.h"        // AssetImage, DrawButton9Slice
#include "buttons.h"       // MkStdBtn, PaintOwnerDrawButton, ButtonKind
#include "scaling.h"       // S()

#include <windows.h>

using namespace Gdiplus;
using std::wstring;
using std::vector;

// Shared themed-modal paint: tiles bg_stone across the whole client, draws
// the frame, and renders a centered gold title. Used by both prompts so
// they match the launcher's other modals.
static void PaintThemedPromptBg(HDC memDC, int W, int H, const wstring& title) {
    Graphics g(memDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    if (Gdiplus::Bitmap* stone = AssetImage(L"bg_stone.png")) {
        int sw = (int)stone->GetWidth(), sh = (int)stone->GetHeight();
        for (int yy = 0; yy < H; yy += sh)
            for (int xx = 0; xx < W; xx += sw)
                g.DrawImage(stone, xx, yy, sw, sh);
    } else { SolidBrush b(Color(28, 24, 20)); g.FillRectangle(&b, 0, 0, W, H); }
    if (Gdiplus::Bitmap* frame = AssetImage(L"frame_modbanner.png"))
        DrawButton9Slice(g, frame, 0, 0, W, H, 24);
    if (!title.empty()) {
        SolidBrush gold(Tok::Gold);
        StringFormat sfC; sfC.SetAlignment(StringAlignmentCenter);
        sfC.SetLineAlignment(StringAlignmentCenter);
        Gdiplus::Font* tf = g_fModName ? g_fModName : g_fNavSm;
        if (tf) g.DrawString(title.c_str(), -1, tf,
            RectF((REAL)0, (REAL)S(14), (REAL)W, (REAL)S(34)), &sfC, &gold);
    }
}

// Draw wrapped body text (pale gold) in a themed prompt, below the title.
static void PaintThemedPromptBody(HDC memDC, int W, int topY, int h,
                                  const wstring& body) {
    Graphics g(memDC);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    SolidBrush txt(Color(0xD8, 0xC7, 0xA0));
    StringFormat sf; sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentNear);
    Gdiplus::Font* bf = g_fNavSm;
    if (bf) g.DrawString(body.c_str(), -1, bf,
        RectF((REAL)S(24), (REAL)topY, (REAL)(W - 2 * S(24)), (REAL)h), &sf, &txt);
}

// ─────────────────────────────────────────────────────────────────────
//  Overwrite dialog — DLL Only / Config Only / DLL and Config / Cancel
//  (or Yes / No for a DLL-only plugin). Returns an OverwriteChoice.
// ─────────────────────────────────────────────────────────────────────

namespace {

struct OverwriteResult { OverwriteChoice choice = OverwriteChoice::Cancel; bool done = false; };

// Control IDs — kept off IDOK(1)/IDCANCEL(2) to avoid the Esc/Enter
// synthetic-click trap (see the v1.5 modal fixes).
enum {
    OW_DLL_ONLY   = 100,
    OW_CONFIG     = 101,
    OW_BOTH       = 102,
    OW_CANCEL     = 103,
    OW_YES        = 104,   // DLL-only plugin: Yes = overwrite
};

LRESULT CALLBACK OverwriteProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        int W = rc.right, H = rc.bottom;
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);
        PaintThemedPromptBg(memDC, W, H, L"Plugin already exists");
        auto* body = (wstring*)GetPropW(hw, L"owBody");
        if (body) PaintThemedPromptBody(memDC, W, S(56), S(84), *body);
        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM); DeleteObject(memBM); DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }
    if (msg == WM_DRAWITEM) {
        if (PaintOwnerDrawButton((DRAWITEMSTRUCT*)lp)) return TRUE;
    }
    if (msg == WM_COMMAND) {
        auto* r = (OverwriteResult*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        WORD id = LOWORD(wp);
        if (r) {
            switch (id) {
                case OW_DLL_ONLY: r->choice = OverwriteChoice::DllOnly;      r->done = true; break;
                case OW_CONFIG:   r->choice = OverwriteChoice::ConfigOnly;   r->done = true; break;
                case OW_BOTH:     r->choice = OverwriteChoice::DllAndConfig; r->done = true; break;
                case OW_YES:      r->choice = OverwriteChoice::DllAndConfig; r->done = true; break;
                case OW_CANCEL:
                case IDCANCEL:    r->choice = OverwriteChoice::Cancel;       r->done = true; break;
                default: return 0;
            }
            DestroyWindow(hw);
        }
        return 0;
    }
    if (msg == WM_CLOSE) {
        auto* r = (OverwriteResult*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        if (r) { r->choice = OverwriteChoice::Cancel; r->done = true; }
        DestroyWindow(hw);
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

OverwriteChoice ShowOverwriteDialog(HWND parent, bool hasConfig,
                                    const wstring& pluginName) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = OverwriteProc;
        wc.hInstance     = g_hInst;
        wc.lpszClassName = L"AngirisOverwriteDlg";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        reg = true;
    }

    int w = (int)(560 * g_scale);
    int h = (int)((hasConfig ? 300 : 260) * g_scale);
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - w) / 2;
    int y = pr.top  + ((pr.bottom - pr.top) - h) / 2;

    OverwriteResult res;
    HWND dlg = CreateWindowExW(0,   // owned popup stays above owner only
        L"AngirisOverwriteDlg", L"Plugin already exists",
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, w, h, parent, nullptr, g_hInst, nullptr);
    if (!dlg) return OverwriteChoice::Cancel;
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&res);

    wstring body = pluginName + L"\nis already installed. What would you like to overwrite?";
    SetPropW(dlg, L"owBody", (HANDLE)&body);

    int bw = (int)(150 * g_scale), bh = (int)(56 * g_scale);
    int gap = (int)(14 * g_scale);
    if (hasConfig) {
        // Three across, centered, then Cancel below.
        int rowW = bw * 3 + gap * 2;
        int rowX = (w - rowW) / 2;
        int by = (int)(150 * g_scale);
        MkStdBtn(dlg, L"DLL Only", OW_DLL_ONLY, rowX, by, bw, bh, true, ButtonKind::Plugins);
        MkStdBtn(dlg, L"Config Only", OW_CONFIG, rowX + bw + gap, by, bw, bh, true, ButtonKind::Plugins);
        MkStdBtn(dlg, L"DLL + Config", OW_BOTH, rowX + 2*(bw+gap), by, bw, bh, true, ButtonKind::Plugins);
        MkStdBtn(dlg, L"Cancel", OW_CANCEL, (w - bw)/2, by + bh + gap, bw, bh, true, ButtonKind::Plugins);
    } else {
        // Yes / No, two across, centered.
        int rowW = bw * 2 + gap;
        int rowX = (w - rowW) / 2;
        int by = (int)(158 * g_scale);
        MkStdBtn(dlg, L"Yes", OW_YES, rowX, by, bw, bh, true, ButtonKind::Plugins);
        MkStdBtn(dlg, L"No", OW_CANCEL, rowX + bw + gap, by, bw, bh, true, ButtonKind::Plugins);
    }

    EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    MSG m;
    while (!res.done && GetMessageW(&m, nullptr, 0, 0)) {
        if (IsDialogMessageW(dlg, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    RemovePropW(dlg, L"owBody");
    EnableWindow(parent, TRUE);
    SetActiveWindow(parent);
    return res.choice;
}

// ─────────────────────────────────────────────────────────────────────
//  Excel mod picker — a dropdown of every mod in the mod list.
// ─────────────────────────────────────────────────────────────────────

struct ExcelPickResult { wstring mod; bool done = false; bool cancel = false; };

enum { EX_COMBO = 200, EX_OK = 201, EX_CANCEL = 202 };

LRESULT CALLBACK ExcelPickProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        int W = rc.right, H = rc.bottom;
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);
        PaintThemedPromptBg(memDC, W, H, L"Which mod?");
        PaintThemedPromptBody(memDC, W, S(54), S(90),
            L"This global plugin contains mod-local file(s). "
            L"Which mod should they be installed into?");
        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM); DeleteObject(memBM); DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }
    if (msg == WM_DRAWITEM) {
        if (PaintOwnerDrawButton((DRAWITEMSTRUCT*)lp)) return TRUE;
    }
    if (msg == WM_COMMAND) {
        auto* r = (ExcelPickResult*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        WORD id = LOWORD(wp);
        if (r && (id == EX_OK)) {
            HWND combo = GetDlgItem(hw, EX_COMBO);
            int sel = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
            if (sel >= 0) {
                wchar_t buf[512];
                SendMessageW(combo, CB_GETLBTEXT, sel, (LPARAM)buf);
                r->mod = buf;
            }
            r->done = true;
            DestroyWindow(hw);
        } else if (r && (id == EX_CANCEL || id == IDCANCEL)) {
            r->cancel = true; r->done = true;
            DestroyWindow(hw);
        }
        return 0;
    }
    if (msg == WM_CLOSE) {
        auto* r = (ExcelPickResult*)GetWindowLongPtrW(hw, GWLP_USERDATA);
        if (r) { r->cancel = true; r->done = true; }
        DestroyWindow(hw);
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

wstring ShowExcelModPicker(HWND parent, const vector<wstring>& mods) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = ExcelPickProc;
        wc.hInstance     = g_hInst;
        wc.lpszClassName = L"AngirisExcelPickDlg";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        reg = true;
    }

    int w = (int)(560 * g_scale), h = (int)(300 * g_scale);
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - w) / 2;
    int y = pr.top  + ((pr.bottom - pr.top) - h) / 2;

    ExcelPickResult res;
    HWND dlg = CreateWindowExW(0,   // owned popup stays above owner only
        L"AngirisExcelPickDlg", L"Which mod?",
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, w, h, parent, nullptr, g_hInst, nullptr);
    if (!dlg) return L"";
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&res);

    int pad = (int)(28 * g_scale);
    HWND combo = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        pad, (int)(150 * g_scale), w - 2 * pad, (int)(240 * g_scale),
        dlg, (HMENU)EX_COMBO, g_hInst, nullptr);
    if (g_fNavSm) {
        // Give the combobox the launcher's UI font for consistency.
        static HFONT s_comboFont = CreateFontW(-(int)(16 * g_scale), 0, 0, 0,
            FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        SendMessageW(combo, WM_SETFONT, (WPARAM)s_comboFont, TRUE);
    }
    for (const wstring& m : mods)
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)m.c_str());
    if (!mods.empty()) SendMessageW(combo, CB_SETCURSEL, 0, 0);

    int bw = (int)(150 * g_scale), bh = (int)(56 * g_scale);
    int gap = (int)(14 * g_scale);
    int rowW = bw * 2 + gap;
    int rowX = (w - rowW) / 2;
    int by = h - (int)(20 * g_scale) - bh;
    MkStdBtn(dlg, L"OK", EX_OK, rowX, by, bw, bh, true, ButtonKind::Plugins);
    MkStdBtn(dlg, L"Cancel", EX_CANCEL, rowX + bw + gap, by, bw, bh, true, ButtonKind::Plugins);

    EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    MSG m;
    while (!res.done && GetMessageW(&m, nullptr, 0, 0)) {
        if (IsDialogMessageW(dlg, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    EnableWindow(parent, TRUE);
    SetActiveWindow(parent);
    return res.cancel ? wstring() : res.mod;
}

// ─────────────────────────────────────────────────────────────────────
//  Callback thunks (C-style, matching PluginDropCallbacks).
//  ctx carries the parent HWND.
// ─────────────────────────────────────────────────────────────────────

void CbNoManifest(void* ctx) {
    HWND parent = (HWND)ctx;
    MessageBoxW(parent,
        L"No plugin_info.json file detected. All plugin files will be "
        L"placed in the plugins folder.",
        L"Plugin Install", MB_OK | MB_ICONINFORMATION);
}

wstring CbPickExcelMod(void* ctx, const vector<wstring>& mods) {
    return ShowExcelModPicker((HWND)ctx, mods);
}

void CbEncryptedMpq(void* ctx, const wstring& modName) {
    HWND parent = (HWND)ctx;
    wstring msg = L"This mod includes an encrypted .MPQ. TXT file has been "
                  L"placed in /mods/" + modName + L"/.";
    MessageBoxW(parent, msg.c_str(), L"Plugin Install",
                MB_OK | MB_ICONINFORMATION);
}

OverwriteChoice CbAskOverwrite(void* ctx, bool hasConfig, const wstring& name) {
    return ShowOverwriteDialog((HWND)ctx, hasConfig, name);
}

void CbError(void* ctx, const wstring& message) {
    MessageBoxW((HWND)ctx, message.c_str(), L"Plugin Install",
                MB_OK | MB_ICONWARNING);
}

void CbNotAuthorized(void* ctx, const wstring& pluginName,
                     const wstring& modName, const wstring& modAuthor) {
    // Two wordings per the design: with an author name, or without.
    wstring plugin = pluginName.empty() ? L"This plugin" : pluginName;
    wstring msg;
    if (!modAuthor.empty()) {
        msg = modAuthor + L" has not authorized " + plugin +
              L" to be used with " + modName + L".";
    } else {
        msg = modName + L" does not support " + plugin + L".";
    }
    MessageBoxW((HWND)ctx, msg.c_str(), L"Plugin Not Authorized",
                MB_OK | MB_ICONWARNING);
}

PluginDropCallbacks MakeCallbacks(HWND parent) {
    PluginDropCallbacks cb;
    cb.noManifestNotice   = CbNoManifest;
    cb.pickExcelMod       = CbPickExcelMod;
    cb.encryptedMpqNotice = CbEncryptedMpq;
    cb.askOverwrite       = CbAskOverwrite;
    cb.errorNotice        = CbError;
    cb.notAuthorized      = CbNotAuthorized;
    cb.ctx                = (void*)parent;
    return cb;
}

// Build the display mod list for the excel picker from g_mods.
vector<wstring> ModListForPicker() {
    vector<wstring> out;
    out.reserve(g_mods.size());
    for (const ModInfo& m : g_mods)
        out.push_back(m.folder.empty() ? m.name : m.folder);
    return out;
}

} // namespace


// ─────────────────────────────────────────────────────────────────────
//  Public entry points (called by the WM_DROPFILES handlers)
// ─────────────────────────────────────────────────────────────────────

// Main-window plugin drop → GLOBAL scope.
void HandleMainWindowPluginDrop(HWND parent, const wstring& zipPath) {
    if (g_cfg.d2rPath.empty()) {
        MessageBoxW(parent,
            L"Set your Diablo II: Resurrected path first, then try again.",
            L"Plugin Install", MB_OK | MB_ICONWARNING);
        return;
    }
    PluginDropCallbacks cb = MakeCallbacks(parent);
    PluginAllowlist allow;   // inactive — global drops have no mod allowlist
    HandlePluginDropZip(zipPath, g_cfg.d2rPath,
                        InstallScope::Global, L"",
                        ModListForPicker(), cb, allow);
}

// Plugin-manager plugin drop → MOD scope (the manager's selected mod).
void HandlePluginManagerDrop(HWND parent, const wstring& zipPath,
                             const wstring& selectedModFolder) {
    if (g_cfg.d2rPath.empty() || selectedModFolder.empty()) {
        MessageBoxW(parent,
            L"Select a mod in the plugin manager first.",
            L"Plugin Install", MB_OK | MB_ICONWARNING);
        return;
    }

    // Build the allowlist from the mod's plugin_config.json. If the mod has
    // a manifest (present), it's RESTRICTED: only sanctioned plugins install,
    // and a no-manifest zip is rejected. No manifest → inactive allowlist,
    // legacy "anything installs" behavior.
    PluginAllowlist allow;
    wstring modDir = g_cfg.d2rPath + L"\\mods\\" + selectedModFolder;
    PluginConfig mf = LoadPluginConfig(modDir);
    if (mf.present) {
        allow.active    = true;
        allow.entries   = mf.plugins;         // sanctioned DLL filenames
        allow.modName   = selectedModFolder;  // display; folder name here
        allow.modAuthor = mf.author;          // optional
    }

    PluginDropCallbacks cb = MakeCallbacks(parent);
    HandlePluginDropZip(zipPath, g_cfg.d2rPath,
                        InstallScope::Mod, selectedModFolder,
                        ModListForPicker(), cb, allow);
}

// Main-window bare .json patch drop → GLOBAL patches folder.
void HandleMainWindowPatchDrop(HWND parent, const wstring& jsonPath) {
    if (g_cfg.d2rPath.empty()) {
        MessageBoxW(parent,
            L"Set your Diablo II: Resurrected path first, then try again.",
            L"Patch Install", MB_OK | MB_ICONWARNING);
        return;
    }
    PluginDropCallbacks cb = MakeCallbacks(parent);
    PluginAllowlist allow;   // inactive — global drops have no mod allowlist
    HandleBarePatchDrop(jsonPath, g_cfg.d2rPath,
                        InstallScope::Global, L"", cb, allow);
}

// Plugin-manager bare .json patch drop → the selected mod's patches folder,
// gated by that mod's allowlist (same as plugin drops).
void HandlePluginManagerPatchDrop(HWND parent, const wstring& jsonPath,
                                  const wstring& selectedModFolder) {
    if (g_cfg.d2rPath.empty() || selectedModFolder.empty()) {
        MessageBoxW(parent,
            L"Select a mod in the plugin manager first.",
            L"Patch Install", MB_OK | MB_ICONWARNING);
        return;
    }
    PluginAllowlist allow;
    wstring modDir = g_cfg.d2rPath + L"\\mods\\" + selectedModFolder;
    PluginConfig mf = LoadPluginConfig(modDir);
    if (mf.present) {
        allow.active    = true;
        allow.entries   = mf.plugins;
        allow.modName   = selectedModFolder;
        allow.modAuthor = mf.author;
    }
    PluginDropCallbacks cb = MakeCallbacks(parent);
    HandleBarePatchDrop(jsonPath, g_cfg.d2rPath,
                        InstallScope::Mod, selectedModFolder, cb, allow);
}

// Main-window patch-bundle zip drop → GLOBAL patches folder.
void HandleMainWindowPatchBundle(HWND parent, const wstring& zipPath) {
    if (g_cfg.d2rPath.empty()) {
        MessageBoxW(parent,
            L"Set your Diablo II: Resurrected path first, then try again.",
            L"Patch Install", MB_OK | MB_ICONWARNING);
        return;
    }
    PluginDropCallbacks cb = MakeCallbacks(parent);
    PluginAllowlist allow;   // inactive — global
    HandlePatchBundleZip(zipPath, g_cfg.d2rPath,
                         InstallScope::Global, L"", cb, allow);
}

// Plugin-manager patch-bundle zip drop → the selected mod's patches folder,
// each JSON allowlist-gated.
void HandlePluginManagerPatchBundle(HWND parent, const wstring& zipPath,
                                    const wstring& selectedModFolder) {
    if (g_cfg.d2rPath.empty() || selectedModFolder.empty()) {
        MessageBoxW(parent,
            L"Select a mod in the plugin manager first.",
            L"Patch Install", MB_OK | MB_ICONWARNING);
        return;
    }
    PluginAllowlist allow;
    wstring modDir = g_cfg.d2rPath + L"\\mods\\" + selectedModFolder;
    PluginConfig mf = LoadPluginConfig(modDir);
    if (mf.present) {
        allow.active    = true;
        allow.entries   = mf.plugins;
        allow.modName   = selectedModFolder;
        allow.modAuthor = mf.author;
    }
    PluginDropCallbacks cb = MakeCallbacks(parent);
    HandlePatchBundleZip(zipPath, g_cfg.d2rPath,
                         InstallScope::Mod, selectedModFolder, cb, allow);
}

// ═══════════════════════════════════════════════════════════════════════
//  Repository install — install an already-downloaded file through the same
//  pipeline a drag-drop uses, with the browser's scope + mod choice.
// ═══════════════════════════════════════════════════════════════════════

namespace {

// Context for the mod-bypass picker: carries the parent window AND the mod
// the browser's dropdown pre-selected, so excel/{mod} files resolve to that
// mod WITHOUT showing the picker UI.
struct RepoPickCtx {
    HWND    parent;
    wstring mod;
};

// Bypass picker: returns the pre-chosen mod without any UI. Substituted for
// CbPickExcelMod on the repo install path so the dropdown's mod flows
// straight into the pipeline.
wstring CbRepoBypassPick(void* ctx, const vector<wstring>& /*mods*/) {
    auto* c = (RepoPickCtx*)ctx;
    return c ? c->mod : wstring();
}

// Wrappers that pull the parent HWND out of the RepoPickCtx for the other
// callbacks (which expect ctx == HWND).
void CbRepoNoManifest(void* ctx)                                { CbNoManifest((void*)((RepoPickCtx*)ctx)->parent); }
void CbRepoEncryptedMpq(void* ctx, const wstring& m)            { CbEncryptedMpq((void*)((RepoPickCtx*)ctx)->parent, m); }
OverwriteChoice CbRepoAskOverwrite(void* ctx, bool hc, const wstring& n) { return CbAskOverwrite((void*)((RepoPickCtx*)ctx)->parent, hc, n); }
void CbRepoError(void* ctx, const wstring& m)                  { CbError((void*)((RepoPickCtx*)ctx)->parent, m); }
void CbRepoNotAuthorized(void* ctx, const wstring& pluginName,
                         const wstring& modName, const wstring& modAuthor) {
    CbNotAuthorized((void*)((RepoPickCtx*)ctx)->parent, pluginName, modName, modAuthor);
}

// Build callbacks for the repo path: same prompts as a drop, but the mod
// picker is replaced by the dropdown-bypass.
PluginDropCallbacks MakeRepoCallbacks(RepoPickCtx* ctx) {
    PluginDropCallbacks cb;
    cb.noManifestNotice   = CbRepoNoManifest;
    cb.pickExcelMod       = CbRepoBypassPick;    // ← the bypass
    cb.encryptedMpqNotice = CbRepoEncryptedMpq;
    cb.askOverwrite       = CbRepoAskOverwrite;
    cb.errorNotice        = CbRepoError;
    cb.notAuthorized      = CbRepoNotAuthorized;
    cb.ctx                = (void*)ctx;
    return cb;
}

} // namespace

bool HandleRepoInstall(HWND parent, const wstring& filePath, bool isPatch,
                       bool modLocal, const wstring& mod) {
    if (g_cfg.d2rPath.empty()) {
        MessageBoxW(parent, L"Set your Diablo II: Resurrected path first.",
                    L"Install", MB_OK | MB_ICONWARNING);
        return false;
    }

    RepoPickCtx ctx{ parent, mod };
    PluginDropCallbacks cb = MakeRepoCallbacks(&ctx);

    InstallScope scope = modLocal ? InstallScope::Mod : InstallScope::Global;
    wstring selMod = modLocal ? mod : wstring();

    // Build the allowlist when installing into a manifest-mode mod.
    PluginAllowlist allow;
    if (modLocal && !mod.empty()) {
        wstring modDir = g_cfg.d2rPath + L"\\mods\\" + mod;
        PluginConfig mf = LoadPluginConfig(modDir);
        if (mf.present) {
            allow.active    = true;
            allow.entries   = mf.plugins;
            allow.modName   = mod;
            allow.modAuthor = mf.author;
        }
    }

    if (isPatch) {
        return HandleBarePatchDrop(filePath, g_cfg.d2rPath, scope, selMod, cb, allow);
    }
    return HandlePluginDropZip(filePath, g_cfg.d2rPath, scope, selMod,
                               ModListForPicker(), cb, allow);
}
