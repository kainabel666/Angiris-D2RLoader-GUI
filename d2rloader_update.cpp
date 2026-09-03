// ═══════════════════════════════════════════════════════════════════════
//  d2rloader_update.cpp — download + install the latest D2RLoader
// ═══════════════════════════════════════════════════════════════════════
//
//  Flow (all on a background thread):
//    1. Download d2rloader.net/downloads/latest → temp\d2rloader.zip
//    2. Verify the download against the SHA-256 published on
//       d2rloader.net/download.html. Mismatch aborts; an unavailable
//       hash only warns.
//    3. Extract with bsdtar → temp\extracted
//    4. Copy the extracted tree into the D2R directory, but PRESERVE the
//       user's D2RLoader.toml (never overwrite it).
//    5. Merge: parse the freshly-shipped toml (saved aside during copy)
//       and append any keys/sections the user's toml is missing.
//
//  The toml merge is intentionally conservative: it only ADDS. It never
//  edits, reorders, or deletes anything in the user's existing file, and
//  it never changes a value the user already has. New keys are appended
//  under their section (creating the section header if absent). This
//  means a user who has tuned their loader keeps every setting, and only
//  gains defaults for genuinely-new features.

#include "d2rloader_update.h"
#include "core.h"          // AppDir, ReadTextFile, WriteTextFile
#include "config.h"        // g_cfg (D2R path)
#include "http.h"          // HttpDownloadFile, HttpGet
#include "fs_utils.h"      // MakeTempInstallDir, RunTarExtract,
                           // DeleteFolderRecursive, CopyTreeIntoLogged
#include "zip_install.h"   // ZI_DirExists, ZI_FileExists
#include "version.h"       // CompareVersions

#include <atomic>
#include <vector>
#include <string>
#include <tlhelp32.h>      // CreateToolhelp32Snapshot, Process32
#include <winver.h>        // GetFileVersionInfo, VerQueryValue
#include <bcrypt.h>        // SHA-256 for download verification (-lbcrypt)

static const wchar_t* kD2RLoaderZipUrl =
    L"https://d2rloader.net/downloads/latest";
// The page carrying the release version + SHA-256, which is what the
// scrapers below need — NOT the site root. d2rloader.net was split into
// Overview / Download / Changelog / Docs, moving both the release
// filename and the checksum off the landing page. Pointing this at
// https://d2rloader.net finds neither, so the update check silently
// reports "up to date" forever. Keep it on the page that actually
// prints "D2RLoader-<version>.zip" and the hash.
static const wchar_t* kD2RLoaderDownloadPageUrl =
    L"https://d2rloader.net/download.html";

static std::atomic<bool> g_d2rlInstallRunning{ false };
static std::atomic<bool> g_d2rlCheckRunning{ false };

// Update-availability state (read by the main window's paint/hit-test).
bool    g_d2rloaderUpdateAvailable = false;
wstring g_d2rloaderLatestVersion;

bool IsD2RLoaderInstallRunning() { return g_d2rlInstallRunning.load(); }

// Read the file-version resource ("x.y.z") stamped into the installed
// D2RLoader.exe. Empty on any failure (missing exe / no version stamp).
wstring GetInstalledD2RLoaderVersion() {
    if (g_cfg.d2rPath.empty()) return L"";
    wstring exe = g_cfg.d2rPath + L"\\D2RLoader.exe";
    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &dummy);
    if (size == 0) return L"";
    std::vector<BYTE> buf(size);
    if (!GetFileVersionInfoW(exe.c_str(), 0, size, buf.data())) return L"";
    VS_FIXEDFILEINFO* ffi = nullptr;
    UINT ffiLen = 0;
    if (!VerQueryValueW(buf.data(), L"\\", (LPVOID*)&ffi, &ffiLen) || !ffi) {
        return L"";
    }
    WORD major = HIWORD(ffi->dwFileVersionMS);
    WORD minor = LOWORD(ffi->dwFileVersionMS);
    WORD build = HIWORD(ffi->dwFileVersionLS);
    wchar_t out[48];
    swprintf(out, 48, L"%u.%u.%u", major, minor, build);
    return out;
}

