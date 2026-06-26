// ═══════════════════════════════════════════════════════════════════════
//  fs_utils.cpp — see fs_utils.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "fs_utils.h"

bool ZI_DirExists(const wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ZI_FileExists(const wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

wstring MakeTempInstallDir() {
    wchar_t base[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, base);
    if (n == 0 || n >= MAX_PATH) return L"";
    GUID g; CoCreateGuid(&g);
    wchar_t out[MAX_PATH];
    int len = swprintf_s(out, MAX_PATH,
                         L"%sangiris_install_%08lX_%04hX",
                         base, g.Data1, g.Data2);
    if (len <= 0) return L"";
    if (!CreateDirectoryW(out, nullptr)) return L"";
    return out;
}

void DeleteFolderRecursive(const wstring& path) {
    if (path.empty()) return;
    vector<wchar_t> from(path.begin(), path.end());
    from.push_back(0);
    from.push_back(0);
    SHFILEOPSTRUCTW op = {};
    op.wFunc  = FO_DELETE;
    op.pFrom  = from.data();
    op.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    SHFileOperationW(&op);
}

bool RunTarExtract(const wstring& zipPath, const wstring& destDir,
                   const wchar_t* excludePattern) {
    wchar_t winDir[MAX_PATH];
    if (GetWindowsDirectoryW(winDir, MAX_PATH) == 0) return false;
    wstring tarPath = wstring(winDir) + L"\\System32\\tar.exe";
    if (!ZI_FileExists(tarPath)) return false;

    wstring cmd = L"\"" + tarPath + L"\" -xf \"" + zipPath
                + L"\" -C \"" + destDir + L"\"";
    if (excludePattern && *excludePattern) {
        // bsdtar accepts --exclude=PATTERN as a single argv; CreateProcessW
        // doesn't glob-expand so `*.ttf` is passed literally to bsdtar,
        // which then matches it against each archive entry's path.
        cmd += L" \"--exclude=";
        cmd += excludePattern;
        cmd += L"\"";
    }

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    vector<wchar_t> cmdLine(cmd.begin(), cmd.end());
    cmdLine.push_back(0);

    if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &si, &pi)) {
        return false;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exitCode == 0;
}

bool CopyTreeInto(const wstring& src, const wstring& dst, bool addMissing) {
    WIN32_FIND_DATAW fd;
    wstring pattern = src + L"\\*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;   // empty src is OK
    bool ok = true;
    do {
        if (wcscmp(fd.cFileName, L".")  == 0) continue;
        if (wcscmp(fd.cFileName, L"..") == 0) continue;
        wstring s = src + L"\\" + fd.cFileName;
        wstring d = dst + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            bool dstHas = ZI_DirExists(d);
            if (!dstHas) {
                if (addMissing) {
                    CreateDirectoryW(d.c_str(), nullptr);
                    if (!CopyTreeInto(s, d, true)) ok = false;
                }
                // else (Update mode): destination doesn't have this dir,
                // skip its entire subtree.
            } else {
                if (!CopyTreeInto(s, d, addMissing)) ok = false;
            }
        } else {
            if (addMissing || ZI_FileExists(d)) {
                // CopyFileW with FALSE = overwrite existing
                if (!CopyFileW(s.c_str(), d.c_str(), FALSE)) ok = false;
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

bool CopyTreeExcept(const wstring& src, const wstring& dst,
                    const wchar_t* excludeName) {
    WIN32_FIND_DATAW fd;
    wstring pattern = src + L"\\*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;
    bool ok = true;
    do {
        if (wcscmp(fd.cFileName, L".")  == 0) continue;
        if (wcscmp(fd.cFileName, L"..") == 0) continue;
        if (excludeName &&
            _wcsicmp(fd.cFileName, excludeName) == 0) continue;
        wstring s = src + L"\\" + fd.cFileName;
        wstring d = dst + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CreateDirectoryW(d.c_str(), nullptr);
            // Recurse with the existing copy helper — exclusion only
            // applies at the top level, deeper trees copy normally.
            if (!CopyTreeInto(s, d, true)) ok = false;
        } else {
            if (!CopyFileW(s.c_str(), d.c_str(), FALSE)) ok = false;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

bool CopyTreeIntoLogged(const wstring& src, const wstring& dst,
                        FILE* logF, int* failCount) {
    auto WLOG = [&](const wchar_t* fmt, ...) {
        if (!logF) return;
        va_list ap;
        va_start(ap, fmt);
        vfwprintf(logF, fmt, ap);
        va_end(ap);
        fwprintf(logF, L"\n");
        fflush(logF);
    };

    WIN32_FIND_DATAW fd;
    wstring pattern = src + L"\\*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;   // empty src is OK
    bool ok = true;
    do {
        if (wcscmp(fd.cFileName, L".")  == 0) continue;
        if (wcscmp(fd.cFileName, L"..") == 0) continue;
        wstring s = src + L"\\" + fd.cFileName;
        wstring d = dst + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!ZI_DirExists(d)) CreateDirectoryW(d.c_str(), nullptr);
            if (!CopyTreeIntoLogged(s, d, logF, failCount)) ok = false;
        } else {
            if (CopyFileW(s.c_str(), d.c_str(), FALSE)) {
                WLOG(L"  COPY OK   : %ls", d.c_str());
            } else {
                DWORD err = GetLastError();
                WLOG(L"  COPY FAIL : %ls  (err=%lu)", d.c_str(), err);
                if (failCount) (*failCount)++;
                ok = false;
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}
