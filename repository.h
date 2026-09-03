// ═══════════════════════════════════════════════════════════════════════
//  repository.h — plugin/patch repository model + manifest fetch (v1.6.2)
// ═══════════════════════════════════════════════════════════════════════
//
//  Fetches a single JSON catalog from a stable public URL (Google Drive for
//  now, later D2RLoader.net), parses it into RepoEntry rows, and exposes the
//  result to the browser modal. See repository_SCHEMA.md for the format.
//
//  The catalog is a layer of indirection: each entry carries a full download
//  URL, so a plugin can move hosts by editing the manifest — no launcher
//  change. Installing an entry downloads its file and hands it to the SAME
//  drop handlers a drag-drop uses (all routing/allowlist/overwrite logic is
//  reused).

#pragma once

#include "angiris_common.h"
#include <vector>

// One catalog entry. Optional fields are empty when absent in the JSON.
struct RepoEntry {
    wstring id;          // stable unique key (immutable once published)
    wstring name;        // display name
    wstring kind;        // "plugin" | "patch"
    wstring url;         // direct download URL (zip for plugin, .json for patch)
    // PATCH ENTRIES ONLY, optional but strongly recommended. A bare
    // .json patch is installed under its own filename, and that filename
    // is ALSO what a manifest-mode mod's allowlist is checked against.
    // The download URL carries no filename (Drive serves by file id), so
    // without this the patch would land under the temp download name.
    // Set it to the patch's real filename, e.g. "hireling_ai.json".
    // Falls back to a sanitized <name>.json when absent.
    wstring fileName;
    wstring author;      // optional
    wstring summary;     // optional one-liner
    wstring description; // optional longer text
    wstring version;     // optional, reserved for future update tracking
    wstring updated;     // optional free-form date
    std::vector<wstring> tags; // optional
};

// The parsed catalog.
struct RepoManifest {
    int     schema = 0;      // format version from the JSON
    wstring repoName;        // optional display title
    wstring updated;         // optional catalog date
    std::vector<RepoEntry> entries;

    bool    ok = false;      // true if fetch + parse succeeded
    wstring error;           // human-readable error when !ok
};

// The schema version this launcher build understands. A manifest with a
// higher schema is refused (forward-safety) with a clear message.
constexpr int REPO_SCHEMA_SUPPORTED = 1;

// Fetch + parse the catalog from a URL. Network + parse errors are reported
// in the returned manifest's .ok / .error (never throws). timeoutMs bounds
// the HTTP GET.
RepoManifest FetchRepoManifest(const wstring& manifestUrl, int timeoutMs);

// Parse a manifest from an in-memory JSON string (factored out so it can be
// unit-tested / fed a local file without a network round-trip).
RepoManifest ParseRepoManifest(const wstring& json);

// ── Repository source URL ──────────────────────────────────────────────
// The catalog manifest lives at a stable public URL. Hardcoded for now
// (Google Drive direct-download form); moving to config or D2RLoader.net
// later is a one-line change here. Must be the direct-download URL, not a
// Drive "share" page (which returns HTML).
extern const wchar_t* REPO_MANIFEST_URL;

// ── Install from the repository ────────────────────────────────────────
// Outcome of a repository install attempt, surfaced to the user as a popup.
enum class RepoInstallResult {
    Success,          // downloaded, verified, and installed
    DownloadFailed,   // couldn't retrieve the file (network / not found)
    NotAZip,          // the URL didn't return a usable zip/json
    NameMismatch,     // the file's manifest name didn't match the entry
    ExtractFailed,    // extraction/parse failed during install
    InstallDeclined,  // user cancelled (overwrite/mod-scope), not an error
};

// Human-readable summary for a result (for the popup body).
wstring RepoInstallResultText(RepoInstallResult r, const RepoEntry& e);

// Install scope for a repository install, chosen by the browser's switch.
enum class RepoInstallScope { Global, ModLocal };

// Install one catalog entry. Downloads its file to a temp path, verifies the
// file's manifest name matches the entry (guards a wrong/swapped file behind
// the URL), then routes it through the normal drop-install flow at the given
// scope. `modForModLocal` is the dropdown's mod (used as the scope target and
// to bypass the mod picker). Returns the outcome for the caller to report.
RepoInstallResult InstallRepoEntry(HWND parent, const RepoEntry& e,
                                   RepoInstallScope scope,
                                   const wstring& modForModLocal);
