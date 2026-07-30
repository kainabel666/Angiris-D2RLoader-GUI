// ═══════════════════════════════════════════════════════════════════════
//  plugin_config.cpp — see plugin_config.h for the public surface
// ═══════════════════════════════════════════════════════════════════════
//
//  Minimal JSON-array-of-strings parser inline. core.h's JsonStr/Int/Bool
//  helpers handle single scalar fields but the codebase doesn't have a
//  general "extract a string array under key X" helper yet — and the
//  manifest schema is small enough that adding one here keeps the
//  surface area minimal. If a future module needs the same primitive,
//  promote this to core.cpp.

#include "plugin_config.h"
#include "core.h"           // ReadTextFile
#include "fs_utils.h"       // ZI_FileExists, ZI_DirExists

namespace {

// Case-insensitive ASCII comparison, sufficient for matching DLL names
// since plugin filenames are always ASCII in practice. Avoids dragging
// in <algorithm> + locale-aware tolower for what's effectively a
// simple equality check.
bool IEqualAscii(const wstring& a, const wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        wchar_t ca = a[i], cb = b[i];
        if (ca >= L'A' && ca <= L'Z') ca = (wchar_t)(ca - L'A' + L'a');
        if (cb >= L'A' && cb <= L'Z') cb = (wchar_t)(cb - L'A' + L'a');
        if (ca != cb) return false;
    }
    return true;
}

// Unescape a JSON string literal body (the bytes between the quotes).
// Mirrors core.cpp's JsonStr unescape table so the manifest accepts the
// same syntax as every other JSON file the launcher consumes.
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

// Find the substring `"key"` in `j` (with surrounding quotes), then
// advance past `key`'s closing quote, skip whitespace + colon + more
// whitespace, and return the index of the opening `[` of the value
// array. Returns wstring::npos if the key isn't found, isn't followed
// by `: [`, or the source ends prematurely.
size_t FindArrayStart(const wstring& j, const wstring& key) {
    wstring needle = L"\"" + key + L"\"";
    size_t p = j.find(needle);
    if (p == wstring::npos) return wstring::npos;
    p += needle.size();
    // Skip whitespace, expect ':'
    while (p < j.size() && (j[p] == L' ' || j[p] == L'\t' ||
                            j[p] == L'\n' || j[p] == L'\r')) ++p;
    if (p >= j.size() || j[p] != L':') return wstring::npos;
    ++p;
    // Skip whitespace, expect '['
    while (p < j.size() && (j[p] == L' ' || j[p] == L'\t' ||
                            j[p] == L'\n' || j[p] == L'\r')) ++p;
    if (p >= j.size() || j[p] != L'[') return wstring::npos;
    return p;
}

// Walk forward from the opening `[` (inclusive), collecting every quoted
// string entry up to the matching `]`. Bare values (numbers, booleans,
// nulls) and nested structures are skipped silently — only "..." entries
// are harvested. Returns the collected strings in source order.
vector<wstring> ExtractStringArray(const wstring& j, size_t openBracket) {
    vector<wstring> out;
    size_t p = openBracket + 1;          // skip the `[`
    while (p < j.size()) {
        // Skip whitespace + commas between entries
        while (p < j.size() && (j[p] == L' ' || j[p] == L'\t' ||
                                j[p] == L'\n' || j[p] == L'\r' ||
                                j[p] == L',')) ++p;
        if (p >= j.size()) break;
        if (j[p] == L']') break;          // end of array

        if (j[p] == L'"') {
            // Find matching closing quote, honoring backslash escapes
            size_t start = p + 1;
            size_t end = start;
            while (end < j.size() && !(j[end] == L'"' && j[end - 1] != L'\\')) ++end;
            if (end >= j.size()) break;   // unterminated string, bail
            out.push_back(UnescapeJsonStringBody(j.substr(start, end - start)));
            p = end + 1;
        } else {
            // Skip non-string token (number/bool/null/whatever) up to the
            // next comma or closing bracket — we don't accept it but we
            // mustn't get stuck.
            while (p < j.size() && j[p] != L',' && j[p] != L']') ++p;
        }
    }
    return out;
}

// Drop entries that are empty, that don't end in .dll (case-insensitive),
// or that duplicate a previous entry (case-insensitive). Preserves the
// first occurrence so manifest ordering is stable.
vector<wstring> SanitizeEntries(const vector<wstring>& raw) {
    vector<wstring> out;
    out.reserve(raw.size());
    for (const wstring& s : raw) {
        if (s.empty()) continue;
        // Suffix check: ".dll" at the end, case-insensitive
        if (s.size() < 4) continue;
        wstring suffix = s.substr(s.size() - 4);
        if (!IEqualAscii(suffix, L".dll")) continue;
        // De-dup
        bool seen = false;
        for (const wstring& existing : out) {
            if (IEqualAscii(existing, s)) { seen = true; break; }
        }
        if (seen) continue;
        out.push_back(s);
    }
    return out;
}

} // namespace

