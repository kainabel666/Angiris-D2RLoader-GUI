// ═══════════════════════════════════════════════════════════════════════
//  repository.h — plugin/patch repository model + manifest fetch (v1.7)
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

    // ── Extension Hub fields (d2rloader.net/api/v1/hub) ──────────────
    // The hub is now the catalog source, so these come straight from
    // its JSON rather than a hand-written manifest.
    wstring slug;            // hub URL slug
    wstring releaseId;       // latestRelease.id
    wstring storageKey;      // path of the release blob (not a full URL)
    wstring sha256;          // release hash — verified after download
    long long sizeBytes = 0; // release size, for the detail panel
    int     downloadCount = 0;
    wstring ratingAverage;   // string in the API ("0", "5")
    int     ratingCount = 0;
    // D2RLoader compatibility. loaderVersionId is the hub's own
    // ordering key (higher = newer), which is what "newest only"
    // filtering keys on — more robust than string-comparing versions.
    int     loaderVersionId = 0;
    wstring loaderVersion;   // e.g. "1.2.1"
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

// Parse a response from the D2RLoader Extension Hub
// (GET https://d2rloader.net/api/v1/hub/plugins). Shape is
//   { "items": [ ... ], "page": 1, "pageSize": 18, "total": N }
// Each item carries nested loaderVersion{} and latestRelease{} objects.
//
// When newestLoaderOnly is true, entries are filtered to the highest
// loaderVersionId present in the response — so the list follows the hub
// forward as new D2RLoader versions land, with no code change.
RepoManifest ParseHubCatalog(const wstring& json, bool newestLoaderOnly);

// Full download URL for an entry's latest release. Built from the hub's
// storageKey against the blob host.
wstring HubDownloadUrl(const RepoEntry& e);

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
    HashMismatch,     // sha256 from the hub didn't match the download
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