// Scrape the latest version from the d2rloader.net download page. It
// exposes the version in several spots; the most stable is the release
// filename "D2RLoader-<version>.zip" (e.g. D2RLoader-1.0.1-beta.zip).
// Returns the version string (e.g. "1.0.1-beta") or empty if not found.
static wstring ScrapeLatestVersion(const wstring& html) {
    // Find "D2RLoader-" then read until ".zip".
    const wstring needle = L"D2RLoader-";
    size_t p = html.find(needle);
    while (p != wstring::npos) {
        size_t start = p + needle.size();
        size_t zip = html.find(L".zip", start);
        if (zip != wstring::npos && zip > start && (zip - start) < 40) {
            wstring ver = html.substr(start, zip - start);
            // Sanity: must start with a digit (avoids matching stray
            // "D2RLoader-Something" text).
            if (!ver.empty() && iswdigit(ver[0])) return ver;
        }
        p = html.find(needle, start);
    }
    return L"";
}

// Strip a pre-release suffix ("-beta", "-alpha", "-rc1", …) leaving just
// the numeric "x.y.z". Used so the site's "1.0.1-beta" compares cleanly
// against the exe's numeric-only "1.0.1".
static wstring StripPreRelease(const wstring& v) {
    size_t dash = v.find(L'-');
    return (dash == wstring::npos) ? v : v.substr(0, dash);
}

// ─────────────────────────────────────────────────────────────────────
//  Download integrity (SHA-256)
// ─────────────────────────────────────────────────────────────────────

static bool IsHexDigit(wchar_t c) {
    return (c >= L'0' && c <= L'9')
        || (c >= L'a' && c <= L'f')
        || (c >= L'A' && c <= L'F');
}

// Pull the published SHA-256 out of the d2rloader.net download page. It
// prints the hash in a code block under a "SHA-256" heading, and ALSO
// prints a certificate thumbprint elsewhere — the thumbprint is 40 hex
// chars and a SHA-256 is exactly 64, so requiring a bounded 64-char hex
// run keeps the two from being confused even if the headings move.
// Returns lowercase hex, or empty if not found.
static wstring ScrapeSha256(const wstring& html) {
    // Prefer scanning from the "SHA-256" anchor so we pick the right
    // block when several hashes are present; fall back to the whole
    // document if the heading text ever changes.
    size_t from = 0;
    size_t anchor = html.find(L"SHA-256");
    if (anchor == wstring::npos) anchor = html.find(L"SHA256");
    if (anchor != wstring::npos) from = anchor;

    for (int pass = 0; pass < 2; ++pass) {
        size_t i = (pass == 0) ? from : 0;
        while (i < html.size()) {
            if (!IsHexDigit(html[i])) { ++i; continue; }
            size_t start = i;
            while (i < html.size() && IsHexDigit(html[i])) ++i;
            if (i - start == 64) {
                wstring hex = html.substr(start, 64);
                for (wchar_t& c : hex) {
                    if (c >= L'A' && c <= L'F') c = (wchar_t)(c - L'A' + L'a');
                }
                return hex;
            }
        }
        if (!from) break;   // pass 0 already covered the whole document
    }
    return L"";
}

// Compute the SHA-256 of a file using Win32 CNG (bcrypt) — no external
// crypto dependency, just -lbcrypt. Returns lowercase hex, or empty on
// any failure (caller treats that as "couldn't verify").
static wstring ComputeFileSha256(const wstring& path) {
    wstring result;
    BCRYPT_ALG_HANDLE  hAlg  = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    std::vector<BYTE>  hashObj, hashVal;
    HANDLE hFile = INVALID_HANDLE_VALUE;

    auto cleanup = [&]() {
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
        if (hHash) BCryptDestroyHash(hHash);
        if (hAlg)  BCryptCloseAlgorithmProvider(hAlg, 0);
    };

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM,
                                    nullptr, 0) < 0) {
        cleanup(); return L"";
    }

    DWORD cbObj = 0, cbData = 0, cbHash = 0;
    if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&cbObj,
                          sizeof(cbObj), &cbData, 0) < 0) {
        cleanup(); return L"";
    }
    if (BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&cbHash,
                          sizeof(cbHash), &cbData, 0) < 0) {
        cleanup(); return L"";
    }
    hashObj.resize(cbObj);
    hashVal.resize(cbHash);

    if (BCryptCreateHash(hAlg, &hHash, hashObj.data(), cbObj,
                         nullptr, 0, 0) < 0) {
        cleanup(); return L"";
    }

    hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                        nullptr, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                        nullptr);
    if (hFile == INVALID_HANDLE_VALUE) { cleanup(); return L""; }

    std::vector<BYTE> buf(64 * 1024);
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(hFile, buf.data(), (DWORD)buf.size(), &got, nullptr)) {
            cleanup(); return L"";
        }
        if (got == 0) break;                 // EOF
        if (BCryptHashData(hHash, buf.data(), got, 0) < 0) {
            cleanup(); return L"";
        }
    }

    if (BCryptFinishHash(hHash, hashVal.data(), cbHash, 0) < 0) {
        cleanup(); return L"";
    }

    static const wchar_t* kHex = L"0123456789abcdef";
    result.reserve(cbHash * 2);
    for (DWORD i = 0; i < cbHash; ++i) {
        result += kHex[(hashVal[i] >> 4) & 0xF];
        result += kHex[hashVal[i] & 0xF];
    }
    cleanup();
    return result;
}

