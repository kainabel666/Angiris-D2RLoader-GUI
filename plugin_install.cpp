// ═══════════════════════════════════════════════════════════════════════
//  plugin_install.cpp — drag-drop plugin/patch zip routing core (v1.6)
//  See plugin_install.h for the flow and public surface.
// ═══════════════════════════════════════════════════════════════════════

#include "plugin_install.h"
#include "core.h"             // ReadTextFile, AppDir
#include "fs_utils.h"         // RunTarExtract, MakeTempInstallDir, DeleteFolderRecursive,
                              // ZI_DirExists, ZI_FileExists
#include "plugin_manifest.h"  // SetPluginReadmePath, SavePluginManifest, LoadPluginManifest,
                              // GetPluginFriendlyName (collision check via known dlls)

#include <algorithm>

using std::wstring;
using std::vector;

// ─────────────────────────────────────────────────────────────────────
//  Small shared helpers
// ─────────────────────────────────────────────────────────────────────

namespace {

// Windows-illegal path characters plus control chars, trailing space/dot.
// Same rule as zip_install.cpp's SanitizeModName (kept local so this
// module has no dependency on that file's private static). Replaces bad
// chars with nothing; a name that sanitizes to empty is caller-rejected.
wstring SanitizeName(const wstring& name) {
    static const wchar_t* bad = L"<>:\"/\\|?*";
    wstring out; out.reserve(name.size());
    for (wchar_t c : name) {
        if (c < 0x20) continue;
        bool isBad = false;
        for (const wchar_t* p = bad; *p; ++p) if (*p == c) { isBad = true; break; }
        if (!isBad) out.push_back(c);
    }
    while (!out.empty() && (out.back() == L' ' || out.back() == L'.'))
        out.pop_back();
    return out;
}

// Lowercased file extension including the dot (".dll"), or empty.
wstring ExtLower(const wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    size_t dot   = path.find_last_of(L'.');
    if (dot == wstring::npos) return L"";
    if (slash != wstring::npos && dot < slash) return L"";
    wstring e = path.substr(dot);
    for (wchar_t& c : e) if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
    return e;
}

// Basename (filename with extension) from a path.
wstring BaseName(const wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return (slash == wstring::npos) ? path : path.substr(slash + 1);
}

// Filename without extension.
wstring StemOf(const wstring& path) {
    wstring b = BaseName(path);
    size_t dot = b.find_last_of(L'.');
    return (dot == wstring::npos) ? b : b.substr(0, dot);
}

// Map a manifest dest string to the enum. Case-insensitive.
PluginDest DestFromString(const wstring& s) {
    wstring d = s;
    for (wchar_t& c : d) if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
    if (d == L"plugins") return PluginDest::Plugins;
    if (d == L"config")  return PluginDest::Config;
    if (d == L"patches") return PluginDest::Patches;
    if (d == L"readme")  return PluginDest::Readme;
    if (d == L"excel")   return PluginDest::Excel;
    return PluginDest::Unknown;
}

// ── destPath sandbox ─────────────────────────────────────────────────
// A destPath is an author-supplied LITERAL path. Because a future version
// installs zips downloaded from a repository, an unchecked literal path is
// an arbitrary-file-write primitive — a malicious manifest could target a
// Windows autostart folder or overwrite system files. So every destPath is
// validated to stay UNDER the D2R install root:
//   - must be relative (no drive letter "C:", no UNC "\\", no leading slash)
//   - no ".." component anywhere (blocks climbing out of the root)
//   - not empty after normalization
// Returns the normalized path (backslash-separated, no leading slash) on
// success, or empty on rejection. The {mod} token is left intact here and
// substituted later.
wstring ValidateDestPath(const wstring& raw) {
    if (raw.empty()) return L"";

    // Normalize separators to backslash for component analysis.
    wstring p; p.reserve(raw.size());
    for (wchar_t c : raw) p.push_back(c == L'/' ? L'\\' : c);

    // Reject drive letters ("C:\...") and device/UNC prefixes ("\\...").
    if (p.size() >= 2 && p[1] == L':') return L"";
    if (p.size() >= 2 && p[0] == L'\\' && p[1] == L'\\') return L"";

    // Strip a single leading slash (so "/mods/x" → "mods/x"); a path that
    // was ONLY slashes normalizes to empty and is rejected below.
    while (!p.empty() && p[0] == L'\\') p.erase(p.begin());

    // Split into components; reject any ".." (or "." kept but harmless).
    // Also reject empty components from doubled slashes by skipping them.
    wstring out;
    size_t i = 0;
    while (i < p.size()) {
        size_t j = p.find(L'\\', i);
        if (j == wstring::npos) j = p.size();
        wstring comp = p.substr(i, j - i);
        i = j + 1;
        if (comp.empty() || comp == L".") continue;      // skip . and //
        if (comp == L"..") return L"";                    // traversal — reject
        // Illegal filename chars in a component (except the {mod} token,
        // whose braces are fine as a literal marker we replace later).
        for (wchar_t c : comp) {
            if (c < 0x20) return L"";
            if (c == L':' || c == L'*' || c == L'?' ||
                c == L'"' || c == L'<' || c == L'>' || c == L'|')
                return L"";
        }
        if (!out.empty()) out += L"\\";
        out += comp;
    }
    return out;   // empty ⇒ was all slashes/dots ⇒ caller rejects
}

// Does a (validated) destPath contain the {mod} token?
bool DestPathNeedsMod(const wstring& p) {
    return p.find(L"{mod}") != wstring::npos;
}

// Substitute {mod} with a sanitized mod folder name.
wstring SubstituteMod(const wstring& tmpl, const wstring& modName) {
    wstring mod = SanitizeName(modName);
    wstring out = tmpl;
    size_t pos;
    while ((pos = out.find(L"{mod}")) != wstring::npos)
        out.replace(pos, 5, mod);
    return out;
}

// The d2rloader base for a given scope. Global → <d2r>\d2rloader.
// Mod → <d2r>\mods\<mod>\d2rloader.
wstring D2rLoaderBase(const wstring& d2rPath, InstallScope scope,
                      const wstring& selectedMod) {
    if (scope == InstallScope::Mod)
        return d2rPath + L"\\mods\\" + selectedMod + L"\\d2rloader";
    return d2rPath + L"\\d2rloader";
}

// Folder for a non-excel dest under a scope base. Readme resolves to the
// per-plugin subfolder; caller appends the <DLLName>-Readme filename.
wstring FolderForDest(PluginDest dest, const wstring& base,
                      const wstring& pluginName) {
    switch (dest) {
        case PluginDest::Plugins: return base + L"\\plugins";
        case PluginDest::Config:  return base + L"\\config";
        case PluginDest::Patches: return base + L"\\patches";
        case PluginDest::Readme:  return base + L"\\readmes\\" + pluginName;
        default:                  return L"";   // Excel/Unknown handled elsewhere
    }
}

} // namespace


