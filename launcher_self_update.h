// ═══════════════════════════════════════════════════════════════════════
//  launcher_self_update.h
// ═══════════════════════════════════════════════════════════════════════
//
//  In-place launcher self-update. The trick that makes it work without
//  a separate updater binary:
//
//    1. Make temp dir, download release zip, extract via tar.exe.
//    2. BFS for Angiris.exe in the extracted tree to determine the
//       release root.
//    3. Rename the running exe to "<path>.exe.old". Windows lets you
//       rename a running executable even though it forbids deleting
//       or overwriting one.
//    4. CopyTreeInto(releaseRoot → installDir). The new Angiris.exe
//       lands at the original path; updated asset files overwrite
//       their old versions.
//    5. Restart button click: spawn new exe + PostMessage WM_CLOSE
//       on the (hidden) main window to exit cleanly.
//    6. On next startup, the new exe's CleanupLauncherOldExe deletes
//       the .old leftover (with a deferred-retry path for the case
//       where the prior process is still releasing its image file).
//
//  This module covers:
//    • Startup check worker (KickoffLauncherUpdateCheck)
//    • Install flow (StartLauncherUpdateInstall)
//    • Minimal "Downloading → Updating → Complete" popup
//    • .old-file cleanup with deferred-retry state machine
//
//  The "Update available?" themed prompt dialog (the one with the
//  changelog + Skip/Update/Ignore buttons) stays in Angiris.cpp —
//  it's themed UI code with deep paint/font/asset dependencies that
//  will move with the other dialogs in Phase 6.
//
//  Depends on:
//    core, http, version, fs_utils
//    + DestroyAssetCache() and UnloadFonts() (still in Angiris.cpp
//      until Phase 4, forward-declared in this module's .cpp)
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// ── GitHub release coordinates ───────────────────────────────────────
//
// Where the self-update checker looks for new releases. These are
// project-wide constants (not per-instance config) so they live with
// the rest of the launcher-version data.
constexpr const wchar_t* LAUNCHER_GITHUB_OWNER = L"kainabel666";
constexpr const wchar_t* LAUNCHER_GITHUB_REPO  = L"Angiris-D2RLoader-GUI";

// Current launcher version. Bumped each release; the self-update
// check compares the remote GitHub release tag against this value.
// Also surfaced by the version label paint and the About dialog.
// `inline constexpr` (C++17 inline variable) so every TU that
// includes this header sees the same definition without an ODR
// violation — no separate .cpp definition needed.
inline constexpr const wchar_t* LAUNCHER_VERSION = L"1.2";

// ── Worker → UI protocol ─────────────────────────────────────────────

// Posted to g_hwMain by the startup check worker when a newer tagged
// release exists on GitHub. MainProc reacts by showing the themed
// "Update available" dialog (LauncherUpdateDlgProc, still in
// Angiris.cpp). Payload is read from the file-scope globals
// below that the worker fills before posting.
constexpr UINT MSG_LAUNCHER_UPDATE_AVAILABLE = WM_USER + 50;

// Sent (blocking) by the install worker to drive the popup forward
// through its three statuses. wp = new status:
//   1 = Downloading
//   2 = Updating
//   3 = Complete
// UI thread updates g_lupopupStatus and repaints the popup window.
constexpr UINT MSG_LUPOPUP_STATUS            = WM_USER + 51;

// ── Timer ID ─────────────────────────────────────────────────────────

// Fires on g_hwMain every 2 s while a pending .old file refuses to
// delete (typically the prior launcher process is still releasing
// its image). The MainProc WM_TIMER handler reads
// g_pendingOldExeDelete and tries DeleteFileW; gives up after ~30 s
// and schedules MOVEFILE_DELAY_UNTIL_REBOOT as the last resort.
constexpr UINT IDT_CLEANUP_OLD_EXE = 9103;

// ── State (extern; defined in launcher_self_update.cpp) ──────────────
//
// Most of these are written by the workers and read by the UI thread.
// All UI reads happen on the message thread (no extra synchronization
// needed — workers post a message after writing, so paint sees the
// new value through the post barrier).

// Tag string of the latest GitHub release (e.g. L"v1.3"). Empty until
// the startup check completes successfully.
extern wstring g_launcherUpdateLatestTag;

// First .zip asset URL on that release. Empty if no .zip is attached
// — StartLauncherUpdateInstall handles that by opening the browser
// on the Releases page instead of running the in-place flow.
extern wstring g_launcherUpdateDownloadUrl;

// True once the startup check confirms remote > local. UI reads to
// drive the version-label glow. Skipped-version state doesn't gate
// this flag — we want the glow to appear even when the user has
// previously asked to Skip, so the label itself can act as an
// escape hatch to re-check.
extern bool g_launcherUpdateAvailable;

// One-shot bypass for the skipped-version gate. When the user
// clicks the version label, we re-run the check and want the
// dialog to appear even if they previously chose Skip Version
// on this exact tag. MainProc clears this back to false after
// the prompt fires.
extern bool g_forceUpdatePrompt;

// Queue state for the deferred .old cleanup. Populated by
// CleanupLauncherOldExe when the up-front fast-path attempt fails.
// MainProc's IDT_CLEANUP_OLD_EXE timer handler reads / mutates these
// directly (they're naturally single-threaded since only the UI
// thread services WM_TIMER). Empty pending = no cleanup pending.
extern wstring g_pendingOldExeDelete;
extern int     g_cleanupOldExeAttempts;

// ── Entry points ─────────────────────────────────────────────────────

// Fire the startup background check. Spawns a detached thread,
// returns immediately. Posts MSG_LAUNCHER_UPDATE_AVAILABLE to
// g_hwMain on success. Safe to call again after the previous check
// completed; in-flight calls are deduped via an internal atomic.
void KickoffLauncherUpdateCheck();

// Try to delete Angiris.exe.old. Cheap no-op when there isn't one.
// If the file exists but can't be deleted right now (prior process
// still releasing it), queues the path into g_pendingOldExeDelete
// so the deferred retry timer can keep trying. Called once at
// wWinMain entry.
void CleanupLauncherOldExe();

// Arm the deferred .old cleanup timer if a pending delete is
// queued. Called from wWinMain after g_hwMain is created (timers
// need a window to deliver WM_TIMER to). No-op when
// CleanupLauncherOldExe already succeeded synchronously or there
// was no .old to begin with.
void StartDeferredOldExeCleanup();

// Called by MainProc's IDT_CLEANUP_OLD_EXE timer when the retry
// cap is reached. Writes a one-line entry to
// assets\last_update_install.log explaining why cleanup gave up
// and that MoveFileEx was used as the fallback. Caller does the
// actual MoveFileEx; this helper just logs.
void LogCleanupOldExeGaveUp(const wstring& oldExe, DWORD lastErr);

// Kick off an in-place update install. Hides the main window,
// tears down the GDI+ asset cache and font collection (releasing
// their file handles), shows the minimal popup, and spawns the
// install worker. Returns immediately. Called from MainProc when
// the user clicks Update in the themed prompt dialog.
void StartLauncherUpdateInstall(HWND parent);

// Register the popup window class. Idempotent. Called by
// StartLauncherUpdateInstall before the first show. Exposed
// publicly only so the MainProc cleanup code can also de-register
// it on shutdown if needed (currently no-op).
void EnsureLauncherUpdatePopupClass();