// True if D2R.exe or D2RLoader.exe is in the current process snapshot.
// Case-insensitive. Used to block an install while the loader/game is
// running (their open handles would make the file copy fail partway).
bool IsD2RLoaderRunning() {
    static const wchar_t* const names[] = { L"D2R.exe", L"D2RLoader.exe" };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe = { sizeof(pe) };
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            for (const wchar_t* nm : names) {
                if (_wcsicmp(pe.szExeFile, nm) == 0) { found = true; break; }
            }
            if (found) break;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

// Force-terminate every running D2R.exe / D2RLoader.exe. Walks the
// process snapshot, opens each match with PROCESS_TERMINATE, and calls
// TerminateProcess. Returns the count we asked to terminate. Failures
// to open/terminate a single process (e.g. insufficient rights) are
// skipped silently — the caller re-checks IsD2RLoaderRunning afterward
// to know whether the field is actually clear.
int TerminateD2RLoaderProcesses() {
    static const wchar_t* const names[] = { L"D2R.exe", L"D2RLoader.exe" };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe = { sizeof(pe) };
    int terminated = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            bool match = false;
            for (const wchar_t* nm : names) {
                if (_wcsicmp(pe.szExeFile, nm) == 0) { match = true; break; }
            }
            if (!match) continue;
            HANDLE p = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
            if (p) {
                if (TerminateProcess(p, 1)) ++terminated;
                CloseHandle(p);
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return terminated;
}

// ─────────────────────────────────────────────────────────────────────
//  TOML merge
// ─────────────────────────────────────────────────────────────────────
//
//  A minimal TOML model sufficient for D2RLoader's flat-ish config:
//  sections introduced by "[section.name]" headers, each holding
//  "key = value" lines. Comments (# ...) and blank lines are content we
//  preserve verbatim when they belong to the NEW file's added keys, but
//  we don't try to interpret them.

// Trim leading/trailing ASCII whitespace.
static wstring TrimWs(const wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r'
                     || s[a] == L'\n')) ++a;
    while (b > a && (s[b-1] == L' ' || s[b-1] == L'\t' || s[b-1] == L'\r'
                     || s[b-1] == L'\n')) --b;
    return s.substr(a, b - a);
}

// If `line` is a section header "[name]", return name; else empty.
static wstring ParseSectionHeader(const wstring& line) {
    wstring t = TrimWs(line);
    if (t.size() >= 2 && t.front() == L'[' && t.back() == L']') {
        // Reject "[[array.of.tables]]" — D2RLoader's config doesn't use
        // them, and treating them as plain sections would be wrong. If
        // one ever appears we skip it (returns empty → treated as body).
        if (t.size() >= 4 && t[1] == L'[' && t[t.size()-2] == L']')
            return L"";
        return TrimWs(t.substr(1, t.size() - 2));
    }
    return L"";
}

// If `line` is "key = value" (outside a comment), return key (trimmed);
// else empty. A leading '#' means comment → not a key.
static wstring ParseKeyName(const wstring& line) {
    wstring t = TrimWs(line);
    if (t.empty() || t[0] == L'#') return L"";
    size_t eq = t.find(L'=');
    if (eq == wstring::npos) return L"";
    return TrimWs(t.substr(0, eq));
}

