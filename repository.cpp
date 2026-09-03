// ═══════════════════════════════════════════════════════════════════════
//  repository.cpp — plugin/patch repository model + manifest fetch (v1.6.2)
//  See repository.h for the flow and repository_SCHEMA.md for the format.
// ═══════════════════════════════════════════════════════════════════════

#include "repository.h"
#include "core.h"    // JsonStr, JsonInt
#include "http.h"    // HttpGet, HttpResult, HttpDownloadFile
#include "fs_utils.h"       // MakeTempInstallDir, DeleteFolderRecursive
#include "plugin_install.h" // PeekManifestName, PeekZipKind, ZipKind
#include "plugin_drop_ui.h" // HandleRepoInstall

#include <string>
#include <shlwapi.h>
#include <windows.h>

using std::wstring;
using std::vector;

// ── Repository source URL ──────────────────────────────────────────────
// Direct-download form: https://drive.google.com/uc?export=download&id=FILEID
// (NOT the .../file/d/FILEID/view share page, which returns an HTML
// viewer rather than the JSON — the LooksLikeJson check would reject it.)
//
// The file must be shared as "Anyone with the link" or the fetch gets an
// HTML sign-in page instead of the manifest.
const wchar_t* REPO_MANIFEST_URL =
    L"https://drive.google.com/uc?export=download&id=1Iip09_rMNsrYUDHK9vnFExIPTd75xwxd";

// Set by InstallRepoEntry when a name check fails, so the error can say
// what the zip actually declared instead of leaving the user to guess.
// A NameMismatch is nearly always a metadata typo rather than a broken
// link, and without both strings side by side it's near-impossible to
// spot an en-dash, a curly apostrophe, or "&" vs "and".
static wstring g_repoDeclaredName;

wstring RepoInstallResultText(RepoInstallResult r, const RepoEntry& e) {
    switch (r) {
        case RepoInstallResult::Success:
            return e.name + L"\nwas downloaded and installed successfully.";
        case RepoInstallResult::DownloadFailed:
            return L"Couldn't retrieve \"" + e.name + L"\".\n"
                   L"The file may have moved or the link is unavailable.";
        case RepoInstallResult::NotAZip:
            return L"The download for \"" + e.name + L"\" didn't return a "
                   L"usable file. The link may be wrong or the file was "
                   L"removed.";
        case RepoInstallResult::NameMismatch:
            return L"The downloaded file doesn't match \"" + e.name + L"\".\n\n"
                   L"Catalog name:  \"" + e.name + L"\"\n"
                   L"File declares: \"" + g_repoDeclaredName + L"\"\n\n"
                   L"These must match exactly (case aside). Fix whichever "
                   L"is wrong — the catalog entry, or the plugin_info.json "
                   L"inside the zip.";
        case RepoInstallResult::ExtractFailed:
            return L"\"" + e.name + L"\" was downloaded but could not be "
                   L"extracted or installed.";
        case RepoInstallResult::InstallDeclined:
            return L"Installation of \"" + e.name + L"\" was cancelled.";
    }
    return L"";
}

namespace {

// Find this object's real closing brace, skipping any '}' inside a string
// value (same guard the plugin manifest parser uses — a value like
// "{mod}/{mpq}" would otherwise truncate the object at the first '}').
size_t FindObjectEnd(const wstring& j, size_t open) {
    bool inStr = false;
    for (size_t k = open; k < j.size(); ++k) {
        wchar_t c = j[k];
        if (c == L'"' && (k == 0 || j[k - 1] != L'\\'))
            inStr = !inStr;
        else if (c == L'}' && !inStr)
            return k;
    }
    return wstring::npos;
}

// Extract a string array value: "key": ["a","b"]. Returns each element.
vector<wstring> JsonStrArray(const wstring& obj, const wstring& key) {
    vector<wstring> out;
    wstring needle = L"\"" + key + L"\"";
    size_t k = obj.find(needle);
    if (k == wstring::npos) return out;
    size_t lb = obj.find(L'[', k);
    if (lb == wstring::npos) return out;
    size_t rb = obj.find(L']', lb);
    if (rb == wstring::npos) return out;
    wstring inside = obj.substr(lb + 1, rb - lb - 1);
    // Split on quoted strings.
    size_t p = 0;
    while (p < inside.size()) {
        size_t q1 = inside.find(L'"', p);
        if (q1 == wstring::npos) break;
        size_t q2 = inside.find(L'"', q1 + 1);
        if (q2 == wstring::npos) break;
        out.push_back(inside.substr(q1 + 1, q2 - q1 - 1));
        p = q2 + 1;
    }
    return out;
}

} // namespace