PluginConfig LoadPluginConfig(const wstring& modDir) {
    PluginConfig mf;   // present=false, empty plugins

    // Build the manifest path. modDir may or may not have a trailing
    // backslash — handle both so callers don't have to normalize.
    wstring path = modDir;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
        path += L'\\';
    }
    path += L"plugin_config.json";

    wstring json = ReadTextFile(path);
    if (json.empty()) return mf;          // file missing or empty → legacy mode

    size_t arrStart = FindArrayStart(json, L"plugins");
    if (arrStart == wstring::npos) return mf;   // no "plugins" key or not an array

    // File found AND has a recognizable "plugins": [ ... ] structure →
    // manifest is "present" even if the array turns out empty after
    // sanitation. An empty manifest means "this mod ships no plugins" —
    // a valid state we want to honor (suppress globals, show empty list).
    mf.present = true;
    mf.plugins = SanitizeEntries(ExtractStringArray(json, arrStart));

    // Optional "author" scalar (v1.6) — used in the allowlist-rejection
    // popup ("<author> has not authorized …"). Absent → empty, and the
    // popup uses its no-author phrasing. Minimal inline scalar parse: find
    // "author", expect a colon then a quoted string.
    {
        size_t p = json.find(L"\"author\"");
        if (p != wstring::npos) {
            p += 8;                                  // past "author"
            while (p < json.size() &&
                   (json[p]==L' '||json[p]==L'\t'||json[p]==L'\n'||json[p]==L'\r'))
                ++p;
            if (p < json.size() && json[p] == L':') {
                ++p;
                while (p < json.size() &&
                       (json[p]==L' '||json[p]==L'\t'||json[p]==L'\n'||json[p]==L'\r'))
                    ++p;
                if (p < json.size() && json[p] == L'"') {
                    ++p;
                    wstring val;
                    while (p < json.size() && json[p] != L'"') {
                        if (json[p] == L'\\' && p + 1 < json.size()) { ++p; }
                        val += json[p++];
                    }
                    mf.author = val;
                }
            }
        }
    }
    return mf;
}


// ─────────────────────────────────────────────────────────────────────
//  Recovery sweep implementation
// ─────────────────────────────────────────────────────────────────────

