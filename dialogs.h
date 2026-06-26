// ═══════════════════════════════════════════════════════════════════════
//  dialogs.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Themed modal dialogs spawned by various launcher flows. Every
//  dialog is a stand-alone WS_POPUP window with its own message
//  pump (run inside the Show* function); the parent is gated with
//  EnableWindow(FALSE) for the duration and re-enabled on close.
//
//  Six dialogs:
//
//    Conflict        — "Mod folder already exists" when a dropped zip
//                      collides with an existing mod folder. Three
//                      choices: Update / Overwrite / Cancel.
//
//    SetPath         — "Diablo II: Resurrected not found" — invites
//                      the user to point at a D2R install. Calls
//                      SHBrowseForFolder on Set Path and writes
//                      g_cfg.d2rPath on success.
//
//    NoModInfo       — "<zip> doesn't appear to be a valid mod" —
//                      shown when the dropped archive has no
//                      modinfo.json. Single OK button.
//
//    Uninstall       — "Uninstall <mod>?" confirmation prior to
//                      deleting the mod folder. Cancel / Delete.
//
//    LauncherUpdate  — "Launcher update available (vX → vY)" — shown
//                      at startup when a newer GitHub release is
//                      detected. Update / Skip Version / Ignore.
//
//    Progress        — Long-running zip extraction status. Async:
//                      ShowProgressDialog returns immediately;
//                      UpdateProgressDialog is called from the worker
//                      via MSG_ZIP_PROGRESS_UPDATE; HideProgressDialog
//                      tears down when the worker finishes.
//
//  All dialogs use bg_stone.png + gold borders + NexusUpdate-styled
//  buttons created via MkStdBtn (so they participate in the main
//  window's WM_DRAWITEM owner-draw pipeline).
//
//  Depends on assets, fonts, colors, scaling, layout, core, config,
//  zip_install (ProgressUpdate), control_ids, mod_types,
//  launcher_self_update, buttons.
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"
#include "zip_install.h"   // for ProgressUpdate

// ── Conflict / install dialogs ───────────────────────────────────────

// "Mod folder already exists" dialog. Returns:
//   1 = Update    (keep folder, overwrite only the archive's files)
//   2 = Overwrite (wipe folder, extract fresh)
//   0 = Cancel    (leave folder untouched, skip the zip)
int ShowConflictDialog(HWND parent, const wstring& modName);

// "<zip> doesn't appear to be a valid mod" notice. Single OK button.
// Always returns void — there's only one acknowledgement.
void ShowNoModInfoDialog(HWND parent, const wstring& zipName);

// "Diablo II: Resurrected not found" picker. On Set Path the dialog
// itself runs SHBrowseForFolder and writes g_cfg.d2rPath on success.
// Returns:
//   1 = path set (caller should resume any pending operation)
//   0 = Cancel / Esc / picker was itself cancelled
int ShowSetPathDialog(HWND parent);

// "Uninstall <mod>?" confirmation. Returns:
//   1 = Delete
//   0 = Cancel
int ShowUninstallConfirmDialog(HWND parent, const wstring& modName);

// "Launcher update available (vX → vY)" dialog. Returns:
//   1 = Update (caller should kick off StartLauncherUpdateInstall)
//   2 = Skip Version (remember tag in g_skippedLauncherVersion)
//   0 = Ignore (no action; will prompt again next launch)
int ShowLauncherUpdateDialog(HWND parent, const wstring& latestTag);

// ── Progress dialog (async, non-modal-but-modal-looking) ─────────────

// Show the long-running progress dialog. Returns the dialog HWND
// immediately — caller continues spawning the worker thread. The
// dialog stays up until HideProgressDialog is called.
//
// Internally tracks "active progress dialog" so subsequent dialogs
// (Conflict, SetPath, NoModInfo) can re-parent to this dialog
// instead of g_hwMain, keeping the modal stacking sensible.
HWND ShowProgressDialog(HWND parent);

// Update the progress dialog's stage label / zip index / archive
// name from a worker-thread ProgressUpdate. Safe to call from the
// UI thread only (typically forwarded via MSG_ZIP_PROGRESS_UPDATE).
void UpdateProgressDialog(const ProgressUpdate& p);

// Tear down the progress dialog. Called by the worker-finished
// MSG_ZIP_PROGRESS_HIDE path. Idempotent — safe to call when no
// dialog is up.
void HideProgressDialog();

// Accessor for the currently-active progress dialog HWND, or
// nullptr if none. Used by Angiris.cpp to pick a sensible parent
// for Conflict / SetPath / NoModInfo dialogs spawned mid-flow.
HWND GetProgressDialog();
