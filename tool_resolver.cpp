// ═══════════════════════════════════════════════════════════════════════
//  tool_resolver.cpp — see tool_resolver.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "tool_resolver.h"
#include "config.h"   // g_cfg.toolsDir, SaveCfg

wstring ResolveShortcut(const wstring& lnkPath) {
    HRESULT hr;
    IShellLinkW* psl = nullptr;
    wstring result;

    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                   (void**)&psl))) {
        IPersistFile* ppf = nullptr;
        if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void**)&ppf))) {
            if (SUCCEEDED(ppf->Load(lnkPath.c_str(), STGM_READ))) {
                wchar_t buf[MAX_PATH] = {};
                if (SUCCEEDED(psl->GetPath(buf, MAX_PATH, nullptr, 0)))
                    result = buf;
            }
            ppf->Release();
        }
        psl->Release();
    }
    return result;
    (void)hr;
}

wstring SearchToolRecursive(const wstring& root, const wstring& targetExe,
                            int depth, int* visited) {
    int localVisited = 0;
    if (!visited) visited = &localVisited;
    if (depth > 6 || *visited > 5000) return L"";

    // Stem: "Code.exe" -> "Code"
    wstring stem = targetExe;
    size_t dot = stem.rfind(L'.');
    if (dot != wstring::npos) stem = stem.substr(0, dot);
    wstring linkName = stem + L".lnk";

    WIN32_FIND_DATAW ffd;
    HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &ffd);
    if (h == INVALID_HANDLE_VALUE) return L"";

    wstring found;
    do {
        ++(*visited);
        wstring name = ffd.cFileName;
        if (name == L"." || name == L"..") continue;
        wstring full = root + L"\\" + name;

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            wstring sub = SearchToolRecursive(full, targetExe, depth + 1, visited);
            if (!sub.empty()) { found = sub; break; }
        } else {
            // Direct .exe match
            if (_wcsicmp(name.c_str(), targetExe.c_str()) == 0) {
                found = full; break;
            }
            // Shortcut match — resolve it
            if (_wcsicmp(name.c_str(), linkName.c_str()) == 0) {
                wstring target = ResolveShortcut(full);
                if (!target.empty()) { found = target; break; }
            }
        }
    } while (FindNextFileW(h, &ffd));
    FindClose(h);
    return found;
}

wstring BrowseForTool(HWND owner, const wstring& title) {
    wchar_t buf[MAX_PATH] = {};
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner   = owner;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrFilter = L"Programs and shortcuts (*.exe;*.lnk)\0*.exe;*.lnk\0"
                      L"Programs (*.exe)\0*.exe\0"
                      L"Shortcuts (*.lnk)\0*.lnk\0"
                      L"All files (*.*)\0*.*\0";
    ofn.lpstrTitle  = title.c_str();
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return L"";

    wstring picked = buf;
    // If the user picked a shortcut, resolve to target so we can launch directly
    size_t dot = picked.rfind(L'.');
    if (dot != wstring::npos
        && _wcsicmp(picked.c_str() + dot, L".lnk") == 0) {
        wstring target = ResolveShortcut(picked);
        if (!target.empty()) return target;
    }
    return picked;
}

void LaunchTool(HWND owner, wstring& cachedPath,
                const wstring& exeName, const wstring& friendlyName) {
    // 1. Try cached path
    if (!cachedPath.empty()
        && GetFileAttributes(cachedPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        ShellExecute(owner, L"open", cachedPath.c_str(),
                     nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }

    // 2. Search the configured tools directory
    if (!g_cfg.toolsDir.empty()
        && GetFileAttributes(g_cfg.toolsDir.c_str()) != INVALID_FILE_ATTRIBUTES) {
        wstring hit = SearchToolRecursive(g_cfg.toolsDir, exeName);
        if (!hit.empty()) {
            cachedPath = hit;
            SaveCfg();
            ShellExecute(owner, L"open", hit.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }
    }

    // 3. Not found — ask the user
    wstring msg = friendlyName + L" was not found";
    if (g_cfg.toolsDir.empty())
        msg += L".\n\nNo Tools Directory has been set. Browse to the executable manually?";
    else
        msg += L"\nin: " + g_cfg.toolsDir + L"\n\nBrowse manually?";

    int rc = MessageBox(owner, msg.c_str(),
                        L"Tool Not Found", MB_YESNO | MB_ICONQUESTION);
    if (rc != IDYES) return;

    wstring picked = BrowseForTool(owner, L"Locate " + friendlyName);
    if (picked.empty()) return;
    cachedPath = picked;
    SaveCfg();
    ShellExecute(owner, L"open", picked.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
