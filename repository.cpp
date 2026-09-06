// ═══════════════════════════════════════════════════════════════════════
//  repository.cpp — plugin/patch repository model + manifest fetch (v1.7)
//  See repository.h for the flow and repository_SCHEMA.md for the format.
// ═══════════════════════════════════════════════════════════════════════

#include "repository.h"
#include "core.h"    // JsonStr, JsonInt
#include "http.h"    // HttpGet, HttpResult, HttpDownloadFile
#include "fs_utils.h"       // MakeTempInstallDir, DeleteFolderRecursive
#include "plugin_install.h" // PeekManifestName, PeekZipKind, ZipKind
#include "plugin_drop_ui.h" // HandleRepoInstall
#include "d2rloader_update.h" // ComputeFileSha256 (hub release verification)

#include <vector>
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
// The catalog source. As of v1.7 this is the D2RLoader Extension Hub
// rather than a hand-maintained Drive manifest — the hub is public,
// unauthenticated, versioned (/api/v1/), and every package is scanned
// and hashed before publication.
//
// type=both matches the hub's own Plugin/Patch filter; sort=newest
// matches its default ordering.
const wchar_t* REPO_MANIFEST_URL =
    L"https://d2rloader.net/api/v1/hub/plugins?type=both&sort=newest";

// Previous source, kept for reference. FetchRepoManifest still parses
// this format if REPO_MANIFEST_URL is pointed back at it:
//   https://drive.google.com/uc?export=download&id=1Iip09_rMNsrYUDHK9vnFExIPTd75xwxd

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
        case RepoInstallResult::HashMismatch:
            return L"\"" + e.name + L"\" failed its integrity check.\n\n"
                   L"The download doesn't match the hash the hub published "
                   L"for this release, so it hasn't been installed.\n\n"
                   L"Expected: " + e.sha256 + L"\n"
                   L"Got:      " + g_repoDeclaredName + L"\n\n"
                   L"This usually means an interrupted download. Try again; "
                   L"if it keeps failing, report it to the plugin author.";
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

