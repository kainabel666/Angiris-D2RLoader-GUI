// ═══════════════════════════════════════════════════════════════════════
//  plugin_manifest.cpp — see plugin_manifest.h for the public surface
// ═══════════════════════════════════════════════════════════════════════
//
//  In-memory store is a flat unordered_map keyed by the LOWERCASED DLL
//  filename so lookups are case-insensitive without iterating. Friendly
//  names preserve their original casing (whatever the user/author
//  typed in the JSON file).
//
//  The JSON parser is intentionally hand-rolled rather than dragging in
//  a full library — the schema is a single nested object of strings,
//  small enough that ~80 lines of code cover the cases. core.h's
//  JsonStr/JsonInt/JsonBool are scalar helpers and don't handle nested
//  objects with arbitrary keys, so we walk the file directly here.

#include "plugin_manifest.h"
#include "core.h"           // ReadTextFile, WriteTextFile, AppDir

#include <unordered_map>
#include <algorithm>        // std::sort

namespace {

// ── State ────────────────────────────────────────────────────────────

// Case-insensitive hash + equality so the map can store keys in their
// ORIGINAL casing (matching how the DLL appears on disk / in the JSON
// file) while still hashing/comparing in a case-insensitive way. This
// matters because when we serialize back to plugin_manifest.json, the
// user sees their original spelling — saves don't silently rewrite
// "PD2.dll" as "pd2.dll".
struct DllKeyHash {
    size_t operator()(const wstring& s) const noexcept {
        size_t h = 1469598103934665603ull;          // FNV-1a basis
        for (wchar_t c : s) {
            if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
            h ^= (size_t)c;
            h *= 1099511628211ull;
        }
        return h;
    }
};
struct DllKeyEqual {
    bool operator()(const wstring& a, const wstring& b) const noexcept {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            wchar_t ca = a[i], cb = b[i];
            if (ca >= L'A' && ca <= L'Z') ca = (wchar_t)(ca - L'A' + L'a');
            if (cb >= L'A' && cb <= L'Z') cb = (wchar_t)(cb - L'A' + L'a');
            if (ca != cb) return false;
        }
        return true;
    }
};

// DLL filename (original casing) → friendly display name (original casing).
// Populated by LoadPluginManifest, read by GetPluginFriendlyName,
// mutated by EnsureManifestEntries, written by SavePluginManifest.
std::unordered_map<wstring, wstring, DllKeyHash, DllKeyEqual> g_friendlyNames;

// DLL filename → readme path (relative to the launcher folder, e.g.
// "d2rloader\\readmes\\Warlock\\warlock-Readme.txt"). Stored in a PARALLEL
// "readmes" object in the JSON rather than changing the "names" value type,
// so old manifest files (which have only "names") still load unchanged —
// they simply produce no readme entries. Added v1.6 for drag-drop install.
std::unordered_map<wstring, wstring, DllKeyHash, DllKeyEqual> g_readmePaths;

// ── Helpers ──────────────────────────────────────────────────────────

// Unescape a JSON string literal body — same backslash table the rest
// of the launcher uses (matches core.cpp's JsonStr and plugin_config's
// parser so manifest files behave consistently with every other JSON
// file in the launcher).
wstring UnescapeJsonStringBody(const wstring& raw) {
    wstring out; out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == L'\\' && i + 1 < raw.size()) {
            switch (raw[i + 1]) {
                case L'\\': out += L'\\'; ++i; break;
                case L'"':  out += L'"';  ++i; break;
                case L'/':  out += L'/';  ++i; break;
                case L'n':  out += L'\n'; ++i; break;
                case L't':  out += L'\t'; ++i; break;
                case L'r':  out += L'\r'; ++i; break;
                default:    out += raw[i]; break;
            }
        } else {
            out += raw[i];
        }
    }
    return out;
}

void SkipWhitespace(const wstring& j, size_t& p) {
    while (p < j.size() && (j[p] == L' ' || j[p] == L'\t' ||
                            j[p] == L'\n' || j[p] == L'\r')) {
        ++p;
    }
}

// Parse a quoted string starting at j[p] == '"'. On success advances
// `p` past the closing quote and returns the unescaped body. On
// failure (no opening quote, unterminated string) leaves `p`
// unchanged and returns false.
bool ParseString(const wstring& j, size_t& p, wstring& out) {
    if (p >= j.size() || j[p] != L'"') return false;
    size_t start = p + 1;
    size_t end = start;
    while (end < j.size() && !(j[end] == L'"' && j[end - 1] != L'\\')) ++end;
    if (end >= j.size()) return false;            // unterminated
    out = UnescapeJsonStringBody(j.substr(start, end - start));
    p = end + 1;
    return true;
}