// ─────────────────────────────────────────────────────────────────────
//  plugin_info.json — minimal hand-rolled parse (mirrors plugin_manifest)
// ─────────────────────────────────────────────────────────────────────
//
//  Schema:
//    { "name": "...", "type": "plugin"|"patch",
//      "files": [ { "path": "...", "dest": "..." }, ... ] }
//
//  We need: name (string), and files[] (array of {path,dest}). type is a
//  label (not enforced). Reuses the same scalar-string extraction the
//  rest of the launcher uses.

namespace {

void SkipWs(const wstring& j, size_t& p) {
    while (p < j.size() && (j[p]==L' '||j[p]==L'\t'||j[p]==L'\n'||j[p]==L'\r')) ++p;
}

wstring Unescape(const wstring& raw) {
    wstring out; out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == L'\\' && i + 1 < raw.size()) {
            switch (raw[i+1]) {
                case L'\\': out+=L'\\'; ++i; break;
                case L'"':  out+=L'"';  ++i; break;
                case L'/':  out+=L'/';  ++i; break;
                case L'n':  out+=L'\n'; ++i; break;
                case L't':  out+=L'\t'; ++i; break;
                case L'r':  out+=L'\r'; ++i; break;
                default:    out+=raw[i]; break;
            }
        } else out += raw[i];
    }
    return out;
}

bool ParseStr(const wstring& j, size_t& p, wstring& out) {
    if (p >= j.size() || j[p] != L'"') return false;
    size_t start = p + 1, end = start;
    while (end < j.size() && !(j[end]==L'"' && j[end-1]!=L'\\')) ++end;
    if (end >= j.size()) return false;
    out = Unescape(j.substr(start, end - start));
    p = end + 1;
    return true;
}

// Find a top-level string value for `key`. Cheap: locate "key", expect
// ':' then a string. Good enough for name/type at the object root.
bool FindTopString(const wstring& j, const wchar_t* key, wstring& out) {
    wstring needle = wstring(L"\"") + key + L"\"";
    size_t p = j.find(needle);
    if (p == wstring::npos) return false;
    p += needle.size();
    SkipWs(j, p);
    if (p >= j.size() || j[p] != L':') return false;
    ++p; SkipWs(j, p);
    return ParseStr(j, p, out);
}

struct RawFileEntry { wstring path; wstring dest; wstring destPath; };

