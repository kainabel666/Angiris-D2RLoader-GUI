// ═══════════════════════════════════════════════════════════════════════
//  plugin_drop_ui.cpp — UI glue for drag-drop plugin install (v1.6, Step 3)
// ═══════════════════════════════════════════════════════════════════════
//
//  Bridges the UI-agnostic routing core (plugin_install.cpp) to the
//  launcher's windows: supplies the prompt callbacks (no-manifest notice,
//  excel mod picker, encrypted-mpq notice, overwrite choice, error) and
//  the entry points the WM_DROPFILES handlers call.
//
//  Prompt dialogs here are FUNCTIONAL but not yet fully themed to match
//  ShowConflictDialog — theming is a follow-up polish pass. They use a
//  compact custom modal (overwrite) and a dropdown picker (excel).

#include "plugin_install.h"
#include "core.h"          // g_hInst
#include "config.h"        // g_cfg
#include "mod_scan.h"      // g_mods, ModInfo
#include "plugin_config.h" // LoadPluginConfig, PluginConfig (allowlist)
#include "plugin_drop_ui.h"

#include <windows.h>

using std::wstring;
using std::vector;

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
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassExW(&wc);
        reg = true;
    }

    int w = 420, h = hasConfig ? 190 : 160;
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - w) / 2;
    int y = pr.top  + ((pr.bottom - pr.top) - h) / 2;

    OverwriteResult res;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME,  // topmost dropped: owned popup stays above owner only
        L"AngirisOverwriteDlg", L"Plugin already exists",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h, parent, nullptr, g_hInst, nullptr);
    if (!dlg) return OverwriteChoice::Cancel;
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&res);

    wstring msg = L"Plugin already exists:\n" + pluginName;
    CreateWindowExW(0, L"STATIC", msg.c_str(),
        WS_CHILD | WS_VISIBLE, 16, 12, w - 32, 40, dlg, nullptr, g_hInst, nullptr);

    int by = hasConfig ? 62 : 70;
    if (hasConfig) {
        CreateWindowExW(0, L"BUTTON", L"DLL Only",
            WS_CHILD | WS_VISIBLE, 16, by, 120, 28,
            dlg, (HMENU)OW_DLL_ONLY, g_hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Config Only",
            WS_CHILD | WS_VISIBLE, 146, by, 120, 28,
            dlg, (HMENU)OW_CONFIG, g_hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"DLL and Config",
            WS_CHILD | WS_VISIBLE, 276, by, 128, 28,
            dlg, (HMENU)OW_BOTH, g_hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE, 146, by + 40, 120, 28,
            dlg, (HMENU)OW_CANCEL, g_hInst, nullptr);
    } else {
        CreateWindowExW(0, L"BUTTON", L"Yes",
            WS_CHILD | WS_VISIBLE, 90, by, 100, 30,
            dlg, (HMENU)OW_YES, g_hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"No",
            WS_CHILD | WS_VISIBLE, 220, by, 100, 30,
            dlg, (HMENU)OW_CANCEL, g_hInst, nullptr);
    }

    EnableWindow(parent, FALSE);
    MSG m;
    while (!res.done && GetMessageW(&m, nullptr, 0, 0)) {
        if (IsDialogMessageW(dlg, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
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
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassExW(&wc);
        reg = true;
    }

    int w = 460, h = 200;
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - w) / 2;
    int y = pr.top  + ((pr.bottom - pr.top) - h) / 2;

    ExcelPickResult res;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME,  // topmost dropped: owned popup stays above owner only
        L"AngirisExcelPickDlg", L"Which mod's Excel folder?",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h, parent, nullptr, g_hInst, nullptr);
    if (!dlg) return L"";
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&res);

    CreateWindowExW(0, L"STATIC",
        L"This Global Plugin contains a TXT file. Which MOD's Excel "
        L"folder should this be added to?",
        WS_CHILD | WS_VISIBLE, 16, 12, w - 32, 44, dlg, nullptr, g_hInst, nullptr);

    HWND combo = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        16, 64, w - 32, 200, dlg, (HMENU)EX_COMBO, g_hInst, nullptr);
    for (const wstring& m : mods)
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)m.c_str());
    if (!mods.empty()) SendMessageW(combo, CB_SETCURSEL, 0, 0);

    CreateWindowExW(0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE, w - 210, 118, 90, 30,
        dlg, (HMENU)EX_OK, g_hInst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE, w - 110, 118, 90, 30,
        dlg, (HMENU)EX_CANCEL, g_hInst, nullptr);

    EnableWindow(parent, FALSE);
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
