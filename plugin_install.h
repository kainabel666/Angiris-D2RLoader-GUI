// ═══════════════════════════════════════════════════════════════════════
//  plugin_install.h — drag-drop plugin/patch zip routing core (v1.6)
// ═══════════════════════════════════════════════════════════════════════
//
//  Reads a dropped .zip, parses its plugin_info.json manifest, and routes
//  the contained files to the correct D2RLoader folders. This is the
//  INTERNAL core — no UI, no drag-drop wiring. Step 3 (WM_DROPFILES) hands
//  zip paths here; the caller supplies callbacks for the prompts (excel
//  mod picker, overwrite choice) so this module stays UI-agnostic.
//
//  Flow, per the v1.6 design:
//    1. Extract zip to a temp dir.
//    2. Look for plugin_info.json at the root.
//         - present  → parse; route each file by its explicit `dest`.
//         - absent   → NoManifest result; caller shows the "all to
//                      plugins" notice, then ExecuteNoManifest dumps
//                      everything into the plugins folder.
//    3. Sanitize author-supplied names used as path components.
//    4. Detect a collision (a plugin with the same DLL already installed).
//    5. Route: plugins / config / patches / readme / excel per dest.
//    6. Record the readme path in plugin_manifest.json.
//
//  Scope (global vs per-mod) is chosen by the CALLER (drop location) and
//  passed in — this module doesn't know about windows.

#pragma once

#include "angiris_common.h"

// Where a file wants to go. Mirrors the manifest `dest` strings.
enum class PluginDest {
    Plugins,   // d2rloader/plugins
    Config,    // d2rloader/config
    Patches,   // d2rloader/patches
    Readme,    // d2rloader/readmes/<name>/  (renamed to <DLLName>-Readme)
    Excel,     // mods/<Mod>/<Mod>.mpq/data/global/excel/
    Literal,   // destPath: a sandboxed literal path from the D2R root
    Unknown,   // unrecognized dest string — skipped, reported
};

// Install scope, decided by drop location.
enum class InstallScope {
    Global,    // <d2r>/d2rloader/…      (dropped on the main window)
    Mod,       // <d2r>/mods/<sel>/d2rloader/…  (plugin manager open)
};

// One planned file move: from the extracted temp location to a resolved
// absolute destination. `dest`/`isDll`/`isConfig` drive collision choices.
struct PluginFileOp {
    wstring srcTempPath;   // absolute path inside the temp extract dir
    wstring destAbsPath;   // absolute final path (name already sanitized/renamed)
    PluginDest dest = PluginDest::Unknown;
    bool isDll    = false; // .dll → collision "DLL" category
    bool isConfig = false; // toml/json-config → collision "Config" category
    bool isReadme = false; // routed to the readme store
    bool isExcel  = false; // game-data txt → a mod's excel folder

    // Literal-path (destPath) support. When dest==Literal, this holds the
    // validated relative template (possibly containing the {mod} token).
    // destAbsPath is resolved from it — immediately for non-{mod} paths, or
    // after the mod picker for {mod} paths (like excel).
    wstring literalTemplate;   // e.g. "mods/{mod}/data/global/ui/x.json"
    bool    needsMod = false;  // literalTemplate contains {mod} → needs a mod
};

// The parsed, resolved install plan for one zip. Produced by
// InspectPluginZip; consumed (after any prompts) by ExecutePluginPlan.
struct PluginInstallPlan {
    bool     ok            = false;  // false = inspection failed (see error)
    bool     hasManifest   = false;  // false → no-manifest path (see below)
    wstring  error;                  // human-readable failure reason

    wstring  tempDir;                // extract dir — caller/executor cleans up
    wstring  pluginName;             // manifest "name" (sanitized), for readme folder
    wstring  dllName;                // primary DLL filename (collision key)

    InstallScope scope = InstallScope::Global;

    vector<PluginFileOp> files;      // resolved moves (manifest path)

    // Collision: a plugin with this DLL is already installed at `scope`.
    bool     collision     = false;
    bool     hasConfig     = false;  // plan includes config files (picks the
                                     // 4-way overwrite prompt vs the 2-way)

