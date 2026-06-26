// ═══════════════════════════════════════════════════════════════════════
//  save_backup.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Per-mod save folder snapshots. D2R writes each mod's character +
//  stash data to
//    %USERPROFILE%\Saved Games\Diablo II Resurrected\Mods\<savepath>\
//  where <savepath> is the value of the "savepath" field in the
//  mod's modinfo.json. A backup snapshot is a full copy of that
//  folder into:
//    <savefolder>\backups\<YYYY-MM-DD_HHMMSS>\
//  keeping the backups co-located with the data they protect.
//
//  Triggered manually (right-click context menu) and automatically
//  before any Overwrite install (where the destination's existing
//  mod folder is about to be wiped) and before any Uninstall.
//  Rotation keeps the most recent five snapshots.
//
//  Depends on:
//    fs_utils — ZI_DirExists, CopyTreeExcept, DeleteFolderRecursive
//    core     — ReadTextFile, JsonStr (for BackupModSavesFromModinfo)
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// Create a timestamped snapshot of the mod's save folder. The
// folder is found at %USERPROFILE%\Saved Games\Diablo II Resurrected\
// Mods\<savePath>. Returns true on success and writes the new
// backup folder's absolute path into *outBackupDir (caller can use
// for UI feedback). Returns false on:
//   • Empty savePath
//   • Missing USERPROFILE environment variable
//   • Save folder doesn't exist (nothing to back up)
//   • Couldn't create the backups subfolder
//   • Copy operation failed
// Rotates so only the 5 most recent snapshots survive — older ones
// are deleted automatically.
bool BackupModSavesByPath(const wstring& savePath, wstring* outBackupDir);

// Convenience wrapper: read savepath out of a modinfo.json on disk
// and back up that mod's saves. Used by the zip installer's
// auto-backup-before-Overwrite path, where the existing mod is about
// to be deleted and we want its saves protected first. Returns true
// on full success.
bool BackupModSavesFromModinfo(const wstring& modinfoPath,
                                wstring* outBackupDir);