RepoManifest ParseRepoManifest(const wstring& json) {
    RepoManifest m;
    if (json.empty()) {
        m.error = L"The repository manifest was empty.";
        return m;
    }

    m.schema   = JsonInt(json, L"schema", 0);
    m.repoName = JsonStr(json, L"repo_name");
    m.updated  = JsonStr(json, L"updated");

    if (m.schema <= 0) {
        m.error = L"The manifest is missing a valid \"schema\" number.";
        return m;
    }
    if (m.schema > REPO_SCHEMA_SUPPORTED) {
        m.error = L"This repository needs a newer launcher. Please update "
                  L"Angiris, then try again.";
        return m;
    }

    // Find the "entries" array and walk its objects.
    size_t ek = json.find(L"\"entries\"");
    if (ek == wstring::npos) {
        m.error = L"The manifest has no \"entries\" list.";
        return m;
    }
    size_t lb = json.find(L'[', ek);
    if (lb == wstring::npos) {
        m.error = L"The manifest \"entries\" list is malformed.";
        return m;
    }

    size_t p = lb + 1;
    while (p < json.size()) {
        // Skip whitespace / commas to the next object or the closing ']'.
        while (p < json.size() && (json[p] == L' ' || json[p] == L'\t' ||
               json[p] == L'\r' || json[p] == L'\n' || json[p] == L','))
            ++p;
        if (p >= json.size() || json[p] == L']') break;
        if (json[p] != L'{') { ++p; continue; }

        size_t oe = FindObjectEnd(json, p);
        if (oe == wstring::npos) break;
        wstring obj = json.substr(p, oe - p + 1);

        RepoEntry e;
        e.id          = JsonStr(obj, L"id");
        e.name        = JsonStr(obj, L"name");
        e.kind        = JsonStr(obj, L"kind");
        e.url         = JsonStr(obj, L"url");
        e.fileName    = JsonStr(obj, L"fileName");
        e.author      = JsonStr(obj, L"author");
        e.summary     = JsonStr(obj, L"summary");
        e.description = JsonStr(obj, L"description");
        e.version     = JsonStr(obj, L"version");
        e.updated     = JsonStr(obj, L"updated");
        e.tags        = JsonStrArray(obj, L"tags");

        // Require the fields the launcher can't function without. A row that
        // lacks id/name/url is skipped rather than shown broken.
        if (!e.id.empty() && !e.name.empty() && !e.url.empty()) {
            if (e.kind.empty()) e.kind = L"plugin";   // sensible default
            m.entries.push_back(e);
        }

        p = oe + 1;
    }

    m.ok = true;
    return m;
}

RepoManifest FetchRepoManifest(const wstring& manifestUrl, int timeoutMs) {
    RepoManifest m;
    if (manifestUrl.empty()) {
        m.error = L"No repository URL is configured.";
        return m;
    }

    HttpResult r = HttpGet(manifestUrl, timeoutMs);
    if (r.timedOut) {
        m.error = L"The repository timed out. Check your connection and try again.";
        return m;
    }
    if (r.status < 200 || r.status >= 300 || r.body.empty()) {
        m.error = L"Couldn't reach the repository (status "
                + std::to_wstring(r.status) + L").";
        return m;
    }

    // Guard against getting an HTML page instead of JSON — the classic
    // Google-Drive-returned-a-page failure. A JSON manifest starts with '{'
    // (after optional whitespace/BOM).
    wstring b = r.body;
    size_t s = b.find_first_not_of(L" \t\r\n\xFEFF");
    if (s == wstring::npos || b[s] != L'{') {
        m.error = L"The repository link didn't return catalog data. If it's a "
                  L"Google Drive link, make sure it's the direct-download form.";
        return m;
    }

    return ParseRepoManifest(r.body);
}

// ── Install one catalog entry ──────────────────────────────────────────
namespace {

// A temp path for the downloaded file, under the install temp area.
// The temp filename MATTERS for patches: InstallOnePatch installs a bare
// .json under BaseName(src), and that same leaf is what a manifest-mode
// mod's allowlist is matched against. Downloading to a generic
// "repo_download.json" therefore both misnames the installed patch and
// makes it fail any allowlist check. Plugin zips are unaffected — their
// manifest supplies the real names — but the base name is passed in for
// both so there's one rule.
wstring RepoTempFile(const wstring& baseName, const wstring& kindExt) {
    wstring dir = MakeTempInstallDir();
    if (dir.empty()) return L"";
    wstring leaf = baseName.empty() ? wstring(L"repo_download") : baseName;
    // Strip anything that can't be a filename component.
    wstring safe;
    for (wchar_t c : leaf) {
        if (c < 0x20) continue;
        if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' ||
            c == L'"'  || c == L'<' || c == L'>' || c == L'|')
            continue;
        safe += c;
    }
    while (!safe.empty() && (safe.back() == L' ' || safe.back() == L'.'))
        safe.pop_back();
    if (safe.empty()) safe = L"repo_download";
    return dir + L"\\" + safe + kindExt;
}