    // No-manifest path: every file dumped to the plugins folder. `files`
    // is populated with Plugins-dest ops; `hasManifest` is false so the
    // caller shows the notice first.
    bool     excelFilesPresent = false; // any Excel-dest files (→ mod picker)

    // True if ANY file needs a mod chosen before it can be placed: excel
    // files, or Literal destPaths containing {mod}. On a global drop this
    // triggers the "which mod?" picker (one picker serves all of them).
    bool     needsModPicker = false;

    // Set during mod resolution if any {mpq} literal destPath resolved
    // against a PACKED (encrypted) .mpq file — you can't write inside one,
    // so the caller warns and those files are skipped.
    bool     mpqLiteralPacked = false;

    // Resolved plugins folder for this scope — used by ExecuteNoManifest
    // (no-manifest dump target) and available for reference.
    wstring  pluginsDir;
};

// Inspect a dropped zip WITHOUT writing anything to the install tree:
// extract to temp, parse plugin_info.json (if any), sanitize, resolve
// destinations, detect collision. Returns a plan. On a manifest, files
// are resolved to their dest folders; without one, files resolve to the
// plugins folder and hasManifest=false. The temp dir lives until
// ExecutePluginPlan or DiscardPluginPlan is called.
//
//   d2rPath   : the D2R install root (g_cfg.d2rPath).
//   scope     : Global or Mod (drop location decides).
//   selectedMod : mod folder name, required when scope==Mod (ignored for Global).
PluginInstallPlan InspectPluginZip(const wstring& zipPath,
                                   const wstring& d2rPath,
                                   InstallScope scope,
                                   const wstring& selectedMod);

// Resolve Excel-dest files against a chosen mod (the "which mod?" answer
// for a global drop). Rewrites each Excel op's destAbsPath to that mod's
// excel folder, or flags the encrypted-.mpq fallback. Call after the
// caller's mod picker returns, before ExecutePluginPlan.
//
// Returns true if the target mod uses an unpacked .mpq FOLDER (writable —
// files go to …/<mod>.mpq/data/global/excel/). Returns false if it's a
// packed .mpq FILE — the caller must show the "encrypted MPQ" popup, and
// the Excel ops are redirected to mods/<mod>/ instead.
bool ResolveExcelTargetMod(PluginInstallPlan& plan,
                           const wstring& d2rPath,
                           const wstring& modName);

// Which parts to overwrite on a collision. Chosen by the caller's prompt.
enum class OverwriteChoice {
    Cancel,        // do nothing, discard the plan
    DllOnly,       // replace DLL (+ readme), keep existing config
    ConfigOnly,    // replace config (+ readme), keep existing DLL
    DllAndConfig,  // replace everything (+ readme)
    All,           // no collision — install everything (default path)
};

// Execute a plan: move files from temp to their destinations per the
// overwrite choice, record the readme path in plugin_manifest.json, and
// clean up the temp dir. Returns true if all intended files landed.
// `choice` is All when there was no collision.
bool ExecutePluginPlan(PluginInstallPlan& plan, OverwriteChoice choice);

// Dump every extracted file into the plugins folder (the no-manifest
// path). Called after the caller shows the "all files to plugins" notice.
// Cleans up temp. Returns true on success.
bool ExecuteNoManifest(PluginInstallPlan& plan);

// Abandon a plan without installing: just delete the temp dir. Used when
// the user cancels at a prompt.
void DiscardPluginPlan(PluginInstallPlan& plan);


// ─────────────────────────────────────────────────────────────────────
//  Drop orchestration (Step 3)
// ─────────────────────────────────────────────────────────────────────

// What kind of zip was dropped, decided by which manifest it contains.
enum class ZipKind {
    Mod,          // has modinfo.json  → existing mod installer
    Plugin,       // has plugin_info.json → v1.6 plugin installer
    PatchBundle,  // no manifest but contains .json file(s) → patches folder
    Bare,         // neither manifest, no json → caller decides (main = failed mod)
    NotAZip,      // extension/extract failed
};

// Peek a zip to decide its kind WITHOUT committing to an install. Does a
// lightweight extract to temp and checks for the two manifests, then
// deletes the temp. Returns the kind; the caller routes accordingly.
// (The plugin path re-extracts in InspectPluginZip — a plugin zip is
//  small, so the double-extract is cheap and keeps the two installers
//  cleanly separate.)
ZipKind PeekZipKind(const wstring& zipPath);

