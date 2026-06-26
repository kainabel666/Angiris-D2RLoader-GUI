// ═══════════════════════════════════════════════════════════════════════
//  fs_utils.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Filesystem helpers shared across the heavy mod-operation modules
//  (zip_install, save_backup, launcher_self_update). Existence checks,
//  temp-dir creation, recursive copy / delete, and the bsdtar shell-out
//  used to extract .zip archives.
//
//  Names retain the `ZI_` prefix on the existence checks (legacy from
//  when these lived inside the zip_install code block) so call sites
//  in Angiris.cpp don't need to be rewritten. A future pass can
//  rename them.
//
//  Depends on core (only for the std/Win32 surface via angiris_common).
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// Existence checks using GetFileAttributesW. Return false on missing
// path OR on the wrong kind (file when directory expected, etc.).
bool ZI_DirExists (const wstring& path);
bool ZI_FileExists(const wstring& path);

// Create a unique %TEMP%\angiris_install_<HEX> folder and return its
// absolute path. Returns empty on any failure (full disk, permissions,
// GUID generation failure). Used as the staging area for zip extracts
// and for launcher self-update unpacks before files get moved into
// their final location.
wstring MakeTempInstallDir();

// Recursive delete of an entire folder tree via SHFileOperationW.
// Silent — no UI, no confirmation prompts, no error dialogs. Safe to
// call on a non-existent path (no-op).
void DeleteFolderRecursive(const wstring& path);

// Mirror src into dst. If addMissing is true, files present only in
// src are added to dst; files already in dst with matching paths get
// overwritten. If addMissing is false, only files that exist in dst
// get overwritten; new src files are skipped. Returns true on full
// success — false if ANY copy failed (caller can check).
bool CopyTreeInto(const wstring& src, const wstring& dst, bool addMissing);

// Mirror src into dst, but skip any top-level entry whose name matches
// excludeName (case-sensitive). Used to back up a save folder while
// excluding its own "backups" subfolder (so we don't recursively
// back up backups). Returns true on full success.
bool CopyTreeExcept(const wstring& src, const wstring& dst,
                    const wchar_t* excludeName);

// Like CopyTreeInto but writes per-file diagnostics to a FILE* log
// stream (one line per file, "COPY OK : <path>" / "COPY FAIL : ...
// (err=N)"). Used by the launcher self-update so a partial overwrite
// failure is debuggable. If failCount is non-null, the running count
// of failed file copies is written there. Returns true on full
// success, false if any copy failed.
bool CopyTreeIntoLogged(const wstring& src, const wstring& dst,
                        FILE* logF, int* failCount);

// Invoke the Windows-bundled bsdtar to extract `zipPath` into
// `destDir`. The hidden-window flag suppresses any console flash.
// Optional `excludePattern` (e.g. L"*.ttf") is passed straight to
// bsdtar's --exclude flag, so matching entries are skipped at the
// extraction stage rather than copied and then deleted. Returns
// true iff tar.exe exited with code 0.
bool RunTarExtract(const wstring& zipPath, const wstring& destDir,
                   const wchar_t* excludePattern = nullptr);
