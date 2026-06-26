// ═══════════════════════════════════════════════════════════════════════
//  zip_install.cpp — see zip_install.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "zip_install.h"
#include "fs_utils.h"        // ZI_DirExists, ZI_FileExists, MakeTempInstallDir,
                             // DeleteFolderRecursive, CopyTreeInto, RunTarExtract
#include "save_backup.h"     // BackupModSavesFromModinfo
#include "core.h"            // g_hwMain, JsonStr
#include "config.h"          // g_cfg.d2rPath

// ── Queue state (private) ────────────────────────────────────────────
//
// File-internal — only EnqueueZipsForInstall and ZipInstallWorker
// touch the queue. The mutex serializes the producer/consumer
// access; g_zipWorkerRunning is an atomic CAS flag used so duplicate
// drops don't spawn duplicate worker threads.

static std::mutex      g_zipQueueMutex;
static vector<wstring> g_zipQueue;     // protected by g_zipQueueMutex
static atomic<bool>    g_zipWorkerRunning(false);

// ── Filename / name-sanitization helpers (private) ───────────────────

// Extract the filename portion of a path — used for the progress
// dialog body and the no-modinfo error message.
static wstring BaseName(const wstring& path) {
    size_t s = path.find_last_of(L"\\/");
    return (s == wstring::npos) ? path : path.substr(s + 1);
}

// Strip characters Windows refuses in filenames. Used on the
// modinfo's "name" field before turning it into a folder name under
// <D2R>\mods\. Also trims trailing space/period (which Windows
// strips silently on create, leading to mismatches with the user's
// expectation).
static wstring SanitizeModName(const wstring& name) {
    static const wchar_t* bad = L"<>:\"/\\|?*";
    wstring out;
    out.reserve(name.size());
    for (wchar_t c : name) {
        if (c < 0x20) continue;
        bool isBad = false;
        for (const wchar_t* p = bad; *p; ++p) if (*p == c) { isBad = true; break; }
        if (!isBad) out.push_back(c);
    }
    while (!out.empty() && (out.back() == L' ' || out.back() == L'.')) out.pop_back();
    return out;
}

// ── modinfo.json discovery (private) ─────────────────────────────────

// BFS for the shallowest modinfo.json in a tree. Picking shallowest
// handles archives that nest the mod folder one level deep (a very
// common layout — the zip contains MyMod/modinfo.json, MyMod/data/...).
static wstring FindModinfoJson(const wstring& root) {
    vector<wstring> queue;
    queue.push_back(root);
    size_t qi = 0;
    while (qi < queue.size()) {
        wstring dir = queue[qi++];
        WIN32_FIND_DATAW fd;
        wstring pattern = dir + L"\\*";
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        wstring foundFile;
        do {
            if (wcscmp(fd.cFileName, L".")  == 0) continue;
            if (wcscmp(fd.cFileName, L"..") == 0) continue;
            wstring full = dir + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                queue.push_back(full);
            } else if (_wcsicmp(fd.cFileName, L"modinfo.json") == 0) {
                foundFile = full;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        if (!foundFile.empty()) return foundFile;
    }
    return L"";
}

// Quick read of "name" (then "title" as fallback) from a UTF-8
// modinfo.json. Inlined UTF-8 read here rather than via core's
// ReadTextFile because the function predates that helper — both
// produce the same result.
static wstring ReadModNameFromInfo(const wstring& modinfoPath) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, modinfoPath.c_str(), L"rb") != 0 || !f) return L"";
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 10 * 1024 * 1024) { fclose(f); return L""; }
    string utf8(sz, 0);
    size_t got = fread(utf8.data(), 1, sz, f);
    fclose(f);
    utf8.resize(got);
    int wn = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                                 nullptr, 0);
    if (wn <= 0) return L"";
    wstring j(wn, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                        j.data(), wn);
    wstring name = JsonStr(j, L"name");
    if (name.empty()) name = JsonStr(j, L"title");
    return name;
}

// ── Progress messaging (private) ─────────────────────────────────────

