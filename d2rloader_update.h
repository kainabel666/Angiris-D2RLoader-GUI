// ═══════════════════════════════════════════════════════════════════════
//  d2rloader_update.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Downloads the latest D2RLoader release from d2rloader.net and installs
//  it into the configured D2R directory. Existing user files are handled
//  carefully:
//
//    * D2RLoader.toml is NEVER overwritten. Instead the newly-shipped
//      toml is parsed and any keys/sections not already present in the
//      user's toml are appended (so new loader features get their config
//      keys without clobbering the user's existing values).
//
//  The download runs on a background thread; progress + completion are
//  reported back to the caller's window via posted messages.

#pragma once

#include "angiris_common.h"

// Posted to the target window as the install progresses / finishes.
//   wParam = stage code (see D2RLoaderInstallStage)
//   lParam = unused
#define MSG_D2RLOADER_INSTALL_PROGRESS (WM_APP + 40)
#define MSG_D2RLOADER_INSTALL_DONE     (WM_APP + 41)

enum D2RLoaderInstallStage {
    D2RL_STAGE_DOWNLOADING = 1,
    D2RL_STAGE_VERIFYING   = 2,
    D2RL_STAGE_EXTRACTING  = 3,
    D2RL_STAGE_INSTALLING  = 4,
    D2RL_STAGE_MERGING     = 5,
};

// Result codes delivered with MSG_D2RLOADER_INSTALL_DONE (in wParam).
enum D2RLoaderInstallResult {
    D2RL_OK            = 0,
    D2RL_ERR_NO_PATH   = 1,   // D2R path not configured
    D2RL_ERR_DOWNLOAD  = 2,
    D2RL_ERR_EXTRACT   = 3,
    D2RL_ERR_COPY      = 4,
    D2RL_ERR_BUSY      = 5,   // an install is already running
    D2RL_ERR_CHECKSUM  = 6,   // download didn't match the published hash
};

// Kick off the download + install on a background thread. `notifyHwnd`
// receives the progress/done messages above. Returns immediately; if an
// install is already in flight it posts MSG_D2RLOADER_INSTALL_DONE with
// D2RL_ERR_BUSY and does nothing else.
void StartD2RLoaderDownloadInstall(HWND notifyHwnd);

// Lowercase hex SHA-256 of a file, or empty on failure. Shared with the
// repository installer, which verifies Extension Hub downloads against
// the hash the hub publishes.
wstring ComputeFileSha256(const wstring& path);

// ── Update availability check ────────────────────────────────────────
//
// Posted to the target window when the background version check finishes
// and a newer D2RLoader is available on d2rloader.net than the one
// installed. Not posted otherwise (up-to-date, not installed, or the
// check failed). The main window uses this to light up a gold
// "D2RLoader Update Available" affordance under the Exit button.
#define MSG_D2RLOADER_UPDATE_AVAILABLE (WM_APP + 42)

// Set true by the check worker when a newer version is found; read by
// the main window's paint + hit-test. Also exposes the parsed latest
// version string for display/logging.
extern bool    g_d2rloaderUpdateAvailable;
extern wstring g_d2rloaderLatestVersion;

// Kick off a background check: fetch d2rloader.net, scrape the latest
// version, compare against the installed D2RLoader.exe. On finding a
// newer version, sets g_d2rloaderUpdateAvailable and posts
// MSG_D2RLOADER_UPDATE_AVAILABLE to notifyHwnd. Safe to call once at
// startup. No-op if a check is already running.
void KickoffD2RLoaderUpdateCheck(HWND notifyHwnd);

// Read the file-version resource ("x.y.z") from the installed
// D2RLoader.exe, or empty if not found / no version stamp. Shared with
// the About modal so both show a consistent installed version.
wstring GetInstalledD2RLoaderVersion();

// True while a background install is running (so the UI can disable the
// download button / show a spinner).
bool IsD2RLoaderInstallRunning();

// True if D2R.exe or D2RLoader.exe is currently running. The caller
// should warn + block the install when this is true, since a running
// loader/game holds file locks that make the copy fail partway.
bool IsD2RLoaderRunning();

// Force-terminate any running D2R.exe / D2RLoader.exe processes. Used by
// the About modal's "Close Now" option so the user doesn't have to
// alt-tab out and close the game manually before updating. Returns the
// number of processes it asked to terminate. Note: termination is not
// instantaneous — the caller should give the OS a moment (and re-check
// IsD2RLoaderRunning) before starting the install.
int TerminateD2RLoaderProcesses();