// Walk forward from the opening `{` of a string→string object, collecting
// "key": "value" pairs into `target`. Stops at the matching `}`. Non-string
// values, bare numbers, and other malformed tokens are skipped silently —
// the manifest must never crash the launcher. Shared by the "names" and
// "readmes" objects, which have the identical shape.
void ParseStringObject(const wstring& j, size_t openBrace,
        std::unordered_map<wstring, wstring, DllKeyHash, DllKeyEqual>& target) {
    size_t p = openBrace + 1;                     // skip `{`
    while (p < j.size()) {
        SkipWhitespace(j, p);
        if (p >= j.size()) break;
        if (j[p] == L'}') break;                  // end of object
        if (j[p] == L',') { ++p; continue; }      // between pairs

        // Key — must be a quoted string
        wstring key;
        if (!ParseString(j, p, key)) {
            // Not a string — advance one char to make progress and
            // hope to resync at the next comma/close-brace.
            ++p;
            continue;
        }

        SkipWhitespace(j, p);
        if (p >= j.size() || j[p] != L':') continue;
        ++p;
        SkipWhitespace(j, p);

        // Value — must be a quoted string. If it's not (e.g. the user
        // put a number or null), skip this pair entirely.
        wstring val;
        if (!ParseString(j, p, val)) {
            // Skip past the malformed value to the next comma/brace.
            while (p < j.size() && j[p] != L',' && j[p] != L'}') ++p;
            continue;
        }

        // Empty values are valid and explicitly mean "no value yet" — we
        // KEEP the entry so save round-trips don't churn the file. Callers
        // treat an empty value the same as a missing entry. Drop only
        // entries with empty KEYS (those are malformed).
        if (key.empty()) continue;
        target[key] = val;
    }
}

// Locate the start of a named object's `{` in a JSON file. Returns
// wstring::npos if the key isn't found or isn't followed by an object.
size_t FindObjectStart(const wstring& j, const wchar_t* keyName) {
    wstring needle = wstring(L"\"") + keyName + L"\"";
    size_t p = j.find(needle);
    if (p == wstring::npos) return wstring::npos;
    p += needle.size();
    SkipWhitespace(j, p);
    if (p >= j.size() || j[p] != L':') return wstring::npos;
    ++p;
    SkipWhitespace(j, p);
    if (p >= j.size() || j[p] != L'{') return wstring::npos;
    return p;
}

} // namespace

void LoadPluginManifest() {
    g_friendlyNames.clear();
    g_readmePaths.clear();

    // AppDir() returns the launcher folder without a trailing
    // backslash, so we add one before the filename or we'd end up
    // reading a non-existent file like "C:\Pathplugin_manifest.json".
    const wstring path = AppDir() + L"\\plugin_manifest.json";
    const wstring json = ReadTextFile(path);
    if (json.empty()) return;                 // file missing / empty

    // "names" (dll → friendly name) — the original v1.x object.
    size_t namesBrace = FindObjectStart(json, L"names");
    if (namesBrace != wstring::npos)
        ParseStringObject(json, namesBrace, g_friendlyNames);

    // "readmes" (dll → readme path) — added v1.6. Absent in old files,
    // which simply produces no readme entries (all lookups return "").
    size_t readmesBrace = FindObjectStart(json, L"readmes");
    if (readmesBrace != wstring::npos)
        ParseStringObject(json, readmesBrace, g_readmePaths);
}

wstring GetPluginFriendlyName(const wstring& dllName) {
    if (g_friendlyNames.empty() || dllName.empty()) return L"";
    auto it = g_friendlyNames.find(dllName);
    if (it == g_friendlyNames.end()) return L"";
    return it->second;       // may be empty string ("no friendly name")
}

void SetPluginFriendlyName(const wstring& dllName, const wstring& friendlyName) {
    if (dllName.empty()) return;
    // operator[] with the case-insensitive comparator updates an
    // existing entry (preserving its stored key casing) rather than
    // creating a duplicate with the caller's casing — exactly what
    // we want for the rename UI: typing in the modal should change
    // the friendly name without disturbing the JSON file's key
    // spelling.
    g_friendlyNames[dllName] = friendlyName;
}

wstring GetPluginReadmePath(const wstring& dllName) {
    if (g_readmePaths.empty() || dllName.empty()) return L"";
    auto it = g_readmePaths.find(dllName);
    if (it == g_readmePaths.end()) return L"";
    return it->second;       // may be empty ("no readme")
}