// Synchronous progress push to the UI thread. Wraps the SendMessage
// so the worker's call sites stay readable. The UI thread updates
// the progress globals + repaints + returns; the SendMessage blocks
// until the new stage is on screen.
static void PushProgress(int stage, int zipIdx, int zipTotal,
                         const wstring& zipName,
                         const wchar_t* stageLabel) {
    if (!g_hwMain) return;
    ProgressUpdate p;
    p.stage      = stage;
    p.zipIdx     = zipIdx;
    p.zipTotal   = zipTotal;
    p.zipName    = zipName;
    p.stageLabel = stageLabel ? stageLabel : L"";
    SendMessageW(g_hwMain, MSG_ZIP_PROGRESS_UPDATE, 0, (LPARAM)&p);
}

// ── Per-zip processor (private) ──────────────────────────────────────

static void ProcessOneZip(const wstring& zipPath, int zipIdx, int zipTotal) {
    wstring zipBase = BaseName(zipPath);

    // Stage 0 — starting. Renders an empty progress bar.
    PushProgress(0, zipIdx, zipTotal, zipBase, L"Starting...");

    // If D2R path isn't set, ask the user. They can either set it now
    // (worker continues with this same zip) or cancel — in which case
    // we clear the rest of the queue too. With no D2R path, every
    // remaining zip would just hit the same dialog; asking again
    // n − 1 times for the same answer would feel hostile.
    if (g_cfg.d2rPath.empty()) {
        int got = (int)SendMessageW(g_hwMain, MSG_ZIP_NEED_PATH, 0, 0);
        if (got != 1) {
            std::lock_guard<std::mutex> lk(g_zipQueueMutex);
            g_zipQueue.clear();
            return;
        }
        // If we get here, g_cfg.d2rPath is populated.
    }
    if (!ZI_DirExists(g_cfg.d2rPath + L"\\mods")) {
        CreateDirectoryW((g_cfg.d2rPath + L"\\mods").c_str(), nullptr);
    }

    PushProgress(1, zipIdx, zipTotal, zipBase, L"Extracting archive...");

    wstring tmp = MakeTempInstallDir();
    if (tmp.empty()) return;

    if (!RunTarExtract(zipPath, tmp)) {
        DeleteFolderRecursive(tmp);
        return;
    }

    PushProgress(2, zipIdx, zipTotal, zipBase, L"Locating mod info...");

    wstring modinfoPath = FindModinfoJson(tmp);
    if (modinfoPath.empty()) {
        // No modinfo.json — surface the error so the user knows what
        // went wrong. SendMessage so it's modal over the progress
        // dialog. Worker continues with the next zip when the user
        // clicks OK.
        SendMessageW(g_hwMain, MSG_ZIP_NO_MODINFO, 0, (LPARAM)&zipBase);
        DeleteFolderRecursive(tmp);
        return;
    }

    size_t slash = modinfoPath.find_last_of(L"\\/");
    wstring modRoot = (slash == wstring::npos) ? tmp
                                               : modinfoPath.substr(0, slash);

    wstring modName = ReadModNameFromInfo(modinfoPath);
    if (modName.empty()) {
        size_t s2 = modRoot.find_last_of(L"\\/");
        if (s2 != wstring::npos) modName = modRoot.substr(s2 + 1);
    }
    modName = SanitizeModName(modName);
    if (modName.empty()) {
        DeleteFolderRecursive(tmp);
        return;
    }

    wstring destDir = g_cfg.d2rPath + L"\\mods\\" + modName;

    PushProgress(3, zipIdx, zipTotal, zipBase, L"Installing files...");

    if (ZI_DirExists(destDir)) {
        ConflictDialogParam p;
        p.modName = modName;
        p.choice  = 0;
        SendMessageW(g_hwMain, MSG_ZIP_CONFLICT_DIALOG, 0, (LPARAM)&p);

        if (p.choice == 0) {
            DeleteFolderRecursive(tmp);
            return;
        }
        if (p.choice == 2) {
            // Overwrite — wipe and reinstall. But first: back up the
            // existing mod's save folder so the user doesn't lose their
            // characters / stash / configs to a one-click oops. We
            // read savepath from the OLD mod's modinfo.json on disk
            // (it's about to be deleted, so this is the last chance).
            // Silent on failure — backup is a safety net, not a
            // blocker; if it can't run (no save folder yet, no
            // permissions, etc.) the user-requested overwrite still
            // proceeds.
            wstring oldModinfo = destDir + L"\\modinfo.json";
            if (ZI_FileExists(oldModinfo)) {
                wstring unused;
                BackupModSavesFromModinfo(oldModinfo, &unused);
            }
            DeleteFolderRecursive(destDir);
            CreateDirectoryW(destDir.c_str(), nullptr);
            CopyTreeInto(modRoot, destDir, /*addMissing*/ true);
        } else {
            // Update — overwrite same-named files AND add new files,
            // preserving folder-only files (saves, user configs).
            CopyTreeInto(modRoot, destDir, /*addMissing*/ true);
        }
    } else {
        CreateDirectoryW(destDir.c_str(), nullptr);
        CopyTreeInto(modRoot, destDir, /*addMissing*/ true);
    }

    DeleteFolderRecursive(tmp);

    PushProgress(4, zipIdx, zipTotal, zipBase, L"Done");
}

