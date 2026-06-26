// ═══════════════════════════════════════════════════════════════════════
//  save_backup.cpp — see save_backup.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "save_backup.h"
#include "fs_utils.h"   // ZI_DirExists, CopyTreeExcept, DeleteFolderRecursive
#include "core.h"       // ReadTextFile, JsonStr

// Keep the most recent `keep` timestamped subfolders in `backupsRoot`;
// delete the rest. Names follow the YYYY-MM-DD_HHMMSS pattern which
// sorts correctly as plain strings, so a lexicographic sort puts the
// oldest at the front of the list.
//
// File-internal — only BackupModSavesByPath needs this.
static void RotateBackups(const wstring& backupsRoot, int keep) {
    vector<wstring> entries;
    WIN32_FIND_DATAW fd;
    wstring pattern = backupsRoot + L"\\*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".")  == 0) continue;
        if (wcscmp(fd.cFileName, L"..") == 0) continue;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        entries.emplace_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    sort(entries.begin(), entries.end());
    while ((int)entries.size() > keep) {
        wstring victim = backupsRoot + L"\\" + entries.front();
        DeleteFolderRecursive(victim);
        entries.erase(entries.begin());
    }
}

// Resolve <savedGames>\Diablo II Resurrected\Mods\<savePath> for the
// given savepath. Uses %USERPROFILE%\Saved Games as the base — that's
// where D2R itself writes, and matches the location across every
// Windows version we care about. Returns empty if USERPROFILE isn't
// set (shouldn't happen on a real Windows session).
static wstring ResolveModSaveFolder(const wstring& savePath) {
    if (savePath.empty()) return L"";
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    return wstring(buf) + L"\\Saved Games\\Diablo II Resurrected\\Mods\\"
                        + savePath;
}

bool BackupModSavesByPath(const wstring& savePath,
                          wstring* outBackupDir) {
    if (outBackupDir) outBackupDir->clear();
    wstring saveDir = ResolveModSaveFolder(savePath);
    if (saveDir.empty()) return false;
    if (!ZI_DirExists(saveDir)) return false;   // no saves to back up

    wstring backupsRoot = saveDir + L"\\backups";
    if (!ZI_DirExists(backupsRoot)) {
        if (!CreateDirectoryW(backupsRoot.c_str(), nullptr)) return false;
    }

    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t ts[64];
    swprintf_s(ts, 64, L"%04d-%02d-%02d_%02d%02d%02d",
               t.wYear, t.wMonth, t.wDay,
               t.wHour, t.wMinute, t.wSecond);
    wstring backupDir = backupsRoot + L"\\" + ts;
    if (!CreateDirectoryW(backupDir.c_str(), nullptr)) {
        // Almost certainly a duplicate timestamp (two backups within
        // the same second). Fall back by appending an index suffix.
        for (int i = 1; i < 100; ++i) {
            wchar_t suffix[16]; swprintf_s(suffix, 16, L"_%02d", i);
            backupDir = backupsRoot + L"\\" + ts + suffix;
            if (CreateDirectoryW(backupDir.c_str(), nullptr)) goto made;
        }
        return false;
    made:;
    }

    bool ok = CopyTreeExcept(saveDir, backupDir, L"backups");
    RotateBackups(backupsRoot, 5);
    if (ok && outBackupDir) *outBackupDir = backupDir;
    return ok;
}

bool BackupModSavesFromModinfo(const wstring& modinfoPath,
                               wstring* outBackupDir) {
    if (outBackupDir) outBackupDir->clear();
    wstring json = ReadTextFile(modinfoPath);
    if (json.empty()) return false;
    wstring sp = JsonStr(json, L"savepath");
    if (sp.empty()) return false;
    return BackupModSavesByPath(sp, outBackupDir);
}