// Split text into lines, keeping content but discarding the line breaks
// (we re-emit with "\r\n").
static std::vector<wstring> SplitLines(const wstring& text) {
    std::vector<wstring> out;
    wstring cur;
    for (wchar_t c : text) {
        if (c == L'\n') {
            out.push_back(cur);
            cur.clear();
        } else if (c != L'\r') {
            cur += c;
        }
    }
    out.push_back(cur);   // trailing partial line (may be empty)
    return out;
}

// Does the user's toml already have (section, key)? Section "" means the
// top-level, pre-any-header region.
static bool UserHasKey(const std::vector<wstring>& userLines,
                       const wstring& section, const wstring& key) {
    wstring cur;   // current section as we scan
    for (const wstring& ln : userLines) {
        wstring sec = ParseSectionHeader(ln);
        if (!sec.empty()) { cur = sec; continue; }
        // A "[name]" with empty inner name won't reach here (returns "").
        // Detect the bare "[]"/array case as a section change too? No —
        // ParseSectionHeader already returned "" for those; treat as body.
        wstring k = ParseKeyName(ln);
        if (!k.empty() && cur == section && k == key) return true;
    }
    return false;
}

// Does the user's toml contain a section header for `section`?
static bool UserHasSection(const std::vector<wstring>& userLines,
                           const wstring& section) {
    for (const wstring& ln : userLines) {
        if (ParseSectionHeader(ln) == section) return true;
    }
    return false;
}