// Filename a downloaded patch should be installed under. Prefers the
// catalog's explicit fileName, else a sanitized display name.
static wstring RepoPatchBaseName(const RepoEntry& e) {
    if (!e.fileName.empty()) {
        wstring b = e.fileName;
        // Accept "x.json" or bare "x" — the extension is added by caller.
        size_t dot = b.find_last_of(L'.');
        if (dot != wstring::npos && _wcsicmp(b.substr(dot).c_str(), L".json") == 0)
            b = b.substr(0, dot);
        return b;
    }
    return e.name;
}

// Does a downloaded file look like a real zip? (magic bytes "PK\x03\x04").
bool LooksLikeZip(const wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    unsigned char sig[4] = {};
    DWORD got = 0;
    ReadFile(h, sig, 4, &got, nullptr);
    CloseHandle(h);
    return got == 4 && sig[0] == 0x50 && sig[1] == 0x4B &&
           sig[2] == 0x03 && sig[3] == 0x04;
}

// Does a downloaded file look like JSON? (first non-space char is '{').
bool LooksLikeJson(const wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char buf[64] = {};
    DWORD got = 0;
    ReadFile(h, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(h);
    for (DWORD i = 0; i < got; ++i) {
        char c = buf[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if ((unsigned char)c == 0xEF || (unsigned char)c == 0xBB ||
            (unsigned char)c == 0xBF) continue;  // BOM bytes
        return c == '{';
    }
    return false;
}

} // namespace

RepoInstallResult InstallRepoEntry(HWND parent, const RepoEntry& e,
                                   RepoInstallScope scope,
                                   const wstring& modForModLocal) {
    bool isPatch = (_wcsicmp(e.kind.c_str(), L"patch") == 0);
    wstring dest = isPatch
                     ? RepoTempFile(RepoPatchBaseName(e), L".json")
                     : RepoTempFile(e.name, L".zip");
    if (dest.empty()) return RepoInstallResult::DownloadFailed;

    // 1. Download.
    int rc = HttpDownloadFile(e.url, dest, 30000);
    if (rc != 0) {
        DeleteFileW(dest.c_str());
        return RepoInstallResult::DownloadFailed;
    }

    // 2. Type check — guards against a Drive HTML page in place of the file.
    if (isPatch) {
        if (!LooksLikeJson(dest)) { DeleteFileW(dest.c_str()); return RepoInstallResult::NotAZip; }
    } else {
        if (!LooksLikeZip(dest)) { DeleteFileW(dest.c_str()); return RepoInstallResult::NotAZip; }
    }

    // 3. Name verification for plugin zips — the file's manifest name must
    //    match the catalog entry (guards a wrong/swapped file behind the
    //    URL). Bare patches (.json) have no manifest name to check.
    if (!isPatch) {
        wstring declared = PeekManifestName(dest);
        // Trim both sides before comparing. _wcsicmp folds case but
        // nothing else, so a stray leading/trailing space in either the
        // catalog or the manifest reads as a hard mismatch. Everything
        // else (dashes, apostrophes, & vs and) still has to match, and
        // the error below now prints both so it can be seen.
        auto trim = [](wstring v) {
            size_t b = v.find_first_not_of(L" \t\r\n");
            if (b == wstring::npos) return wstring();
            size_t en = v.find_last_not_of(L" \t\r\n");
            return v.substr(b, en - b + 1);
        };
        declared = trim(declared);
        wstring expected = trim(e.name);
        g_repoDeclaredName = declared;
        // If the zip has a manifest name, it must match. A manifest-less zip
        // (no plugin_info.json) can't be verified this way — allow it through
        // (the install flow handles no-manifest zips), but a present-and-
        // wrong name is a hard mismatch.
        if (!declared.empty() &&
            _wcsicmp(declared.c_str(), expected.c_str()) != 0) {
            DeleteFileW(dest.c_str());
            return RepoInstallResult::NameMismatch;
        }
    }

    // 4. Install via the SAME entry points a drag-drop uses. Scope + the
    //    mod-bypass wiring is applied inside plugin_drop_ui for the repo
    //    path (see HandleRepoInstall). For now, route by scope + kind.
    //    (The scope/consent/bypass flow is completed in the modal stage.)
    bool ok = HandleRepoInstall(parent, dest, isPatch,
                                scope == RepoInstallScope::ModLocal,
                                modForModLocal);
    DeleteFileW(dest.c_str());
    return ok ? RepoInstallResult::Success : RepoInstallResult::ExtractFailed;
}
