// ═══════════════════════════════════════════════════════════════════════
//  update_cache.cpp — see update_cache.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "update_cache.h"
#include "core.h"   // AppDir, ReadTextFile, WriteTextFile, JsonStr, JsonInt, EscapeJson

map<wstring, UpdateInfo> g_updateInfo;

static wstring UpdateCachePath() {
    return AppDir() + L"\\assets\\update_cache.json";
}

void LoadUpdateCache() {
    g_updateInfo.clear();
    wstring j = ReadTextFile(UpdateCachePath());
    if (j.empty()) return;
    // Schema: { "entries": [ { "folder": "...", "remote_version": "...", ... }, ... ] }
    size_t arrStart = j.find(L"\"entries\"");
    if (arrStart == wstring::npos) return;
    arrStart = j.find(L'[', arrStart);
    if (arrStart == wstring::npos) return;
    size_t arrEnd = j.find(L']', arrStart);
    if (arrEnd == wstring::npos) return;

    size_t p = arrStart + 1;
    while (p < arrEnd) {
        size_t obj1 = j.find(L'{', p);
        if (obj1 == wstring::npos || obj1 >= arrEnd) break;
        // Track brace depth so nested objects (changelog etc) don't fool us
        int depth = 1; size_t obj2 = obj1 + 1;
        while (obj2 < arrEnd && depth > 0) {
            if (j[obj2] == L'{') ++depth;
            else if (j[obj2] == L'}') --depth;
            if (depth == 0) break;
            ++obj2;
        }
        if (obj2 >= arrEnd) break;
        wstring chunk = j.substr(obj1, obj2 - obj1 + 1);
        UpdateInfo ui;
        wstring folder = JsonStr(chunk, L"folder");
        ui.remoteVersion  = JsonStr(chunk, L"remote_version");
        ui.localVersion   = JsonStr(chunk, L"local_version");
        ui.changelog      = JsonStr(chunk, L"changelog");
        ui.downloadUrl    = JsonStr(chunk, L"download_url");
        ui.sourceUrl      = JsonStr(chunk, L"source_url");
        ui.releaseDate    = JsonStr(chunk, L"release_date");
        ui.sha256         = JsonStr(chunk, L"sha256");
        ui.skippedVersion = JsonStr(chunk, L"skipped_version");
        ui.fetchedAt      = (time_t)JsonInt(chunk, L"fetched_at", 0);
        ui.httpStatus     = JsonInt(chunk, L"http_status", 0);
        ui.fetched        = !ui.remoteVersion.empty();
        if (!folder.empty()) g_updateInfo[folder] = ui;
        p = obj2 + 1;
    }
}

void SaveUpdateCache() {
    wstring j;
    j += L"{\n  \"entries\": [\n";
    bool first = true;
    for (const auto& kv : g_updateInfo) {
        if (!first) j += L",\n";
        first = false;
        const UpdateInfo& ui = kv.second;
        wchar_t buf[64];
        j += L"    {\n";
        j += L"      \"folder\":          \"" + EscapeJson(kv.first) + L"\",\n";
        j += L"      \"local_version\":   \"" + EscapeJson(ui.localVersion) + L"\",\n";
        j += L"      \"remote_version\":  \"" + EscapeJson(ui.remoteVersion) + L"\",\n";
        j += L"      \"changelog\":       \"" + EscapeJson(ui.changelog) + L"\",\n";
        j += L"      \"download_url\":    \"" + EscapeJson(ui.downloadUrl) + L"\",\n";
        j += L"      \"source_url\":      \"" + EscapeJson(ui.sourceUrl) + L"\",\n";
        j += L"      \"release_date\":    \"" + EscapeJson(ui.releaseDate) + L"\",\n";
        j += L"      \"sha256\":          \"" + EscapeJson(ui.sha256) + L"\",\n";
        j += L"      \"skipped_version\": \"" + EscapeJson(ui.skippedVersion) + L"\",\n";
        swprintf(buf, 64, L"%lld", (long long)ui.fetchedAt);
        j += wstring(L"      \"fetched_at\":     ") + buf + L",\n";
        swprintf(buf, 64, L"%d", ui.httpStatus);
        j += wstring(L"      \"http_status\":    ") + buf + L"\n";
        j += L"    }";
    }
    j += L"\n  ]\n}";
    WriteTextFile(UpdateCachePath(), j);
}