void SetPluginReadmePath(const wstring& dllName, const wstring& readmePath) {
    if (dllName.empty()) return;
    // Same case-insensitive update semantics as SetPluginFriendlyName.
    // Pass an empty path to clear the readme link (entry kept for
    // round-trip stability, lookup returns "").
    g_readmePaths[dllName] = readmePath;
}

std::vector<std::pair<wstring, wstring>> GetAllPluginReadmes() {
    std::vector<std::pair<wstring, wstring>> out;
    out.reserve(g_readmePaths.size());
    for (const auto& kv : g_readmePaths)
        if (!kv.second.empty()) out.push_back(kv);
    return out;
}


// ─────────────────────────────────────────────────────────────────────
//  Writer + discovery
// ─────────────────────────────────────────────────────────────────────

bool EnsureManifestEntries(const vector<wstring>& dllNames) {
    bool changed = false;
    for (const wstring& name : dllNames) {
        if (name.empty()) continue;
        // find() uses the case-insensitive comparator, so "Foo.dll"
        // finds an existing "FOO.dll" entry — no false-positive insert.
        if (g_friendlyNames.find(name) == g_friendlyNames.end()) {
            g_friendlyNames.emplace(name, wstring());
            changed = true;
        }
    }
    return changed;
}

namespace {

// JSON-escape a string for output. Mirrors the escape table used by
// the parser above so round-tripping load→save is identity-stable for
// any value that didn't have characters outside the basic set.
wstring JsonEscape(const wstring& s) {
    wstring out; out.reserve(s.size() + 2);
    for (wchar_t c : s) {
        switch (c) {
            case L'\\': out += L"\\\\"; break;
            case L'"':  out += L"\\\""; break;
            case L'\n': out += L"\\n";  break;
            case L'\r': out += L"\\r";  break;
            case L'\t': out += L"\\t";  break;
            default:
                // Control chars below 0x20 get escaped; everything
                // else passes through (UTF-8 written by core's
                // WriteTextFile handles non-ASCII naturally).
                if (c < 0x20) {
                    wchar_t buf[8];
                    swprintf(buf, 8, L"\\u%04x", (unsigned)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// Case-insensitive comparator used for stable output ordering. Same
// scheme as the map's hash/equality so the sorted output matches the
// "Foo.dll" / "foo.dll" view-equality semantics consistently.
bool DllLess(const wstring& a, const wstring& b) {
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        wchar_t ca = a[i], cb = b[i];
        if (ca >= L'A' && ca <= L'Z') ca = (wchar_t)(ca - L'A' + L'a');
        if (cb >= L'A' && cb <= L'Z') cb = (wchar_t)(cb - L'A' + L'a');
        if (ca != cb) return ca < cb;
    }
    return a.size() < b.size();
}

} // namespace

void SavePluginManifest() {
    // Collect names into a vector first so we can sort for stable output.
    std::vector<std::pair<wstring, wstring>> entries;
    entries.reserve(g_friendlyNames.size());
    for (const auto& kv : g_friendlyNames) entries.push_back(kv);
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return DllLess(a.first, b.first); });

    // Readme paths: only emit non-empty ones, sorted. If none exist, the
    // "readmes" object is omitted entirely so files that never touched the
    // drag-drop feature keep their original single-object shape.
    std::vector<std::pair<wstring, wstring>> readmes;
    for (const auto& kv : g_readmePaths)
        if (!kv.second.empty()) readmes.push_back(kv);
    std::sort(readmes.begin(), readmes.end(),
              [](const auto& a, const auto& b) { return DllLess(a.first, b.first); });

    // Pretty-print: top-level object holding "names" and (optionally)
    // "readmes", one entry per line, two-space indent. Trailing commas
    // omitted for strict-parser compatibility.
    wstring out;
    out += L"{\n";

    out += L"  \"names\": {";
    for (size_t i = 0; i < entries.size(); ++i) {
        out += L"\n    \"";
        out += JsonEscape(entries[i].first);
        out += L"\": \"";
        out += JsonEscape(entries[i].second);
        out += L"\"";
        if (i + 1 < entries.size()) out += L",";
    }
    if (!entries.empty()) out += L"\n  ";
    out += L"}";

    if (!readmes.empty()) {
        out += L",\n  \"readmes\": {";
        for (size_t i = 0; i < readmes.size(); ++i) {
            out += L"\n    \"";
            out += JsonEscape(readmes[i].first);
            out += L"\": \"";
            out += JsonEscape(readmes[i].second);
            out += L"\"";
            if (i + 1 < readmes.size()) out += L",";
        }
        out += L"\n  }";
    }

    out += L"\n}\n";

    // Same path construction as LoadPluginManifest — keep them in sync.
    WriteTextFile(AppDir() + L"\\plugin_manifest.json", out);
}