// Merge new keys/sections from `newToml` into `userToml`. Returns the
// merged text (CRLF), or the original userToml unchanged if nothing was
// added. Only appends; never edits existing content.
//
// Strategy:
//   * Walk the NEW file section by section.
//   * For each (section, key) present in NEW but absent in USER:
//       - if USER already has the section, append "key = value" to the
//         END of the file under a re-stated "[section]" header block
//         gathered per-section (so multiple new keys in the same new
//         section share one appended header).
//   * Entirely-new sections get their header + all their keys appended.
//
//  We append everything in one trailing block titled with a comment so
//  the user can see what the merge added. Values come verbatim from the
//  NEW file (including any inline comment on the line).
static wstring MergeTomlAddOnly(const wstring& userToml,
                                const wstring& newToml,
                                int* addedCount) {
    std::vector<wstring> userLines = SplitLines(userToml);
    std::vector<wstring> newLines  = SplitLines(newToml);

    // Collect additions grouped by section, preserving new-file order.
    struct SectionAdds {
        wstring section;               // "" = top-level
        std::vector<wstring> lines;    // verbatim "key = value" lines
    };
    std::vector<SectionAdds> adds;

    auto findOrMakeGroup = [&](const wstring& sec) -> SectionAdds& {
        for (auto& g : adds) if (g.section == sec) return g;
        adds.push_back(SectionAdds{ sec, {} });
        return adds.back();
    };

    wstring cur;   // current section while scanning NEW
    int added = 0;
    for (const wstring& ln : newLines) {
        wstring sec = ParseSectionHeader(ln);
        if (!sec.empty()) { cur = sec; continue; }
        wstring k = ParseKeyName(ln);
        if (k.empty()) continue;                    // comment/blank/other
        if (UserHasKey(userLines, cur, k)) continue; // user already has it
        findOrMakeGroup(cur).lines.push_back(TrimWs(ln));
        ++added;
    }

    if (addedCount) *addedCount = added;
    if (added == 0) return userToml;   // nothing to do

    // Re-emit the user's file verbatim, then a trailing block with the
    // additions. Ensure exactly one blank line before our block.
    wstring out = userToml;
    // Normalize trailing whitespace to a single newline boundary.
    while (!out.empty() && (out.back() == L'\n' || out.back() == L'\r'))
        out.pop_back();
    out += L"\r\n\r\n";
    out += L"# ---- Added by Angiris Launcher (new D2RLoader keys) ----\r\n";

    for (const auto& g : adds) {
        if (g.lines.empty()) continue;
        if (!g.section.empty()) {
            out += L"[" + g.section + L"]\r\n";
        }
        for (const wstring& kv : g.lines) {
            out += kv + L"\r\n";
        }
        out += L"\r\n";
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────
//  Worker
// ─────────────────────────────────────────────────────────────────────

struct D2RLInstallCtx {
    HWND notify;
};

static void Notify(HWND h, UINT msg, WPARAM wp) {
    if (h) PostMessageW(h, msg, wp, 0);
}

static DWORD WINAPI D2RLoaderInstallWorker(LPVOID param) {
    D2RLInstallCtx* ctx = (D2RLInstallCtx*)param;
    HWND notify = ctx ? ctx->notify : nullptr;
    delete ctx;

    auto finish = [&](D2RLoaderInstallResult res) -> DWORD {
        Notify(notify, MSG_D2RLOADER_INSTALL_DONE, (WPARAM)res);
        g_d2rlInstallRunning = false;
        return (DWORD)res;
    };

    wstring d2rPath = g_cfg.d2rPath;
    if (d2rPath.empty() || !ZI_DirExists(d2rPath)) {
        return finish(D2RL_ERR_NO_PATH);
    }

    // Diagnostic log alongside the launcher's other update logs.
    wstring logPath = AppDir() + L"\\assets\\d2rloader_install.log";
    FILE* logF = nullptr;
    _wfopen_s(&logF, logPath.c_str(), L"w, ccs=UTF-8");
    auto LOG = [&](const wchar_t* fmt, auto... args) {
        if (!logF) return;
        fwprintf(logF, fmt, args...);
        fwprintf(logF, L"\n");
        fflush(logF);
    };
    LOG(L"=== D2RLoader install ===");
    LOG(L"D2R path: %ls", d2rPath.c_str());
    LOG(L"URL:      %ls", kD2RLoaderZipUrl);

    // 1. Download ------------------------------------------------------
    Notify(notify, MSG_D2RLOADER_INSTALL_PROGRESS, D2RL_STAGE_DOWNLOADING);
    wstring tmpDir = MakeTempInstallDir();
    if (tmpDir.empty()) {
        LOG(L"FAIL: MakeTempInstallDir");
        if (logF) fclose(logF);
        return finish(D2RL_ERR_DOWNLOAD);
    }
    wstring zipPath = tmpDir + L"\\d2rloader.zip";
    int dl = HttpDownloadFile(kD2RLoaderZipUrl, zipPath, 120000);
    LOG(L"Download result: %d (0=ok)", dl);
    if (dl != 0) {
        DeleteFolderRecursive(tmpDir);
        if (logF) fclose(logF);
        return finish(D2RL_ERR_DOWNLOAD);
    }

    // 2. Verify the download against the published SHA-256 ------------
    // d2rloader.net publishes the release hash on its download page. We fetch
    // that page, scrape the hash, and compare it against the bytes we
    // actually received.
    //
    // Policy:
    //   * hash present and MISMATCHED  → hard abort. Something is wrong
    //     (truncated download, MITM, stale CDN object) and extracting an
    //     unverified archive over the user's game install is exactly the
    //     situation the checksum exists to prevent.
    //   * hash unavailable (page fetch failed / format changed) → log it
    //     and continue. The download already came over HTTPS, so failing
    //     every update because a page scrape broke would trade a real
    //     feature for a theoretical gain.
    Notify(notify, MSG_D2RLOADER_INSTALL_PROGRESS, D2RL_STAGE_VERIFYING);
    {
        wstring expected;
        HttpResult page = HttpGet(kD2RLoaderDownloadPageUrl, 8000);
        if (page.status == 200 && !page.body.empty()) {
            expected = ScrapeSha256(page.body);
        }
        LOG(L"Published SHA-256: %ls",
            expected.empty() ? L"(unavailable)" : expected.c_str());

        if (expected.empty()) {
            LOG(L"WARN: could not verify download (no published hash).");
        } else {
            wstring actual = ComputeFileSha256(zipPath);
            LOG(L"Download  SHA-256: %ls",
                actual.empty() ? L"(hash failed)" : actual.c_str());
            if (actual.empty()) {
                LOG(L"WARN: could not hash the download; skipping verify.");
            } else if (actual != expected) {
                LOG(L"FAIL: checksum MISMATCH — aborting install.");
                DeleteFolderRecursive(tmpDir);
                if (logF) fclose(logF);
                return finish(D2RL_ERR_CHECKSUM);
            } else {
                LOG(L"Checksum OK — download matches the official release.");
            }
        }
    }

    // 3. Extract -------------------------------------------------------
    Notify(notify, MSG_D2RLOADER_INSTALL_PROGRESS, D2RL_STAGE_EXTRACTING);
    wstring extractDir = tmpDir + L"\\extracted";
    CreateDirectoryW(extractDir.c_str(), nullptr);
    bool tarOk = RunTarExtract(zipPath, extractDir, nullptr);
    LOG(L"Extract: %ls", tarOk ? L"OK" : L"FAILED");
    if (!tarOk) {
        DeleteFolderRecursive(tmpDir);
        if (logF) fclose(logF);
        return finish(D2RL_ERR_EXTRACT);
    }

    // Some zips wrap everything in a single top-level folder; others
    // extract flat. Detect: if extractDir has exactly one child dir and
    // no D2RLoader.exe at its root, descend into that child.
    wstring releaseRoot = extractDir;
    if (!ZI_FileExists(extractDir + L"\\D2RLoader.exe")) {
        WIN32_FIND_DATAW fd;
        wstring pat = extractDir + L"\\*";
        HANDLE h = FindFirstFileW(pat.c_str(), &fd);
        wstring onlyChildDir;
        int dirCount = 0, fileCount = 0;
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(fd.cFileName, L".") == 0) continue;
                if (wcscmp(fd.cFileName, L"..") == 0) continue;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    onlyChildDir = fd.cFileName; ++dirCount;
                } else ++fileCount;
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        if (dirCount == 1 && fileCount == 0) {
            releaseRoot = extractDir + L"\\" + onlyChildDir;
        }
    }
    LOG(L"Release root: %ls", releaseRoot.c_str());

    // Save aside the freshly-shipped toml for the merge step, THEN
    // preserve the user's toml during the copy so it isn't overwritten.
    wstring newTomlText;
    {
        wstring shipped = releaseRoot + L"\\D2RLoader.toml";
        if (ZI_FileExists(shipped)) {
            newTomlText = ReadTextFile(shipped);
            LOG(L"Shipped toml: %zu chars", newTomlText.size());
        } else {
            LOG(L"Shipped toml: (none in release)");
        }
    }

    // 4. Install (copy tree, preserving the user's toml) ---------------
    Notify(notify, MSG_D2RLOADER_INSTALL_PROGRESS, D2RL_STAGE_INSTALLING);
    // Re-check: the download can take a while, and the user could have
    // launched D2R/D2RLoader in the meantime. Copying over a running
    // loader/game would fail partway and leave a half-updated install,
    // so bail cleanly before touching any destination file.
    if (IsD2RLoaderRunning()) {
        LOG(L"ABORT: D2R/D2RLoader started during download.");
        DeleteFolderRecursive(tmpDir);
        if (logF) fclose(logF);
        return finish(D2RL_ERR_COPY);
    }
    static const wchar_t* const kPreserve[] = {
        L"D2RLoader.toml",
        nullptr,
    };
    int failCount = 0;
    LOG(L"--- copy begin ---");
    bool copyOk = CopyTreeIntoLogged(releaseRoot, d2rPath,
                                     logF, &failCount, kPreserve);
    LOG(L"--- copy end (failCount=%d, ok=%ls) ---",
        failCount, copyOk ? L"yes" : L"no");
    // A locked D2RLoader.exe (game running) is the likely failure. We
    // don't hard-fail the whole op on partial copy — but we report it.
    if (!copyOk && failCount > 0) {
        LOG(L"WARN: %d file(s) failed to copy (loader/game running?)",
            failCount);
    }

    // 5. Merge toml ----------------------------------------------------
    Notify(notify, MSG_D2RLOADER_INSTALL_PROGRESS, D2RL_STAGE_MERGING);
    if (!newTomlText.empty()) {
        wstring userTomlPath = d2rPath + L"\\D2RLoader.toml";
        wstring userToml = ReadTextFile(userTomlPath);
        if (userToml.empty()) {
            // No existing user toml — the preserve step meant the copy
            // skipped it only if it existed; if it didn't exist, the
            // copy already laid the shipped one down. Nothing to merge.
            LOG(L"No existing user toml to merge into (fresh install).");
        } else {
            int addedCount = 0;
            wstring merged = MergeTomlAddOnly(userToml, newTomlText,
                                              &addedCount);
            LOG(L"Toml merge: %d new key(s) appended", addedCount);
            if (addedCount > 0) {
                // Back up the user's toml before writing the merged one.
                wstring bak = userTomlPath + L".bak";
                CopyFileW(userTomlPath.c_str(), bak.c_str(), FALSE);
                WriteTextFile(userTomlPath, merged);
                LOG(L"Wrote merged toml (backup at %ls)", bak.c_str());
            }
        }
    }

    DeleteFolderRecursive(tmpDir);
    if (logF) fclose(logF);

    return finish(copyOk ? D2RL_OK : D2RL_ERR_COPY);
}