namespace {

// Local CreateDirectoryW wrapper. Returns true if the directory exists
// after the call — either because it already existed or because we
// just created it. Mirrors plugin_manager.cpp's static EnsureDirExists
// (kept private there); duplicated here to avoid promoting an internal
// helper across module boundaries for one caller.
bool MakeDir(const wstring& path) {
    if (ZI_DirExists(path)) return true;
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

// Build "<folder>\<name>" without worrying about double backslashes
// if the caller passed a trailing separator.
wstring JoinPath(const wstring& folder, const wstring& name) {
    if (folder.empty()) return name;
    wchar_t last = folder.back();
    if (last == L'\\' || last == L'/') return folder + name;
    return folder + L'\\' + name;
}

// Move src → dst with REPLACE_EXISTING + COPY_ALLOWED. Returns true if
// after the call, dst exists. We re-verify with ZI_FileExists rather
// than trusting MoveFileExW's return value alone — same-volume rename
// is atomic but cross-volume copy+delete can succeed at MoveFileExW's
// API level while leaving an inconsistent state if the destination
// drive runs out of room mid-copy.
bool MovePluginFile(const wstring& src, const wstring& dst) {
    if (!MoveFileExW(src.c_str(), dst.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        return false;
    }
    return ZI_FileExists(dst);
}

// Copy src → dst with overwrite. Used to recover plugins from the
// global folders — globals are shared between mods, so we leave them
// in place so the next mod's recovery sweep also finds them. The
// second parameter to CopyFileW is FALSE for "fail if exists" — we
// pass FALSE to mean "overwrite" (Win32's negated convention).
bool CopyPluginFile(const wstring& src, const wstring& dst) {
    if (!CopyFileW(src.c_str(), dst.c_str(), /*bFailIfExists=*/FALSE)) {
        return false;
    }
    return ZI_FileExists(dst);
}

} // namespace

// Case-insensitive check for a .dll extension. Anything else (including
// files with no extension) routes to the patches folder — .json is the
// only other type D2RLoader recognises so this is a safe default.
static bool IsDllName(const wstring& name) {
    if (name.size() < 4) return false;
    wchar_t a = name[name.size() - 4];
    wchar_t b = name[name.size() - 3];
    wchar_t c = name[name.size() - 2];
    wchar_t d = name[name.size() - 1];
    auto lower = [](wchar_t ch) -> wchar_t {
        return (ch >= L'A' && ch <= L'Z') ? (wchar_t)(ch - L'A' + L'a') : ch;
    };
    return a == L'.' && lower(b) == L'd' && lower(c) == L'l' && lower(d) == L'l';
}

vector<bool> RunPluginRecoverySweep(const wstring& modD2rLoaderDir,
                                    const wstring& d2rPath,
                                    const vector<wstring>& manifestPlugins) {
    vector<bool> found(manifestPlugins.size(), false);
    if (manifestPlugins.empty()) return found;

    // Precompute the four "kind base" folder pairs (mod / global × plugins /
    // patches). Each recovery decision picks the pair matching the entry's
    // extension. All destination folders are created lazily on first hit.
    const wstring modPluginsDir     = modD2rLoaderDir + L"\\plugins";
    const wstring modPatchesDir     = modD2rLoaderDir + L"\\patches";
    const wstring modPluginsDisabled = modPluginsDir + L"\\Disabled";
    const wstring modPatchesDisabled = modPatchesDir + L"\\Disabled";

    const wstring globalBase          = d2rPath + L"\\d2rloader";
    const wstring globalPluginsActive = globalBase + L"\\plugins";
    const wstring globalPatchesActive = globalBase + L"\\patches";
    const wstring globalPluginsDis    = globalPluginsActive + L"\\Disabled";
    const wstring globalPatchesDis    = globalPatchesActive + L"\\Disabled";

    // Per-candidate source-handling policy: globals are shared property
    // (other mods may depend on them, so we copy and leave originals in
    // place), mod-disabled is the mod's own state (so we move and clean
    // up the disabled copy). The boolean flag pairs with each candidate
    // path below.
    struct Candidate { wstring path; bool moveNotCopy; };

    for (size_t i = 0; i < manifestPlugins.size(); ++i) {
        const wstring& name = manifestPlugins[i];
        bool isDll = IsDllName(name);

        // Route this entry to plugins\ or patches\ based on extension.
        const wstring& destDir      = isDll ? modPluginsDir      : modPatchesDir;
        const wstring& modDisabled  = isDll ? modPluginsDisabled : modPatchesDisabled;
        const wstring& globActive   = isDll ? globalPluginsActive : globalPatchesActive;
        const wstring& globDisabled = isDll ? globalPluginsDis    : globalPatchesDis;

        // (0) Already in the destination — nothing to do.
        const wstring destPath = JoinPath(destDir, name);
        if (ZI_FileExists(destPath)) {
            found[i] = true;
            continue;
        }

        // Ensure the destination folder exists before the copy/move.
        MakeDir(destDir);

        // Search the three candidate folders in spec order. First hit
        // is copied (globals) or moved (mod-disabled) into the mod's
        // active folder.
        const Candidate candidates[] = {
            { JoinPath(globActive,   name), /*moveNotCopy=*/false },
            { JoinPath(globDisabled, name), /*moveNotCopy=*/false },
            { JoinPath(modDisabled,  name), /*moveNotCopy=*/true  },
        };
        for (const Candidate& c : candidates) {
            if (!ZI_FileExists(c.path)) continue;
            bool ok = c.moveNotCopy ? MovePluginFile(c.path, destPath)
                                    : CopyPluginFile(c.path, destPath);
            if (ok) found[i] = true;
            break;  // Don't keep searching after the first hit, even
                    // if the copy/move failed — that's a fatal condition
                    // the user needs to resolve manually.
        }
    }

    return found;
}


// ─────────────────────────────────────────────────────────────────────
//  Globals-to-disabled sweep implementation
// ─────────────────────────────────────────────────────────────────────

// Move every matching file in `activeDir` to `activeDir\Disabled`. Used
// once for plugins (*.dll) and once for patches (*.json) inside
// MoveGlobalPluginsToDisabled.
static void SweepDirToDisabled(const wstring& activeDir,
                               const wchar_t* pattern) {
    if (!ZI_DirExists(activeDir)) return;

    const wstring disabledDir = activeDir + L"\\Disabled";
    MakeDir(disabledDir);

    const wstring searchPattern = JoinPath(activeDir, pattern);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(searchPattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        // FindFirstFile's pattern can also pick up directories with a
        // matching-looking name — skip those, even if pathological.
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        const wstring src = JoinPath(activeDir,   fd.cFileName);
        const wstring dst = JoinPath(disabledDir, fd.cFileName);

        // MoveFileExW with REPLACE_EXISTING: if a same-named file is
        // already in Disabled\ (user disabled it before, then a newer
        // version came in via update), the newer one wins.
        MoveFileExW(src.c_str(), dst.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
        // Silent on failure — see header doc.
    } while (FindNextFileW(h, &fd));

    FindClose(h);
}

void MoveGlobalPluginsToDisabled(const wstring& d2rPath) {
    // Sweep both file types independently. Each folder-pair may be
    // absent (D2R install without any global plugins/patches ever
    // configured); SweepDirToDisabled no-ops in that case.
    const wstring base = d2rPath + L"\\d2rloader";
    SweepDirToDisabled(base + L"\\plugins", L"*.dll");
    SweepDirToDisabled(base + L"\\patches", L"*.json");
}
