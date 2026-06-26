// ═══════════════════════════════════════════════════════════════════════
//  playtime.cpp — see playtime.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "playtime.h"
#include "core.h"   // AppDir, ReadTextFile, WriteTextFile, JsonStr, JsonInt, EscapeJson

// Single shared map. Definition lives here; declaration is in
// playtime.h so all readers / writers see the same instance.
map<wstring, PlaytimeRec> g_playtimes;

// Path helper — file-internal, not exposed in the header (no other
// module needs to know where the cache lives).
static wstring PlaytimeCachePath() {
    return AppDir() + L"\\assets\\playtime.json";
}

// Schema: { "entries": [ { "folder": "<name>", "seconds": <int>,
//                          "last_played": <epoch> }, ... ] }
// Same minimal-parser strategy as LoadUpdateCache — walk the array,
// pull the three fields out of each object, drop into the map.
void LoadPlaytimes() {
    g_playtimes.clear();
    wstring j = ReadTextFile(PlaytimeCachePath());
    if (j.empty()) return;
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
        int depth = 1; size_t obj2 = obj1 + 1;
        while (obj2 < arrEnd && depth > 0) {
            if (j[obj2] == L'{') ++depth;
            else if (j[obj2] == L'}') --depth;
            if (depth == 0) break;
            ++obj2;
        }
        if (obj2 >= arrEnd) break;
        wstring chunk = j.substr(obj1, obj2 - obj1 + 1);
        wstring folder = JsonStr(chunk, L"folder");
        if (!folder.empty()) {
            PlaytimeRec r;
            r.seconds    = (uint64_t)JsonInt(chunk, L"seconds",     0);
            r.lastPlayed = (time_t)  JsonInt(chunk, L"last_played", 0);
            g_playtimes[folder] = r;
        }
        p = obj2 + 1;
    }
}

void SavePlaytimes() {
    wstring j;
    j += L"{\n  \"entries\": [\n";
    bool first = true;
    for (const auto& kv : g_playtimes) {
        if (!first) j += L",\n";
        first = false;
        const PlaytimeRec& r = kv.second;
        wchar_t buf[64];
        j += L"    {\n";
        j += L"      \"folder\":      \"" + EscapeJson(kv.first) + L"\",\n";
        swprintf(buf, 64, L"%llu", (unsigned long long)r.seconds);
        j += wstring(L"      \"seconds\":     ") + buf + L",\n";
        swprintf(buf, 64, L"%lld", (long long)r.lastPlayed);
        j += wstring(L"      \"last_played\": ") + buf + L"\n";
        j += L"    }";
    }
    j += L"\n  ]\n}";
    WriteTextFile(PlaytimeCachePath(), j);
}

void RecordPlaytime(const wstring& modFolder, uint64_t secondsToAdd) {
    if (modFolder.empty()) return;
    PlaytimeRec& r = g_playtimes[modFolder];
    r.seconds   += secondsToAdd;
    r.lastPlayed = time(nullptr);
    SavePlaytimes();
}

wstring FormatPlaytime(uint64_t seconds) {
    if (seconds == 0) return L"Never played";
    uint64_t h = seconds / 3600;
    uint64_t m = (seconds % 3600) / 60;
    wchar_t buf[64];
    if (h > 0)      swprintf_s(buf, 64, L"%lluh %llum", h, m);
    else if (m > 0) swprintf_s(buf, 64, L"%llum",       m);
    else            swprintf_s(buf, 64, L"< 1m");
    return buf;
}

wstring FormatLastPlayed(time_t lastPlayed) {
    if (lastPlayed == 0) return L"Never";
    time_t now = time(nullptr);
    time_t d   = now - lastPlayed;
    if (d < 0)        d = 0;
    if (d < 60)       return L"Just now";
    wchar_t buf[64];
    if (d < 3600) {
        long m = (long)(d / 60);
        swprintf_s(buf, 64, L"%ld minute%s ago", m, m == 1 ? L"" : L"s");
        return buf;
    }
    if (d < 86400) {
        long h = (long)(d / 3600);
        swprintf_s(buf, 64, L"%ld hour%s ago", h, h == 1 ? L"" : L"s");
        return buf;
    }
    if (d < 30 * 86400) {
        long days = (long)(d / 86400);
        swprintf_s(buf, 64, L"%ld day%s ago", days, days == 1 ? L"" : L"s");
        return buf;
    }
    // > 30 days — absolute date is more meaningful than "5 months ago".
    struct tm tm_;
    localtime_s(&tm_, &lastPlayed);
    swprintf_s(buf, 64, L"%04d-%02d-%02d",
               tm_.tm_year + 1900, tm_.tm_mon + 1, tm_.tm_mday);
    return buf;
}
