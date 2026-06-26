// ═══════════════════════════════════════════════════════════════════════
//  zip_install.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Drag-and-drop zip mod installer. The user drags one or more .zip
//  archives onto the launcher window; each is extracted, validated
//  for a modinfo.json, deduped against existing mods (Update /
//  Overwrite / Cancel dialog if a folder already exists), and
//  installed into <D2R>\mods\<modName>\.
//
//  Architecture: WM_DROPFILES → EnqueueZipsForInstall → spawn the
//  ZipInstallWorker thread if not already running. The worker pops
//  zips one at a time and drives ProcessOneZip; UI thread receives
//  MSG_ZIP_* messages to show dialogs / update progress.
//
//  Dialog *implementations* live in Angiris.cpp (UI code with deep
//  paint/font/asset dependencies). This module exposes only the
//  message protocol and the worker. MainProc routes the messages
//  to those local dialog functions.
//
//  Depends on:
//    fs_utils    — ZI_DirExists/FileExists, MakeTempInstallDir,
//                  DeleteFolderRecursive, CopyTreeInto, RunTarExtract
//    save_backup — BackupModSavesFromModinfo (pre-Overwrite snapshot)
//    config      — g_cfg.d2rPath
//    core        — g_hwMain
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// ── Worker → UI protocol ─────────────────────────────────────────────
//
// Some are blocking (SendMessage), some are posted (PostMessage). See
// each message's individual comment.

// Sent (blocking) when ProcessOneZip detects an existing mod folder
// with the same name. UI shows the Update / Overwrite / Cancel
// dialog. LPARAM = ConflictDialogParam*; .choice is the out-param
// (0 = cancel, 1 = update, 2 = overwrite).
constexpr UINT MSG_ZIP_CONFLICT_DIALOG = WM_USER + 30;

// Posted when the worker finishes draining the queue (final cleanup
// after MSG_ZIP_PROGRESS_HIDE). UI doesn't currently react beyond
// repainting; kept distinct from PROGRESS_HIDE for future use.
constexpr UINT MSG_ZIP_QUEUE_DONE      = WM_USER + 31;

// Sent (blocking) when a drop comes in and g_cfg.d2rPath is empty.
// UI shows the "Set Path" dialog. Returns 0 = cancel (worker
// clears the whole queue), 1 = path set (worker continues with the
// same zip).
constexpr UINT MSG_ZIP_NEED_PATH       = WM_USER + 32;

// Sent (blocking) when an extracted archive has no modinfo.json.
// LPARAM = wstring* (zip filename for the dialog body). UI shows the
// error and returns 0. Worker continues with the next zip.
constexpr UINT MSG_ZIP_NO_MODINFO      = WM_USER + 33;

// Sent (blocking) before the first stage update so the dialog is on
// screen before any updates come in. No payload.
constexpr UINT MSG_ZIP_PROGRESS_SHOW   = WM_USER + 34;

// Sent (blocking) each time the worker advances a stage. LPARAM =
// ProgressUpdate*. UI thread copies fields into the dialog state +
// invalidates + returns. Blocking ensures the worker can't pile up
// updates faster than the UI can paint them.
constexpr UINT MSG_ZIP_PROGRESS_UPDATE = WM_USER + 35;

// Posted (non-blocking) when the worker is done with the entire
// queue. UI thread closes the dialog.
constexpr UINT MSG_ZIP_PROGRESS_HIDE   = WM_USER + 36;

// ── Payload types ────────────────────────────────────────────────────

struct ConflictDialogParam {
    wstring modName;   // shown in the dialog title/body
    int     choice;    // out: 0=cancel, 1=update, 2=overwrite
};

struct ProgressUpdate {
    int     stage;        // 0..4 — selects progress_bar_<stage>.png
    int     zipIdx;       // 1-based position in queue
    int     zipTotal;     // total queued zips at start of run
    wstring zipName;      // current archive filename (no directory)
    wstring stageLabel;   // "Extracting archive...", etc.
};

// ── Entry points ─────────────────────────────────────────────────────

// Append zip paths to the install queue, starting the worker thread
// if it's not already running. Called from the WM_DROPFILES handler
// in MainProc. Filters to .zip extension (case-insensitive) — other
// file types in the drop are silently ignored. Returns the count of
// archives actually queued.
int EnqueueZipsForInstall(const vector<wstring>& paths);