// ── Worker thread (private) ──────────────────────────────────────────
//
// Pops zips off the queue, processes one at a time. Drives the
// progress dialog from start to finish — shown before the first
// stage update, hidden after the queue drains.
static DWORD WINAPI ZipInstallWorker(LPVOID) {
    // Snapshot the queue length for the "N of M" display, then loop.
    // New drops that arrive mid-run are appended to the queue and get
    // picked up below; we extend total dynamically so the label stays
    // accurate.
    int processed = 0;
    int total = 0;
    {
        std::lock_guard<std::mutex> lk(g_zipQueueMutex);
        total = (int)g_zipQueue.size();
    }
    if (g_hwMain && total > 0) {
        SendMessageW(g_hwMain, MSG_ZIP_PROGRESS_SHOW, 0, 0);
    }
    for (;;) {
        wstring zip;
        {
            std::lock_guard<std::mutex> lk(g_zipQueueMutex);
            if (g_zipQueue.empty()) {
                g_zipWorkerRunning = false;
                break;
            }
            zip = g_zipQueue.front();
            g_zipQueue.erase(g_zipQueue.begin());
            // Recompute total in case more were appended mid-run.
            int remaining = (int)g_zipQueue.size();
            int newTotal = processed + 1 + remaining;
            if (newTotal > total) total = newTotal;
        }
        ++processed;
        ProcessOneZip(zip, processed, total);
    }
    if (g_hwMain) PostMessageW(g_hwMain, MSG_ZIP_PROGRESS_HIDE, 0, 0);
    if (g_hwMain) PostMessageW(g_hwMain, MSG_ZIP_QUEUE_DONE, 0, 0);
    return 0;
}

// ── Public entry point ───────────────────────────────────────────────

int EnqueueZipsForInstall(const vector<wstring>& paths) {
    int added = 0;
    {
        std::lock_guard<std::mutex> lk(g_zipQueueMutex);
        for (const auto& p : paths) {
            size_t dot = p.find_last_of(L'.');
            if (dot == wstring::npos) continue;
            wstring ext = p.substr(dot);
            if (_wcsicmp(ext.c_str(), L".zip") != 0) continue;
            g_zipQueue.push_back(p);
            ++added;
        }
    }
    if (added > 0) {
        bool expected = false;
        if (g_zipWorkerRunning.compare_exchange_strong(expected, true)) {
            HANDLE h = CreateThread(nullptr, 0, ZipInstallWorker,
                                    nullptr, 0, nullptr);
            if (h) CloseHandle(h);
            else   g_zipWorkerRunning = false;
        }
    }
    return added;
}