// ─────────────────────────────────────────────────────────────────────
//  Public entry
// ─────────────────────────────────────────────────────────────────────

void StartD2RLoaderDownloadInstall(HWND notifyHwnd) {
    bool expected = false;
    if (!g_d2rlInstallRunning.compare_exchange_strong(expected, true)) {
        // Already running.
        Notify(notifyHwnd, MSG_D2RLOADER_INSTALL_DONE, (WPARAM)D2RL_ERR_BUSY);
        return;
    }
    D2RLInstallCtx* ctx = new D2RLInstallCtx{ notifyHwnd };
    HANDLE h = CreateThread(nullptr, 0, D2RLoaderInstallWorker, ctx, 0, nullptr);
    if (h) {
        CloseHandle(h);
    } else {
        delete ctx;
        g_d2rlInstallRunning = false;
        Notify(notifyHwnd, MSG_D2RLOADER_INSTALL_DONE, (WPARAM)D2RL_ERR_DOWNLOAD);
    }
}

// ─────────────────────────────────────────────────────────────────────
//  Update-availability check
// ─────────────────────────────────────────────────────────────────────

struct D2RLCheckCtx { HWND notify; };

static DWORD WINAPI D2RLoaderUpdateCheckWorker(LPVOID param) {
    D2RLCheckCtx* ctx = (D2RLCheckCtx*)param;
    HWND notify = ctx ? ctx->notify : nullptr;
    delete ctx;

    wstring installed = GetInstalledD2RLoaderVersion();

    wstring latest;
    HttpResult r = HttpGet(kD2RLoaderDownloadPageUrl, 8000);
    if (r.status == 200 && !r.body.empty()) {
        latest = ScrapeLatestVersion(r.body);
    }

    // Diagnostic log.
    {
        wstring logPath = AppDir() + L"\\assets\\d2rloader_check.log";
        FILE* f = nullptr;
        _wfopen_s(&f, logPath.c_str(), L"w, ccs=UTF-8");
        if (f) {
            fwprintf(f, L"=== D2RLoader update check ===\n");
            fwprintf(f, L"Installed:   %ls\n",
                     installed.empty() ? L"(not detected)" : installed.c_str());
            fwprintf(f, L"HTTP status: %d\n", r.status);
            fwprintf(f, L"Latest:      %ls\n",
                     latest.empty() ? L"(scrape failed)" : latest.c_str());
            fclose(f);
        }
    }

    // Only claim an update when we have BOTH an installed version and a
    // scraped latest, and the latest is strictly newer (comparing on the
    // numeric core, ignoring the -beta suffix). Anything else — not
    // installed, scrape failed, already current — shows nothing, so we
    // never display a false "update available".
    if (!installed.empty() && !latest.empty()) {
        wstring latestCore    = StripPreRelease(latest);
        wstring installedCore = StripPreRelease(installed);
        if (CompareVersions(latestCore, installedCore) > 0) {
            g_d2rloaderLatestVersion  = latest;
            g_d2rloaderUpdateAvailable = true;
            Notify(notify, MSG_D2RLOADER_UPDATE_AVAILABLE, 0);
        }
    }

    g_d2rlCheckRunning = false;
    return 0;
}

void KickoffD2RLoaderUpdateCheck(HWND notifyHwnd) {
    if (g_d2rlCheckRunning.exchange(true)) return;
    D2RLCheckCtx* ctx = new D2RLCheckCtx{ notifyHwnd };
    HANDLE h = CreateThread(nullptr, 0, D2RLoaderUpdateCheckWorker,
                            ctx, 0, nullptr);
    if (h) CloseHandle(h);
    else { delete ctx; g_d2rlCheckRunning = false; }
}
