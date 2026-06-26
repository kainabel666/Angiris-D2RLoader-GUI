// ═══════════════════════════════════════════════════════════════════════
//  mod_updates.cpp — see mod_updates.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "mod_updates.h"
#include "core.h"           // JsonStr, g_hwMain
#include "version.h"        // CompareVersions
#include "http.h"           // HttpGet, HttpResult
#include "update_cache.h"   // g_updateInfo, SaveUpdateCache
#include "mod_scan.h"       // g_mods

// Running-count of dispatched fetches. Each UpdateFetchWorker
// decrements on completion; the last one out triggers a final
// SaveUpdateCache. Kept here (not in update_cache) because it's
// strictly an in-flight-tracking concern, not a persisted value.
static atomic<int> g_updateChecksPending(0);

static UpdateInfo ParseGithubReleaseJson(const wstring& body,
                                          const wstring& localVersion) {
    UpdateInfo ui;
    ui.remoteVersion = JsonStr(body, L"tag_name");
    ui.changelog     = JsonStr(body, L"body");
    ui.releaseDate   = JsonStr(body, L"published_at");
    ui.sourceUrl     = JsonStr(body, L"html_url");
    // Find the first ".zip" download URL within assets[]
    size_t assets = body.find(L"\"assets\"");
    if (assets != wstring::npos) {
        size_t arr = body.find(L'[', assets);
        size_t end = (arr != wstring::npos) ? body.find(L']', arr) : wstring::npos;
        if (arr != wstring::npos && end != wstring::npos) {
            // Scan for browser_download_url ending in .zip
            size_t p = arr;
            while (p < end) {
                size_t k = body.find(L"\"browser_download_url\"", p);
                if (k == wstring::npos || k >= end) break;
                size_t colon = body.find(L':', k);
                size_t q1    = body.find(L'"', colon + 1);
                size_t q2    = (q1 != wstring::npos) ? body.find(L'"', q1 + 1) : wstring::npos;
                if (q2 == wstring::npos) break;
                wstring url = body.substr(q1 + 1, q2 - q1 - 1);
                if (url.size() >= 4 &&
                    _wcsicmp(url.c_str() + url.size() - 4, L".zip") == 0) {
                    ui.downloadUrl = url;
                    break;
                }
                p = q2 + 1;
            }
        }
    }
    ui.localVersion = localVersion;
    ui.fetched = !ui.remoteVersion.empty();
    if (ui.fetched) {
        ui.available = (CompareVersions(ui.remoteVersion, localVersion) > 0);
    }
    return ui;
}

static UpdateInfo ParseGenericManifest(const wstring& body,
                                        const wstring& localVersion) {
    UpdateInfo ui;
    ui.remoteVersion = JsonStr(body, L"latest_version");
    ui.changelog     = JsonStr(body, L"changelog");
    ui.releaseDate   = JsonStr(body, L"release_date");
    ui.sourceUrl     = JsonStr(body, L"source_url");
    ui.downloadUrl   = JsonStr(body, L"download_url");
    ui.sha256        = JsonStr(body, L"sha256");
    ui.localVersion  = localVersion;
    ui.fetched       = !ui.remoteVersion.empty();
    if (ui.fetched) {
        ui.available = (CompareVersions(ui.remoteVersion, localVersion) > 0);
    }
    return ui;
}

// Resolves URL, runs HTTP GET, parses, fills UpdateInfo. Called from a
// worker thread; main thread posts MSG_UPDATE_CHECK_DONE on completion.
static UpdateInfo FetchModUpdateOnce(const ModInfo& mod, int timeoutMs,
                                      bool& outTimedOut) {
    outTimedOut = false;
    UpdateInfo ui;
    ui.localVersion = mod.version;

    wstring url;
    bool isGithub = false;
    if (!mod.updateGithub.empty()) {
        url = L"https://api.github.com/repos/" + mod.updateGithub + L"/releases/latest";
        isGithub = true;
    } else if (!mod.updateManifest.empty()) {
        url = mod.updateManifest;
    } else {
        return ui;     // not opted in
    }

    HttpResult r = HttpGet(url, timeoutMs);
    ui.httpStatus = r.status;
    if (r.timedOut) { outTimedOut = true; return ui; }
    if (r.status != 200 || r.body.empty()) return ui;

    if (isGithub) ui = ParseGithubReleaseJson(r.body, mod.version);
    else          ui = ParseGenericManifest (r.body, mod.version);
    ui.fetchedAt = time(nullptr);

    // Honor skipped-version: if the user previously skipped this exact
    // remote version, don't show it as available.
    auto it = g_updateInfo.find(mod.folder);
    if (it != g_updateInfo.end() && !it->second.skippedVersion.empty()
        && it->second.skippedVersion == ui.remoteVersion) {
        ui.skippedVersion = it->second.skippedVersion;
        ui.available = false;
    }
    return ui;
}

static DWORD WINAPI UpdateFetchWorker(LPVOID param) {
    wstring* folder = (wstring*)param;
    // Find the ModInfo by folder name (the worker only stores folder so
    // we can't race against a g_mods reshuffle holding pointers).
    ModInfo modCopy;
    bool found = false;
    for (const auto& m : g_mods) {
        if (m.folder == *folder) { modCopy = m; found = true; break; }
    }
    if (found) {
        bool timedOut = false;
        UpdateInfo ui = FetchModUpdateOnce(modCopy, UPDATE_HTTP_TIMEOUT_FAST, timedOut);
        if (timedOut) {
            ui.timedOut = true;
            ui.fetched  = false;
        }
        // Preserve skipped_version from any previous cache entry
        auto it = g_updateInfo.find(*folder);
        if (it != g_updateInfo.end()) {
            if (ui.skippedVersion.empty())
                ui.skippedVersion = it->second.skippedVersion;
        }
        g_updateInfo[*folder] = ui;
    }
    delete folder;
    if (--g_updateChecksPending == 0) {
        SaveUpdateCache();
        if (g_hwMain) PostMessage(g_hwMain, MSG_UPDATE_CHECK_DONE, 0, 0);
    } else {
        if (g_hwMain) PostMessage(g_hwMain, MSG_UPDATE_CHECK_DONE, 0, 0);
    }
    return 0;
}

void KickUpdateChecks(bool force) {
    time_t now = time(nullptr);
    for (const auto& m : g_mods) {
        if (m.updateGithub.empty() && m.updateManifest.empty()) continue;

        // TTL check: skip if we have a fresh enough cached result
        if (!force) {
            auto it = g_updateInfo.find(m.folder);
            if (it != g_updateInfo.end() && it->second.fetched
                && now - it->second.fetchedAt < UPDATE_CACHE_TTL_SECONDS) {
                continue;
            }
        }
        ++g_updateChecksPending;
        wstring* folder = new wstring(m.folder);
        HANDLE h = CreateThread(nullptr, 0, UpdateFetchWorker, folder, 0, nullptr);
        if (h) CloseHandle(h);
        else { --g_updateChecksPending; delete folder; }
    }
}

const UpdateInfo* GetUpdateInfo(const wstring& folder) {
    auto it = g_updateInfo.find(folder);
    return (it == g_updateInfo.end()) ? nullptr : &it->second;
}