// Find this object's real closing brace, skipping any brace inside a
// string value (a value like "{mod}/{mpq}" would otherwise truncate the
// object at the first '}').
//
// Tracks NESTING DEPTH. It originally returned the first unquoted '}',
// which was correct only because the old hand-written manifest's entries
// were flat. Hub items nest loaderVersion{}, tags[{}] and
// latestRelease{}, so the naive version stopped at the first inner
// object's brace and silently truncated everything after it — the outer
// object appeared to have no release data at all.
size_t FindObjectEnd(const wstring& j, size_t open) {
    bool inStr = false;
    int depth = 0;
    for (size_t k = open; k < j.size(); ++k) {
        wchar_t c = j[k];
        if (c == L'"' && (k == 0 || j[k - 1] != L'\\')) {
            inStr = !inStr;
            continue;
        }
        if (inStr) continue;
        if (c == L'{' || c == L'[') {
            ++depth;
        } else if (c == L'}' || c == L']') {
            --depth;
            if (depth == 0) return k;
        }
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


// ═══════════════════════════════════════════════════════════════════════
//  D2RLOADER EXTENSION HUB
// ═══════════════════════════════════════════════════════════════════════
//
// https://d2rloader.net/api/v1/hub/plugins — public, no auth, JSON.
// Replaces the hand-maintained Drive manifest as the catalog source.
//
// Query params observed on the hub's own page: sort=newest|updated|
// downloads|rating, type=both|plugin|patch. The browser's type toggle
// maps straight onto that.
//
// Downloads go through the hub's own release endpoint rather than a
// storage URL built from storageKey:
//     GET /api/v1/hub/releases/<releaseId>/download
// which 302s to wherever the blob actually lives. That indirection is
// the hub's to manage — it can move storage without breaking us, which
// matters given the site has already been restructured once during this
// project. HttpDownloadFile follows redirects
// (WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS), so no extra handling here.
static const wchar_t* HUB_RELEASE_BASE =
    L"https://d2rloader.net/api/v1/hub/releases/";

wstring HubDownloadUrl(const RepoEntry& e) {
    if (e.releaseId.empty()) return L"";
    return wstring(HUB_RELEASE_BASE) + e.releaseId + L"/download";
}

// Extract a nested JSON object by key: finds "key" then the '{' that
// follows and returns that whole object, braces included. Empty when the
// key is absent or null (the hub uses null for several optionals).
static wstring JsonSubObject(const wstring& json, const wchar_t* key) {
    wstring needle = wstring(L"\"") + key + L"\"";
    size_t k = json.find(needle);
    if (k == wstring::npos) return L"";
    size_t c = json.find(L':', k + needle.size());
    if (c == wstring::npos) return L"";
    size_t b = json.find_first_not_of(L" \t\r\n", c + 1);
    if (b == wstring::npos || json[b] != L'{') return L"";   // null or scalar
    size_t e = FindObjectEnd(json, b);
    if (e == wstring::npos) return L"";
    return json.substr(b, e - b + 1);
}

// The hub's tags are objects ({id, name, slug}), not bare strings, so
// JsonStrArray doesn't apply — walk the array and take each "name".
static vector<wstring> JsonTagNames(const wstring& obj) {
    vector<wstring> out;
    size_t k = obj.find(L"\"tags\"");
    if (k == wstring::npos) return out;
    size_t lb = obj.find(L'[', k);
    if (lb == wstring::npos) return out;
    size_t p = lb + 1;
    while (p < obj.size()) {
        while (p < obj.size() && (obj[p] == L' ' || obj[p] == L'\t' ||
               obj[p] == L'\r' || obj[p] == L'\n' || obj[p] == L','))
            ++p;
        if (p >= obj.size() || obj[p] == L']') break;
        if (obj[p] != L'{') { ++p; continue; }
        size_t oe = FindObjectEnd(obj, p);
        if (oe == wstring::npos) break;
        wstring t = JsonStr(obj.substr(p, oe - p + 1), L"name");
        if (!t.empty()) out.push_back(t);
        p = oe + 1;
    }
    return out;
}

// Trim an ISO-8601 timestamp to just the date. The hub returns
// "2026-09-05T13:02:50.108Z"; the detail panel only wants the day.
static wstring IsoDateOnly(const wstring& iso) {
    size_t t = iso.find(L'T');
    return (t == wstring::npos) ? iso : iso.substr(0, t);
}

RepoManifest ParseHubCatalog(const wstring& json, bool newestLoaderOnly) {
    RepoManifest m;
    m.repoName = L"Angiris Community Plugins";

    size_t ik = json.find(L"\"items\"");
    if (ik == wstring::npos) {
        m.error = L"The hub response has no \"items\" list.";
        return m;
    }
    size_t lb = json.find(L'[', ik);
    if (lb == wstring::npos) {
        m.error = L"The hub \"items\" list is malformed.";
        return m;
    }

    int maxLoaderId = 0;
    size_t p = lb + 1;
    while (p < json.size()) {
        while (p < json.size() && (json[p] == L' ' || json[p] == L'\t' ||
               json[p] == L'\r' || json[p] == L'\n' || json[p] == L','))
            ++p;
        if (p >= json.size() || json[p] == L']') break;
        if (json[p] != L'{') { ++p; continue; }

        size_t oe = FindObjectEnd(json, p);
        if (oe == wstring::npos) break;
        wstring obj = json.substr(p, oe - p + 1);
        p = oe + 1;

        RepoEntry e;
        // Every top-level scalar precedes the nested loaderVersion /
        // tags / latestRelease objects in the hub's output, so a
        // first-match JsonStr resolves to the right one. Anything that
        // ALSO appears nested (version, fileName, name-inside-tags) is
        // read from an explicitly scoped sub-object instead.
        e.id          = JsonStr(obj, L"id");
        e.name        = JsonStr(obj, L"name");
        e.kind        = JsonStr(obj, L"type");        // "plugin" | "patch"
        e.slug        = JsonStr(obj, L"slug");
        e.author      = JsonStr(obj, L"author");
        e.summary     = JsonStr(obj, L"summary");
        e.description = JsonStr(obj, L"description");
        e.updated     = IsoDateOnly(JsonStr(obj, L"publishedAt"));
        e.downloadCount  = JsonInt(obj, L"downloadCount", 0);
        e.ratingAverage  = JsonStr(obj, L"ratingAverage");
        e.ratingCount    = JsonInt(obj, L"ratingCount", 0);
        e.loaderVersionId = JsonInt(obj, L"loaderVersionId", 0);
        e.tags        = JsonTagNames(obj);

        wstring lv = JsonSubObject(obj, L"loaderVersion");
        if (!lv.empty()) e.loaderVersion = JsonStr(lv, L"version");

        wstring rel = JsonSubObject(obj, L"latestRelease");
        if (!rel.empty()) {
            e.releaseId  = JsonStr(rel, L"id");
            e.version    = JsonStr(rel, L"version");
            e.fileName   = JsonStr(rel, L"fileName");
            e.storageKey = JsonStr(rel, L"storageKey");
            e.sha256     = JsonStr(rel, L"sha256");
            e.sizeBytes  = (long long)JsonInt(rel, L"sizeBytes", 0);
        }
        e.url = HubDownloadUrl(e);

        // A release with no id or no hash can't be downloaded or
        // verified, so it isn't listed at all. storageKey is kept for
        // reference but isn't required — the download endpoint is built
        // from releaseId.
        if (e.id.empty() || e.name.empty() ||
            e.releaseId.empty() || e.sha256.empty()) continue;
        if (e.kind.empty()) e.kind = L"plugin";

        if (e.loaderVersionId > maxLoaderId) maxLoaderId = e.loaderVersionId;
        m.entries.push_back(e);
    }

    // Keep only entries built for the newest D2RLoader the hub knows
    // about. Filtering on loaderVersionId (the hub's own ordering key)
    // rather than the version string means this follows the hub forward
    // without a code change when 1.3 lands.
    if (newestLoaderOnly && maxLoaderId > 0) {
        vector<RepoEntry> keep;
        for (const RepoEntry& e : m.entries)
            if (e.loaderVersionId == maxLoaderId) keep.push_back(e);
        m.entries.swap(keep);
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

    // Guard against getting an HTML page instead of JSON.
    wstring b = r.body;
    size_t s = b.find_first_not_of(L" \t\r\n\xFEFF");
    if (s == wstring::npos || b[s] != L'{') {
        m.error = L"The repository link didn't return catalog data.";
        return m;
    }

    // Two catalog formats. The Extension Hub wraps its rows in "items"
    // and has no "schema" field; the older hand-written manifest uses
    // "entries" with a schema number. Detect rather than assume, so a
    // stale URL produces a sensible error instead of an empty list.
    if (b.find(L"\"items\"") != wstring::npos &&
        b.find(L"\"schema\"") == wstring::npos) {
        return ParseHubCatalog(r.body, /*newestLoaderOnly=*/true);
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
    // 3a. Hash check. The Extension Hub publishes a sha256 for every
    // release, so we can verify the exact bytes we received rather than
    // inferring intent from a name string. This is strictly stronger
    // than the manifest-name check below and catches truncated
    // downloads, CDN errors and substituted files alike.
    //
    // Only applies when the catalog supplied a hash — the older
    // hand-written manifest format has none, and those entries fall
    // through to the name check as before.
    if (!e.sha256.empty()) {
        wstring actual = ComputeFileSha256(dest);
        if (actual.empty() || _wcsicmp(actual.c_str(), e.sha256.c_str()) != 0) {
            g_repoDeclaredName = actual.empty() ? L"(hash unavailable)" : actual;
            DeleteFileW(dest.c_str());
            return RepoInstallResult::HashMismatch;
        }
    }

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
