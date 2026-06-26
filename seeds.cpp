// ═══════════════════════════════════════════════════════════════════════
//  seeds.cpp — see seeds.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "seeds.h"
#include "core.h"   // AppDir, ReadTextFile, WriteTextFile, JsonStr, EscapeJson

vector<wstring> g_seedNames;
vector<wstring> g_seedValues;
vector<wstring> g_recentSeeds;   // 0..3 entries; back = newest
int             g_lastSeedIdx = 0;

// Path to seeds.json — file-internal helper.
static wstring SeedsJsonPath() {
    return AppDir() + L"\\assets\\seeds.json";
}

void ParseSeedArray(const wstring& body,
                    void (*emit)(const wstring& name, const wstring& value,
                                 void* user),
                    void* user) {
    size_t pos = 0;
    while (pos < body.size()) {
        size_t open = body.find(L'{', pos);
        if (open == wstring::npos) break;
        size_t close = body.find(L'}', open + 1);
        if (close == wstring::npos) break;
        wstring block = body.substr(open, close - open + 1);
        wstring nm = JsonStr(block, L"name");
        wstring vl = JsonStr(block, L"value");
        if (!vl.empty()) emit(nm, vl, user);
        pos = close + 1;
    }
}

void LoadSeedsJson() {
    g_seedNames.clear();
    g_seedValues.clear();
    g_recentSeeds.clear();

    wstring j = ReadTextFile(SeedsJsonPath());
    if (!j.empty()) {
        // Find each top-level array key, then parse the array body. The
        // launcher's hand-rolled JSON style means we use string search
        // rather than a proper parser — keys are at known positions and
        // there's no nested ambiguity.
        auto findArray = [&](const wchar_t* key) -> wstring {
            wstring k = wstring(L"\"") + key + L"\"";
            size_t p = j.find(k);
            if (p == wstring::npos) return L"";
            p = j.find(L'[', p);
            if (p == wstring::npos) return L"";
            // Match the closing ']' by depth.
            int depth = 1;
            size_t q = p + 1;
            while (q < j.size() && depth > 0) {
                if (j[q] == L'[') depth++;
                else if (j[q] == L']') depth--;
                if (depth == 0) break;
                ++q;
            }
            return j.substr(p + 1, q - p - 1);
        };

        wstring seedsBody  = findArray(L"seeds");
        wstring recentBody = findArray(L"recent");

        struct PresetCtx { vector<wstring>* names; vector<wstring>* values; };
        PresetCtx pc{ &g_seedNames, &g_seedValues };
        ParseSeedArray(seedsBody,
            [](const wstring& nm, const wstring& vl, void* u) {
                auto* c = (PresetCtx*)u;
                c->names ->push_back(nm.empty() ? vl : nm);
                c->values->push_back(vl);
            }, &pc);

        ParseSeedArray(recentBody,
            [](const wstring& /*nm*/, const wstring& vl, void* u) {
                auto* v = (vector<wstring>*)u;
                if (v->size() < 3) v->push_back(vl);
            }, &g_recentSeeds);
    }
    // Fallback presets if seeds.json is missing or has no "seeds" array.
    // Drop a real file in assets/ and these go away on next launch.
    if (g_seedNames.empty()) {
        g_seedNames  = { L"Random (0)",    L"Standard (1)", L"Speedrun A",   L"Speedrun B" };
        g_seedValues = { L"0",             L"1",            L"1234567",      L"7654321"    };
    }
}

void SaveSeedsJson() {
    wstring j;
    j += L"{\n";
    j += L"  \"seeds\": [\n";
    for (size_t i = 0; i < g_seedNames.size(); ++i) {
        j += L"    {\"name\": \"" + EscapeJson(g_seedNames[i])
          +  L"\", \"value\": \"" + EscapeJson(g_seedValues[i]) + L"\"}";
        if (i + 1 < g_seedNames.size()) j += L",";
        j += L"\n";
    }
    j += L"  ],\n";
    j += L"  \"recent\": [\n";
    for (size_t i = 0; i < g_recentSeeds.size(); ++i) {
        // Slot label is fixed by position: index 0 → "Recent1" (oldest),
        // index 2 → "Recent3" (newest). Matches the spec.
        wchar_t nm[16]; swprintf(nm, 16, L"Recent%zu", i + 1);
        j += L"    {\"name\": \"" + wstring(nm)
          +  L"\", \"value\": \"" + EscapeJson(g_recentSeeds[i]) + L"\"}";
        if (i + 1 < g_recentSeeds.size()) j += L",";
        j += L"\n";
    }
    j += L"  ]\n";
    j += L"}\n";
    WriteTextFile(SeedsJsonPath(), j);
}

bool CommitTypedSeedToRecents(const wstring& value) {
    if (value.empty()) return false;
    // Don't pollute recents with values that already match a preset
    // (the user can just re-pick from the dropdown).
    for (const auto& v : g_seedValues)
        if (v == value) return false;
    // Don't duplicate within recents — if it's already there, leave it
    // alone rather than reshuffling identical entries.
    for (const auto& v : g_recentSeeds)
        if (v == value) return false;
    // Shift down: drop the oldest entry once we'd exceed 3, then append
    // the new value at the end (which becomes "Recent3" / newest).
    g_recentSeeds.push_back(value);
    while (g_recentSeeds.size() > 3) g_recentSeeds.erase(g_recentSeeds.begin());
    SaveSeedsJson();
    return true;
}

int FindSeedIndexForValue(const wstring& val) {
    for (int i = 0; i < (int)g_seedValues.size(); ++i)
        if (g_seedValues[i] == val) return i;
    return -1;
}
