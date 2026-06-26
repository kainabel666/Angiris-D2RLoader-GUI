// ═══════════════════════════════════════════════════════════════════════
//  mod_scan.cpp — see mod_scan.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "mod_scan.h"
#include "core.h"     // ReadTextFile, JsonStr, g_hwMain
#include "config.h"   // g_cfg.d2rPath

// Live mod list + selection index. Definitions live here; declarations
// in the header so paint/UI code in Angiris.cpp and other modules read
// the same instances via extern.
vector<ModInfo> g_mods;
int             g_selMod = -1;

// ── Modinfo parsing ──────────────────────────────────────────────────
//
// Parse one modinfo.json file into a ModInfo. Handles both the flat
// layout (modinfo.json next to the mod folder) and the nested .mpq
// layout (modinfo.json inside <mod>\<sub>.mpq\). The folder name
// used for the -mod argument is derived from the parent of the .mpq
// rather than the .mpq itself when nested.
static ModInfo ModInfoFromJsonPath(const wstring& jsonPath) {
    wstring json = ReadTextFile(jsonPath);

    wstring name = JsonStr(json, L"name");
    if (name.empty()) name = JsonStr(json, L"savepath");

    wstring parentDir  = jsonPath.substr(0, jsonPath.rfind(L'\\'));
    wstring parentName = parentDir.substr(parentDir.rfind(L'\\') + 1);

    wstring modDir = parentDir;
    if (parentName.size() > 4
        && _wcsicmp(parentName.substr(parentName.size() - 4).c_str(), L".mpq") == 0) {
        modDir = parentDir.substr(0, parentDir.rfind(L'\\'));
    }
    wstring folder = modDir.substr(modDir.rfind(L'\\') + 1);
    if (name.empty()) name = folder;

    ModInfo mi;
    mi.name        = name;
    mi.folder      = folder;
    mi.dir         = modDir;
    mi.title       = JsonStr(json, L"title");
    mi.description = JsonStr(json, L"description");
    mi.overview    = JsonStr(json, L"overview");
    mi.version     = JsonStr(json, L"version");
    mi.author      = JsonStr(json, L"author");
    mi.docsUrl       = JsonStr(json, L"docs");
    if (mi.docsUrl.empty())
        mi.docsUrl   = JsonStr(json, L"documents");      // back-compat alias
    mi.websiteUrl    = JsonStr(json, L"website");
    mi.discordUrl    = JsonStr(json, L"discord");
    mi.updateGithub   = JsonStr(json, L"update_github");
    mi.updateManifest = JsonStr(json, L"update_manifest");
    mi.savePath       = JsonStr(json, L"savepath");

    wstring jsonDir = jsonPath.substr(0, jsonPath.rfind(L'\\'));
    mi.launcherDir = jsonDir + L"\\Launcher Files";

    wstring bannerFile = JsonStr(json, L"banner");
    if (!bannerFile.empty()) {
        mi.bannerPath = mi.launcherDir + L"\\" + bannerFile;
        if (GetFileAttributes(mi.bannerPath.c_str()) == INVALID_FILE_ATTRIBUTES)
            mi.bannerPath.clear();
    }
    return mi;
}