// Parse the "files" array of {path,dest} objects. Tolerant: skips
// malformed entries rather than failing the whole parse.
bool ParseFilesArray(const wstring& j, vector<RawFileEntry>& out) {
    const wstring needle = L"\"files\"";
    size_t p = j.find(needle);
    if (p == wstring::npos) return false;
    p += needle.size();
    SkipWs(j, p);
    if (p >= j.size() || j[p] != L':') return false;
    ++p; SkipWs(j, p);
    if (p >= j.size() || j[p] != L'[') return false;
    ++p;                                   // into the array

    while (p < j.size()) {
        SkipWs(j, p);
        if (p >= j.size()) break;
        if (j[p] == L']') break;           // end of array
        if (j[p] == L',') { ++p; continue; }
        if (j[p] != L'{') { ++p; continue; } // resync

        // Parse one object: collect "path" and "dest".
        size_t objEnd = j.find(L'}', p);
        if (objEnd == wstring::npos) break;
        wstring obj = j.substr(p, objEnd - p + 1);
        RawFileEntry e;
        FindTopString(obj, L"path", e.path);
        FindTopString(obj, L"dest", e.dest);
        FindTopString(obj, L"destPath", e.destPath);
        if (!e.path.empty()) out.push_back(e);
        p = objEnd + 1;
    }
    return true;
}