// Optional allowlist: when a manifest-mode mod restricts which plugins may
// be installed, the caller passes the mod's sanctioned DLL/name list here.
// A dropped plugin must match one entry (by DLL filename OR name) or it's
// rejected with the "not authorized" popup. Empty `entries` with active=true
// means the mod ships NO plugins → nothing is allowed. active=false disables
// the check entirely (legacy / no-manifest mods).
struct PluginAllowlist {
    bool            active = false;    // is this mod manifest-restricted?
    vector<wstring> entries;           // sanctioned DLL filenames (+ any names)
    wstring         modName;           // for the rejection popup
    wstring         modAuthor;         // optional; changes the popup wording
};

// Callbacks the orchestrator uses for the UI prompts, so plugin_install
// stays free of window code. The caller (Angiris.cpp) supplies these.
struct PluginDropCallbacks {
    // Show "no plugin_info.json — all files go to plugins" notice. OK-only.
    void (*noManifestNotice)(void* ctx) = nullptr;
    // Ask which mod's excel folder to use; return the mod name, or empty
    // to cancel. `mods` is the launcher's mod list (display names).
    wstring (*pickExcelMod)(void* ctx, const vector<wstring>& mods) = nullptr;
    // Show the "encrypted .mpq — TXT placed in mods/<mod>/" notice. OK-only.
    void (*encryptedMpqNotice)(void* ctx, const wstring& modName) = nullptr;
    // Ask the overwrite choice. `hasConfig` picks the 4-way vs 2-way prompt.
    OverwriteChoice (*askOverwrite)(void* ctx, bool hasConfig,
                                    const wstring& pluginName) = nullptr;
    // Report a hard error (bad manifest, extract failed). OK-only.
    void (*errorNotice)(void* ctx, const wstring& message) = nullptr;
    // Allowlist rejection: the dropped plugin isn't sanctioned by the mod.
    // `pluginName` is the dropped plugin's display name (may be empty for a
    // no-manifest zip). modName/modAuthor drive the wording.
    void (*notAuthorized)(void* ctx, const wstring& pluginName,
                          const wstring& modName, const wstring& modAuthor) = nullptr;
    void* ctx = nullptr;   // opaque, passed back to each callback
};

// Full plugin-install flow for one zip: inspect → allowlist gate → prompt
// (via callbacks) → execute. Handles the manifest, no-manifest, collision,
// and excel cases. Returns true if something was installed. `mods` is the
// current mod list for the excel picker. `allow` enforces a manifest-mode
// mod's plugin allowlist (pass an inactive allowlist to skip the check).
bool HandlePluginDropZip(const wstring& zipPath,
                         const wstring& d2rPath,
                         InstallScope scope,
                         const wstring& selectedMod,
                         const vector<wstring>& mods,
                         const PluginDropCallbacks& cb,
                         const PluginAllowlist& allow);

// Install a BARE .json patch (no plugin_info.json, no manifest) directly to
// the patches folder at `scope`. Patches are open, self-documenting JSON, so
// they skip the manifest/readme machinery entirely. Still honors the mod
// allowlist (the patch filename must be sanctioned) and prompts on collision
// (Yes/No, with a .old backup). Returns true if installed.
//   allow  : manifest-mode mod allowlist (inactive to skip the gate).
bool HandleBarePatchDrop(const wstring& jsonPath,
                         const wstring& d2rPath,
                         InstallScope scope,
                         const wstring& selectedMod,
                         const PluginDropCallbacks& cb,
                         const PluginAllowlist& allow);

// Install a PATCH-BUNDLE zip: a manifest-less zip containing one or more
// .json files. Extracts ONLY the .json files (any other files are ignored)
// to the patches folder at `scope`. Each JSON is allowlist-gated and
// collision-prompted individually (an unlisted one is skipped with its
// notice; the rest still install). Returns the count installed.
int HandlePatchBundleZip(const wstring& zipPath,
                         const wstring& d2rPath,
                         InstallScope scope,
                         const wstring& selectedMod,
                         const PluginDropCallbacks& cb,
                         const PluginAllowlist& allow);