vector<ModInfo> FindMods(const wstring& d2rPath) {
    vector<ModInfo> out;
    if (d2rPath.empty()) return out;
    wstring modsDir = d2rPath + L"\\mods";

    WIN32_FIND_DATA ffd;
    HANDLE h = FindFirstFile((modsDir + L"\\*").c_str(), &ffd);
    if (h == INVALID_HANDLE_VALUE) return out;

    vector<wstring> modDirs;
    do {
        if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            && wcscmp(ffd.cFileName, L".") != 0
            && wcscmp(ffd.cFileName, L"..") != 0)
            modDirs.push_back(ffd.cFileName);
    } while (FindNextFile(h, &ffd));
    FindClose(h);

    sort(modDirs.begin(), modDirs.end(),
         [](auto& a, auto& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });

    for (const auto& dir : modDirs) {
        wstring modPath = modsDir + L"\\" + dir;
        wstring direct = modPath + L"\\modinfo.json";
        if (GetFileAttributes(direct.c_str()) != INVALID_FILE_ATTRIBUTES) {
            out.push_back(ModInfoFromJsonPath(direct));
            continue;
        }
        // Nested .mpq layout
        WIN32_FIND_DATA ffd2;
        HANDLE h2 = FindFirstFile((modPath + L"\\*").c_str(), &ffd2);
        if (h2 != INVALID_HANDLE_VALUE) {
            do {
                if (!(ffd2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                wstring sub = ffd2.cFileName;
                if (sub == L"." || sub == L"..") continue;
                wstring candidate = modPath + L"\\" + sub + L"\\modinfo.json";
                if (GetFileAttributes(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    out.push_back(ModInfoFromJsonPath(candidate));
                    break;
                }
            } while (FindNextFile(h2, &ffd2));
            FindClose(h2);
        }
    }
    return out;
}

// ── Directory watcher ─────────────────────────────────────────────────
//
// Watches <D2R>\mods\ recursively for filesystem changes (mod folder
// added/removed/renamed, files modified). On any change the thread
// posts MSG_MODS_DIRTY to g_hwMain and goes back to waiting. The main
// window debounces and rescans via FindMods.

static HANDLE g_watchThread     = nullptr;
static HANDLE g_watchCancelEvt  = nullptr;     // set to wake the thread
static HANDLE g_watchDirHandle  = nullptr;     // owned by watcher thread
static wstring g_watchDirPath;                 // last-watched path

static DWORD WINAPI ModsWatcherThread(LPVOID) {
    constexpr DWORD BUFFER_SIZE = 16 * 1024;   // 16KB — covers normal bursts
    BYTE* buffer = (BYTE*)malloc(BUFFER_SIZE);
    if (!buffer) return 1;

    OVERLAPPED overlapped = {};
    HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    overlapped.hEvent = evt;

    while (true) {
        // Wait for cancellation OR a directory change
        DWORD bytes = 0;
        BOOL ok = ReadDirectoryChangesW(
            g_watchDirHandle, buffer, BUFFER_SIZE,
            TRUE,                                    // watch subtree
            FILE_NOTIFY_CHANGE_FILE_NAME
                | FILE_NOTIFY_CHANGE_DIR_NAME
                | FILE_NOTIFY_CHANGE_LAST_WRITE,
            &bytes, &overlapped, nullptr);
        if (!ok) break;

        HANDLE waitOn[2] = { evt, g_watchCancelEvt };
        DWORD w = WaitForMultipleObjects(2, waitOn, FALSE, INFINITE);
        if (w == WAIT_OBJECT_0 + 1) {
            // Cancellation
            CancelIoEx(g_watchDirHandle, &overlapped);
            break;
        }
        if (w != WAIT_OBJECT_0) break;

        // Got a real change — let the main thread debounce + rescan
        if (g_hwMain) PostMessage(g_hwMain, MSG_MODS_DIRTY, 0, 0);
    }

    CloseHandle(evt);
    free(buffer);
    return 0;
}

void StopModsWatcher() {
    if (!g_watchThread) return;
    if (g_watchCancelEvt) SetEvent(g_watchCancelEvt);
    if (g_watchDirHandle) CancelIoEx(g_watchDirHandle, nullptr);
    WaitForSingleObject(g_watchThread, 1500);
    CloseHandle(g_watchThread);   g_watchThread    = nullptr;
    if (g_watchCancelEvt) { CloseHandle(g_watchCancelEvt); g_watchCancelEvt = nullptr; }
    if (g_watchDirHandle) { CloseHandle(g_watchDirHandle); g_watchDirHandle = nullptr; }
    g_watchDirPath.clear();
}

void StartModsWatcher() {
    // No-op if already watching the same folder
    wstring target = g_cfg.d2rPath.empty() ? L"" : (g_cfg.d2rPath + L"\\mods");
    if (target == g_watchDirPath && g_watchThread) return;

    StopModsWatcher();   // tear down any previous watcher

    if (target.empty()
        || GetFileAttributes(target.c_str()) == INVALID_FILE_ATTRIBUTES)
        return;

    g_watchDirHandle = CreateFileW(
        target.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);
    if (g_watchDirHandle == INVALID_HANDLE_VALUE) {
        g_watchDirHandle = nullptr;
        return;
    }

    g_watchCancelEvt = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    g_watchDirPath   = target;
    g_watchThread    = CreateThread(nullptr, 0, ModsWatcherThread, nullptr, 0, nullptr);
}