// Locate plugin_info.json in the extracted tree (root, or one level deep —
// zips often nest a single folder). Shallowest wins. Empty if none.
wstring FindPluginInfo(const wstring& root) {
    // Check root first.
    wstring atRoot = root + L"\\plugin_info.json";
    if (ZI_FileExists(atRoot)) return atRoot;

    // One level deep.
    WIN32_FIND_DATAW fd;
    wstring pat = root + L"\\*";
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";
    wstring found;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        wstring cand = root + L"\\" + fd.cFileName + L"\\plugin_info.json";
        if (ZI_FileExists(cand)) { found = cand; break; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────
//  Filesystem helpers (local)
// ─────────────────────────────────────────────────────────────────────

namespace {

// Create a directory and all missing parents. Idempotent.
bool CreateDirTree(const wstring& path) {
    if (path.empty()) return false;
    if (ZI_DirExists(path)) return true;
    // Recurse into the parent first.
    size_t slash = path.find_last_of(L"\\/");
    if (slash != wstring::npos && slash > 2) {
        wstring parent = path.substr(0, slash);
        if (!ZI_DirExists(parent)) CreateDirTree(parent);
    }
    return CreateDirectoryW(path.c_str(), nullptr) ||
           GetLastError() == ERROR_ALREADY_EXISTS;
}

// Copy one file, creating the destination's parent folders. Overwrites.
bool CopyOneFile(const wstring& src, const wstring& dst) {
    size_t slash = dst.find_last_of(L"\\/");
    if (slash != wstring::npos) CreateDirTree(dst.substr(0, slash));
    return CopyFileW(src.c_str(), dst.c_str(), FALSE) != 0;
}

// Before overwriting a config file, preserve the existing one as a
// "<name>.old" sibling so the user can copy their previous settings back.
// Only backs up when `dst` actually exists (a real overwrite). A prior
// ".old" is replaced (one generation kept — the most recent pre-overwrite
// state). Best-effort: a failed backup does NOT block the install, since
// the user explicitly chose to overwrite.
void BackupConfigIfExists(const wstring& dst) {
    if (!ZI_FileExists(dst)) return;              // fresh install — nothing to save
    wstring backup = dst + L".old";
    // MoveFileEx replaces any existing .old atomically where possible.
    MoveFileExW(dst.c_str(), backup.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
}

// Should this op's destination be backed up to .old before overwrite?
// User-editable data/settings — TOML, JSON (plugin config + patches), and
// excel TXT — qualify. Readmes (also .txt) and DLLs do NOT: they're
// replaceable artifacts, not user data. Extension-driven, with an explicit
// readme exclusion since a readme shares the .txt extension.
bool IsBackupWorthy(const PluginFileOp& op) {
    if (op.isReadme) return false;                // readmes aren't user data
    if (op.isDll)    return false;                // DLLs are replaceable
    wstring ext;
    size_t dot = op.destAbsPath.find_last_of(L'.');
    if (dot != wstring::npos) {
        ext = op.destAbsPath.substr(dot);
        for (wchar_t& c : ext) if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
    }
    return ext == L".toml" || ext == L".json" || ext == L".txt";
}

// Does the extracted tree contain a file matching `leaf` (basename)?
// Returns the absolute path within `root`, searching root then one level
// deep — the same shallow layout FindPluginInfo handles.
wstring FindExtractedFile(const wstring& root, const wstring& relPath) {
    // relPath may include subfolders (manifest paths are zip-relative).
    wstring direct = root + L"\\" + relPath;
    if (ZI_FileExists(direct)) return direct;
    // Try one level deep (single nested folder).
    WIN32_FIND_DATAW fd;
    wstring pat = root + L"\\*";
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";
    wstring found;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        wstring cand = root + L"\\" + fd.cFileName + L"\\" + relPath;
        if (ZI_FileExists(cand)) { found = cand; break; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

} // namespace


// ─────────────────────────────────────────────────────────────────────
//  InspectPluginZip
// ─────────────────────────────────────────────────────────────────────

PluginInstallPlan InspectPluginZip(const wstring& zipPath,
                                   const wstring& d2rPath,
                                   InstallScope scope,
                                   const wstring& selectedMod) {
    PluginInstallPlan plan;
    plan.scope = scope;

    // 1. Extract to temp.
    wstring tmp = MakeTempInstallDir();
    if (tmp.empty()) { plan.error = L"Could not create a temporary folder."; return plan; }
    if (!RunTarExtract(zipPath, tmp)) {
        DeleteFolderRecursive(tmp);
        plan.error = L"Could not extract the archive.";
        return plan;
    }
    plan.tempDir = tmp;

    // Resolve the plugins folder for this scope now — used by the
    // no-manifest path (dump target) and referenced by the manifest path.
    plan.pluginsDir = D2rLoaderBase(d2rPath, scope, selectedMod) + L"\\plugins";

    // 2. Manifest?
    wstring infoPath = FindPluginInfo(tmp);
    if (infoPath.empty()) {
        // No-manifest path: caller will show the notice, then call
        // ExecuteNoManifest. Mark ok so the caller proceeds.
        plan.ok = true;
        plan.hasManifest = false;
        return plan;
    }
    plan.hasManifest = true;

    wstring infoJson = ReadTextFile(infoPath);
    if (infoJson.empty()) {
        DeleteFolderRecursive(tmp);
        plan.error = L"plugin_info.json is empty or unreadable.";
        return plan;
    }

    // The extract root the manifest paths are relative to = the folder
    // holding plugin_info.json.
    size_t slash = infoPath.find_last_of(L"\\/");
    wstring manifestRoot = (slash == wstring::npos) ? tmp : infoPath.substr(0, slash);

    // 3. name (sanitized). Required — it's the readme folder + display.
    wstring rawName;
    FindTopString(infoJson, L"name", rawName);
    plan.pluginName = SanitizeName(rawName);
    if (plan.pluginName.empty()) {
        DeleteFolderRecursive(tmp);
        plan.error = L"plugin_info.json is missing a valid \"name\".";
        return plan;
    }

    // 4. files[].
    vector<RawFileEntry> raw;
    ParseFilesArray(infoJson, raw);
    if (raw.empty()) {
        DeleteFolderRecursive(tmp);
        plan.error = L"plugin_info.json lists no files.";
        return plan;
    }

    const wstring base = D2rLoaderBase(d2rPath, scope, selectedMod);
    // Readmes ALWAYS land in the global store, regardless of drop scope, so
    // there's one place readmes live and the single global plugin_manifest
    // (which records readme paths) always resolves them. Only plugin/config/
    // patch/excel files follow the per-scope base above.
    const wstring globalBase = D2rLoaderBase(d2rPath, InstallScope::Global, L"");

    // First pass: find the primary DLL (collision key) and note config.
    for (const RawFileEntry& e : raw) {
        wstring ext = ExtLower(e.path);
        if (ext == L".dll" && plan.dllName.empty())
            plan.dllName = BaseName(e.path);
    }

    // 5. Resolve each file to a destination op.
    for (const RawFileEntry& e : raw) {
        PluginFileOp op;
        op.dest = DestFromString(e.dest);
        op.srcTempPath = FindExtractedFile(manifestRoot, e.path);
        if (op.srcTempPath.empty()) continue;   // listed but not present — skip

        wstring ext = ExtLower(e.path);
        op.isDll    = (ext == L".dll");

        // destPath OVERRIDES dest (per design). A validated literal path
        // from the D2R root, sandboxed by ValidateDestPath. Rejected paths
        // (traversal, absolute, drive) skip the file rather than falling
        // back to dest — a bad destPath is an authoring error to surface,
        // not silently reroute.
        if (!e.destPath.empty()) {
            wstring validated = ValidateDestPath(e.destPath);
            if (validated.empty()) continue;    // unsafe/invalid — skip
            op.dest = PluginDest::Literal;
            op.literalTemplate = validated;
            if (DestPathNeedsMod(validated)) {
                op.needsMod = true;
                plan.needsModPicker = true;
                // destAbsPath resolved after the mod picker (see
                // ResolveModDependentPaths). Left empty for now.
            } else {
                // Resolve immediately against the D2R root.
                op.destAbsPath = d2rPath + L"\\" + validated;
            }
            plan.files.push_back(op);
            continue;                            // destPath handled — skip switch
        }

        op.isConfig = (op.dest == PluginDest::Config);

        switch (op.dest) {
            case PluginDest::Plugins:
            case PluginDest::Config:
            case PluginDest::Patches: {
                wstring folder = FolderForDest(op.dest, base, plan.pluginName);
                wstring leaf   = BaseName(e.path);
                // "Replace in place": if this file already exists ONLY in the
                // Disabled\ subfolder (the plugin/patch is installed but
                // toggled off), overwrite it there rather than writing a
                // fresh active copy — a reinstall shouldn't silently
                // re-enable something the user disabled. Active copy (if
                // present) always wins as the target.
                wstring activePath   = folder + L"\\" + leaf;
                wstring disabledPath = folder + L"\\Disabled\\" + leaf;
                if (!ZI_FileExists(activePath) && ZI_FileExists(disabledPath))
                    op.destAbsPath = disabledPath;   // replace in place, stay disabled
                else
                    op.destAbsPath = activePath;
                break;
            }
            case PluginDest::Readme: {
                op.isReadme = true;
                // Rename to <DLLName>-Readme.<ext>. If no DLL yet known,
                // fall back to the plugin name.
                wstring stem = plan.dllName.empty()
                                 ? plan.pluginName
                                 : StemOf(plan.dllName);
                wstring rext = ExtLower(e.path);
                if (rext.empty()) rext = L".txt";
                // Global store (globalBase), not the per-scope base — a
                // mod-scoped drop still puts its readme in the global
                // readmes folder so all readmes live in one place.
                wstring folder = FolderForDest(PluginDest::Readme, globalBase, plan.pluginName);
                op.destAbsPath = folder + L"\\" + stem + L"-Readme" + rext;
                break;
            }
            case PluginDest::Excel: {
                op.isExcel = true;
                plan.excelFilesPresent = true;
                plan.needsModPicker = true;
                // destAbsPath resolved later by ResolveExcelTargetMod
                // (needs the chosen mod). Left empty for now.
                break;
            }
            default:
                continue;   // Unknown dest — skip silently
        }
        if (op.isConfig) plan.hasConfig = true;
        plan.files.push_back(op);
    }

    if (plan.files.empty() && !plan.needsModPicker) {
        DeleteFolderRecursive(tmp);
        plan.error = L"None of the listed files were found in the archive.";
        return plan;
    }

    // 6. Collision: is a plugin with this DLL already installed at scope?
    //    A plugin can live in EITHER the active plugins folder OR its
    //    Disabled\ subfolder (the plugin manager toggles between them, and
    //    treats both as "installed"). Check both — otherwise dropping a zip
    //    for a currently-DISABLED plugin would miss the collision and leave
    //    two copies (one active, one disabled).
    if (!plan.dllName.empty()) {
        wstring pluginsFolder = FolderForDest(PluginDest::Plugins, base, plan.pluginName);
        wstring activeDll   = pluginsFolder + L"\\" + plan.dllName;
        wstring disabledDll = pluginsFolder + L"\\Disabled\\" + plan.dllName;
        if (ZI_FileExists(activeDll) || ZI_FileExists(disabledDll))
            plan.collision = true;
    }

    plan.ok = true;
    return plan;
}

// ─────────────────────────────────────────────────────────────────────
//  ResolveExcelTargetMod — the "which mod?" answer, plus the .mpq fork
// ─────────────────────────────────────────────────────────────────────

bool ResolveExcelTargetMod(PluginInstallPlan& plan,
                           const wstring& d2rPath,
                           const wstring& modName) {
    wstring mod = SanitizeName(modName);
    if (mod.empty()) return false;

    wstring modFolder  = d2rPath + L"\\mods\\" + mod;
    wstring mpqFolder  = modFolder + L"\\" + mod + L".mpq";      // unpacked (dir)
    wstring mpqFile    = mpqFolder;                              // packed (file, same name)

    // Unpacked .mpq FOLDER present → writable excel path.
    bool unpacked = ZI_DirExists(mpqFolder);
    // Packed .mpq FILE present (and no folder) → cannot write inside.
    bool packed   = !unpacked && ZI_FileExists(mpqFile);

    wstring excelDir;
    if (unpacked) {
        excelDir = mpqFolder + L"\\data\\global\\excel";
    } else if (packed) {
        // Fallback: drop the TXT at mods/<mod>/ and signal the caller to
        // show the "encrypted MPQ" popup.
        excelDir = modFolder;
    } else {
        // Neither exists — create the unpacked excel chain (normal case
        // where the mod exists but has no excel yet).
        excelDir = mpqFolder + L"\\data\\global\\excel";
        unpacked = true;   // treat as writable; CreateDirTree makes it
    }

    for (PluginFileOp& op : plan.files) {
        if (op.isExcel) {
            op.destAbsPath = excelDir + L"\\" + BaseName(op.srcTempPath);
        } else if (op.needsMod && op.dest == PluginDest::Literal) {
            // Literal destPath containing {mod} — substitute the chosen mod
            // and resolve against the D2R root. Sandbox was already applied
            // in ValidateDestPath; SubstituteMod sanitizes the mod name.
            wstring resolved = SubstituteMod(op.literalTemplate, mod);
            op.destAbsPath = d2rPath + L"\\" + resolved;
        }
    }
    return unpacked;   // false ⇒ packed ⇒ caller shows the encrypted-MPQ popup
}


// ─────────────────────────────────────────────────────────────────────
//  Executors
// ─────────────────────────────────────────────────────────────────────

namespace {

// Should this op be written given the overwrite choice? On a fresh
// install (All) everything writes. On a collision, the category chosen
// decides. Readme always rides along with ANY write (per design), so it
// writes unless the whole thing is cancelled.
bool ShouldWrite(const PluginFileOp& op, OverwriteChoice choice) {
    // Literal destPath files are extra data the plugin needs at a specific
    // location; like readme/excel they ride along with ANY write rather
    // than being gated by the DLL/Config collision categories.
    bool ridesAlong = op.isReadme || op.isExcel ||
                      (op.dest == PluginDest::Literal);
    switch (choice) {
        case OverwriteChoice::Cancel:       return false;
        case OverwriteChoice::All:          return true;
        case OverwriteChoice::DllAndConfig: return true;
        case OverwriteChoice::DllOnly:
            return op.isDll || ridesAlong;
        case OverwriteChoice::ConfigOnly:
            return op.isConfig || ridesAlong;
    }
    return false;
}

} // namespace

bool ExecutePluginPlan(PluginInstallPlan& plan, OverwriteChoice choice) {
    if (choice == OverwriteChoice::Cancel) {
        DiscardPluginPlan(plan);
        return false;
    }

    bool allOk = true;
    wstring readmeRel;   // launcher-relative readme path to record

    for (const PluginFileOp& op : plan.files) {
        if (!ShouldWrite(op, choice)) continue;
        if (op.destAbsPath.empty()) continue;   // unresolved excel w/o a mod
        // Preserve user-editable data/settings as .old before overwriting,
        // so tuned values can be copied back. Covers TOML config, JSON
        // (plugin config AND patches), and excel TXT — anything a user or
        // modder might have customized. DLLs and readmes are excluded:
        // they're replaceable artifacts, not user data.
        if (IsBackupWorthy(op))
            BackupConfigIfExists(op.destAbsPath);
        if (!CopyOneFile(op.srcTempPath, op.destAbsPath)) {
            allOk = false;
            continue;
        }
        // Capture the readme's launcher-relative path for the manifest.
        if (op.isReadme) {
            const wstring appDir = AppDir();
            // Is destAbsPath under appDir? Compare the leading substring
            // case-insensitively (Windows paths), using the codebase's
            // standard _wcsicmp on an extracted prefix.
            bool underApp = false;
            if (op.destAbsPath.size() > appDir.size() + 1) {
                wstring prefix = op.destAbsPath.substr(0, appDir.size());
                if (_wcsicmp(prefix.c_str(), appDir.c_str()) == 0)
                    underApp = true;
            }
            readmeRel = underApp ? op.destAbsPath.substr(appDir.size() + 1)
                                 : op.destAbsPath;   // outside app dir — absolute
        }
    }

    // Record the readme link in plugin_manifest.json (keyed by DLL).
    if (!readmeRel.empty() && !plan.dllName.empty()) {
        SetPluginReadmePath(plan.dllName, readmeRel);
        SavePluginManifest();
    }

    DeleteFolderRecursive(plan.tempDir);
    plan.tempDir.clear();
    return allOk;
}

bool ExecuteNoManifest(PluginInstallPlan& plan) {
    // Dump EVERY extracted file into the plugins folder at the plan's
    // scope. No routing, no readme handling — D2RLoader reads the plugins
    // folder recursively, so a flat copy of the tree is fine.
    if (plan.tempDir.empty() || plan.pluginsDir.empty()) {
        DiscardPluginPlan(plan);
        return false;
    }
    CreateDirTree(plan.pluginsDir);
    bool ok = CopyTreeInto(plan.tempDir, plan.pluginsDir, /*addMissing=*/true);
    DeleteFolderRecursive(plan.tempDir);
    plan.tempDir.clear();
    return ok;
}

void DiscardPluginPlan(PluginInstallPlan& plan) {
    if (!plan.tempDir.empty()) {
        DeleteFolderRecursive(plan.tempDir);
        plan.tempDir.clear();
    }
}

// ─────────────────────────────────────────────────────────────────────
//  Drop orchestration (Step 3)
// ─────────────────────────────────────────────────────────────────────

namespace {

// Is there a file named `leaf` at the root or one level deep in `root`?
bool HasManifestFile(const wstring& root, const wchar_t* leaf) {
    if (ZI_FileExists(root + L"\\" + leaf)) return true;
    WIN32_FIND_DATAW fd;
    wstring pat = root + L"\\*";
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        if (ZI_FileExists(root + L"\\" + fd.cFileName + L"\\" + leaf)) {
            found = true; break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

// Collect absolute paths of every .json file at the root or one level deep
// in `root` (the same shallow layout the rest of the installer assumes).
// Used for patch-bundle zips (no manifest, JSONs → patches folder).
void CollectJsonFiles(const wstring& root, vector<wstring>& out) {
    // Root-level jsons.
    WIN32_FIND_DATAW fd;
    wstring pat = root + L"\\*";
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            wstring name = fd.cFileName;
            size_t dot = name.find_last_of(L'.');
            if (dot != wstring::npos &&
                _wcsicmp(name.substr(dot).c_str(), L".json") == 0)
                out.push_back(root + L"\\" + name);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    // One level deep (single nested folder, as zips often nest).
    h = FindFirstFileW(pat.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;
            wstring sub = root + L"\\" + fd.cFileName;
            WIN32_FIND_DATAW fd2;
            wstring pat2 = sub + L"\\*";
            HANDLE h2 = FindFirstFileW(pat2.c_str(), &fd2);
            if (h2 == INVALID_HANDLE_VALUE) continue;
            do {
                if (fd2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                wstring name = fd2.cFileName;
                size_t dot = name.find_last_of(L'.');
                if (dot != wstring::npos &&
                    _wcsicmp(name.substr(dot).c_str(), L".json") == 0)
                    out.push_back(sub + L"\\" + name);
            } while (FindNextFileW(h2, &fd2));
            FindClose(h2);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

// Does the extracted tree contain at least one .json (root or one deep)?
bool TreeContainsJson(const wstring& root) {
    vector<wstring> js;
    CollectJsonFiles(root, js);
    return !js.empty();
}

} // namespace

// Does the dropped plugin match the mod's allowlist? Match on DLL filename
// OR plugin name (case-insensitive) against any sanctioned entry. A
// no-manifest zip (empty dllName AND pluginName) never matches → rejected
// under an active allowlist (strict: unverifiable = not allowed).
static bool PluginMatchesAllowlist(const PluginAllowlist& allow,
                                   const wstring& dllName,
                                   const wstring& pluginName) {
    for (const wstring& e : allow.entries) {
        if (!dllName.empty()    && _wcsicmp(e.c_str(), dllName.c_str())    == 0)
            return true;
        if (!pluginName.empty() && _wcsicmp(e.c_str(), pluginName.c_str()) == 0)
            return true;
    }
    return false;
}

ZipKind PeekZipKind(const wstring& zipPath) {
    // Cheap extension gate first.
    size_t dot = zipPath.find_last_of(L'.');
    if (dot == wstring::npos ||
        _wcsicmp(zipPath.substr(dot).c_str(), L".zip") != 0)
        return ZipKind::NotAZip;

    wstring tmp = MakeTempInstallDir();
    if (tmp.empty()) return ZipKind::NotAZip;
    if (!RunTarExtract(zipPath, tmp)) {
        DeleteFolderRecursive(tmp);
        return ZipKind::NotAZip;
    }

    ZipKind kind = ZipKind::Bare;
    // Plugin manifest takes precedence if both somehow exist (a plugin
    // zip is the more specific case for this launcher's own format).
    if (HasManifestFile(tmp, L"plugin_info.json")) kind = ZipKind::Plugin;
    else if (HasManifestFile(tmp, L"modinfo.json")) kind = ZipKind::Mod;
    else if (TreeContainsJson(tmp)) kind = ZipKind::PatchBundle;   // no manifest + .json → patches

    DeleteFolderRecursive(tmp);
    return kind;
}

bool HandlePluginDropZip(const wstring& zipPath,
                         const wstring& d2rPath,
                         InstallScope scope,
                         const wstring& selectedMod,
                         const vector<wstring>& mods,
                         const PluginDropCallbacks& cb,
                         const PluginAllowlist& allow) {
    PluginInstallPlan plan = InspectPluginZip(zipPath, d2rPath, scope, selectedMod);

    // Hard failure (extract failed, bad manifest).
    if (!plan.ok) {
        if (cb.errorNotice && !plan.error.empty())
            cb.errorNotice(cb.ctx, plan.error);
        DiscardPluginPlan(plan);
        return false;
    }

    // Allowlist gate (manifest-mode mods): the dropped plugin must be
    // sanctioned by the mod, else reject with the "not authorized" popup.
    // A no-manifest zip (no plugin_info.json) can't be verified, so under
    // an active allowlist it's rejected too (strict). Checked BEFORE any
    // notice/prompt and before install — nothing is written.
    if (allow.active) {
        bool matched = plan.hasManifest &&
                       PluginMatchesAllowlist(allow, plan.dllName, plan.pluginName);
        if (!matched) {
            if (cb.notAuthorized)
                cb.notAuthorized(cb.ctx, plan.pluginName, allow.modName, allow.modAuthor);
            DiscardPluginPlan(plan);
            return false;
        }
    }

    // No-manifest path: notice, then dump to plugins.
    if (!plan.hasManifest) {
        if (cb.noManifestNotice) cb.noManifestNotice(cb.ctx);
        return ExecuteNoManifest(plan);
    }

    // Any file that needs a mod (excel files, or {mod} destPaths) →
    // resolve the target mod first. One picker serves all of them.
    if (plan.needsModPicker) {
        wstring chosenMod;
        if (scope == InstallScope::Mod) {
            // Mod scope already knows the mod — no picker.
            chosenMod = selectedMod;
        } else if (cb.pickExcelMod) {
            chosenMod = cb.pickExcelMod(cb.ctx, mods);
        }
        if (chosenMod.empty()) {          // user cancelled the picker
            DiscardPluginPlan(plan);
            return false;
        }
        // Resolves excel files AND literal {mod} destPaths against the mod.
        // Return value reflects the excel .mpq fork; only meaningful when
        // excel files are present.
        bool writable = ResolveExcelTargetMod(plan, d2rPath, chosenMod);
        if (plan.excelFilesPresent && !writable && cb.encryptedMpqNotice) {
            // Packed .mpq — excel files were redirected to mods/<mod>/.
            cb.encryptedMpqNotice(cb.ctx, chosenMod);
        }
    }

    // Collision → overwrite prompt.
    OverwriteChoice choice = OverwriteChoice::All;
    if (plan.collision) {
        if (cb.askOverwrite)
            choice = cb.askOverwrite(cb.ctx, plan.hasConfig, plan.pluginName);
        else
            choice = OverwriteChoice::Cancel;   // no handler → safe default
        if (choice == OverwriteChoice::Cancel) {
            DiscardPluginPlan(plan);
            return false;
        }
    }

    return ExecutePluginPlan(plan, choice);
}

// ─────────────────────────────────────────────────────────────────────
//  Bare patch drop (v1.6) — a straight .json → patches folder
// ─────────────────────────────────────────────────────────────────────

// Install one patch JSON (from `srcPath`, which may be a loose file or an
// extracted temp file) into the patches folder at scope. Shared by the
// bare-.json drop and the patch-bundle zip. Handles the allowlist gate,
// disabled-in-place, collision prompt, and .old backup. Returns true if
// the file was written.
static bool InstallOnePatch(const wstring& srcPath,
                            const wstring& d2rPath,
                            InstallScope scope,
                            const wstring& selectedMod,
                            const PluginDropCallbacks& cb,
                            const PluginAllowlist& allow) {
    wstring leaf = BaseName(srcPath);
    if (leaf.empty()) return false;

    // Allowlist gate: the patch filename must be sanctioned. The mod's
    // plugin_config "plugins" list doubles as the patch allowlist.
    if (allow.active) {
        bool matched = false;
        for (const wstring& e : allow.entries)
            if (_wcsicmp(e.c_str(), leaf.c_str()) == 0) { matched = true; break; }
        if (!matched) {
            if (cb.notAuthorized)
                cb.notAuthorized(cb.ctx, leaf, allow.modName, allow.modAuthor);
            return false;
        }
    }

    // Resolve the patches folder, honoring disabled-in-place.
    wstring base       = D2rLoaderBase(d2rPath, scope, selectedMod);
    wstring folder     = FolderForDest(PluginDest::Patches, base, L"");
    wstring activePath   = folder + L"\\" + leaf;
    wstring disabledPath = folder + L"\\Disabled\\" + leaf;
    wstring dest = (!ZI_FileExists(activePath) && ZI_FileExists(disabledPath))
                     ? disabledPath : activePath;

    // Collision → Yes/No overwrite + .old backup.
    if (ZI_FileExists(dest)) {
        OverwriteChoice choice = OverwriteChoice::Cancel;
        if (cb.askOverwrite) choice = cb.askOverwrite(cb.ctx, /*hasConfig=*/false, leaf);
        if (choice == OverwriteChoice::Cancel) return false;
        BackupConfigIfExists(dest);
    }

    return CopyOneFile(srcPath, dest);
}

bool HandleBarePatchDrop(const wstring& jsonPath,
                         const wstring& d2rPath,
                         InstallScope scope,
                         const wstring& selectedMod,
                         const PluginDropCallbacks& cb,
                         const PluginAllowlist& allow) {
    return InstallOnePatch(jsonPath, d2rPath, scope, selectedMod, cb, allow);
}

int HandlePatchBundleZip(const wstring& zipPath,
                         const wstring& d2rPath,
                         InstallScope scope,
                         const wstring& selectedMod,
                         const PluginDropCallbacks& cb,
                         const PluginAllowlist& allow) {
    // Extract to temp, collect the .json files, install each to patches.
    wstring tmp = MakeTempInstallDir();
    if (tmp.empty()) {
        if (cb.errorNotice) cb.errorNotice(cb.ctx, L"Could not create a temporary folder.");
        return 0;
    }
    if (!RunTarExtract(zipPath, tmp)) {
        DeleteFolderRecursive(tmp);
        if (cb.errorNotice) cb.errorNotice(cb.ctx, L"Could not extract the archive.");
        return 0;
    }

    vector<wstring> jsons;
    CollectJsonFiles(tmp, jsons);   // only .json — other files ignored

    int installed = 0;
    for (const wstring& j : jsons)
        if (InstallOnePatch(j, d2rPath, scope, selectedMod, cb, allow))
            ++installed;

    DeleteFolderRecursive(tmp);
    return installed;
}
