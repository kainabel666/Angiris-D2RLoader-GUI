// ═══════════════════════════════════════════════════════════════════════
//  launcher_self_update.cpp — see launcher_self_update.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "launcher_self_update.h"
#include "core.h"        // AppDir, JsonStr, g_hwMain, g_dpiScale
#include "http.h"        // HttpGet, HttpDownloadFile, HttpResult
#include "fs_utils.h"    // MakeTempInstallDir, DeleteFolderRecursive,
                         // RunTarExtract, CopyTreeIntoLogged
#include "assets.h"      // DestroyAssetCache — release PNG file handles
                         // before the in-place install overwrites them
#include "fonts.h"       // UnloadFonts — RemoveFontResourceEx every
                         // bundled .ttf so they're unlocked for overwrite

// ── Restart button control ID ────────────────────────────────────────
//
// Matches the value in the Angiris.cpp control-ID enum. Defined
// inline here so this module doesn't have to include the entire UI
// header surface just to learn one constant. If the value ever
// changes in Angiris.cpp, this needs the same update.
static constexpr int IDC_LAUNCHER_RESTART_BTN = 600;

// ── Self-update state (definitions) ─────────────────────────────────
//
// Declarations in launcher_self_update.h. UI thread reads g_launcher
// UpdateAvailable + g_launcherUpdateLatestTag to drive the version-
// label glow and dialog. Workers write before posting messages, so
// the UI sees fresh values after each post.

wstring g_launcherUpdateLatestTag;
wstring g_launcherUpdateDownloadUrl;
bool    g_launcherUpdateAvailable = false;
bool    g_forceUpdatePrompt       = false;
wstring g_pendingOldExeDelete;
int     g_cleanupOldExeAttempts   = 0;

// Internal: dedupe in-flight check workers. Atomic CAS pattern — the
// thread that wins the false→true swap is the one that runs the
// network code.
static atomic<bool> g_launcherUpdateCheckRunning{false};

// ── Popup state ─────────────────────────────────────────────────────
//
// All single-threaded — the popup lives only on the UI thread. The
// install worker fires SendMessage(MSG_LUPOPUP_STATUS) which causes
// the UI thread to update g_lupopupStatus before painting. No
// synchronization needed beyond the natural SendMessage barrier.

static HWND  g_lupopupWnd          = nullptr;
static HWND  g_lupopupRestartBtn   = nullptr;
static int   g_lupopupStatus       = 0;       // 0=init, 1=DL, 2=updating, 3=complete
static DWORD g_lupopupCompleteTick = 0;       // GetTickCount() when status hit 3
static HFONT g_lupopupTitleFont    = nullptr;
static HFONT g_lupopupBodyFont     = nullptr;
static HFONT g_lupopupBtnFont      = nullptr;

// ── Helpers for FindReleaseZipUrl / FindAngirisExeInTree ────────────

// Walk every "browser_download_url" field in the JSON; first one
// whose value ends in .zip wins. If no .zip is found, fall back to
// the first URL we saw. Crude but robust enough for a single-asset
// release flow — we're not parsing the array structure, just
// substring matching, which keeps us free of nested-object parsing.
static wstring FindReleaseZipUrl(const wstring& json) {
    const wstring key = L"\"browser_download_url\"";
    wstring firstAny;
    size_t pos = 0;
    while (true) {
        size_t k = json.find(key, pos);
        if (k == wstring::npos) break;
        size_t colon = json.find(L':',  k);
        if (colon == wstring::npos) break;
        size_t q1    = json.find(L'"',  colon);
        if (q1 == wstring::npos) break;
        size_t q2    = json.find(L'"',  q1 + 1);
        if (q2 == wstring::npos) break;
        wstring url = json.substr(q1 + 1, q2 - q1 - 1);
        if (firstAny.empty()) firstAny = url;
        if (url.size() >= 4
            && _wcsicmp(url.c_str() + url.size() - 4, L".zip") == 0) {
            return url;
        }
        pos = q2 + 1;
    }
    return firstAny;
}

// BFS for the shallowest Angiris.exe in the extracted release.
// Mirrors FindModinfoJson in zip_install — release zips may wrap
// the payload in a "Angiris-vX.Y" folder, so we don't assume a flat
// layout.
static wstring FindAngirisExeInTree(const wstring& root) {
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
            } else if (_wcsicmp(fd.cFileName, L"Angiris.exe") == 0) {
                foundFile = full;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        if (!foundFile.empty()) return foundFile;
    }
    return L"";
}

// ── Check worker ────────────────────────────────────────────────────
//
// Wakes once at startup, fires off the API request, posts the
// "update available" message if the server confirmed a release.
// Pure network code — no UI, no g_cfg writes — runs entirely on its
// own thread so the launcher window comes up instantly even on a
// slow network.
//
// Posts the message whenever the API returned a tag, regardless of
// whether a .zip asset was attached. The download URL may
// legitimately be missing — release tagged but no binary uploaded
// yet, or only non-zip assets attached — and we want the user to
// see the dialog anyway. StartLauncherUpdateInstall handles the
// missing-URL case by opening the GitHub releases page in their
// browser, so the Update button always has a fallback action.
//
// On every run, writes a diagnostic to
// <appdir>\assets\last_update_check.log covering HTTP status,
// parsed tag, parsed download URL, and (when the response is small
// enough) the raw body. Open that file to debug why the dialog
// didn't appear or why the Update button fell back to the browser.
static DWORD WINAPI LauncherUpdateCheckWorker(LPVOID) {
    wstring apiUrl = wstring(L"https://api.github.com/repos/")
                   + LAUNCHER_GITHUB_OWNER + L"/" + LAUNCHER_GITHUB_REPO
                   + L"/releases/latest";
    HttpResult r = HttpGet(apiUrl, 8000);

    wstring tag, url;
    if (r.status == 200 && !r.body.empty()) {
        tag = JsonStr(r.body, L"tag_name");
        url = FindReleaseZipUrl(r.body);
        if (!tag.empty()) {
            g_launcherUpdateLatestTag   = tag;
            g_launcherUpdateDownloadUrl = url;
            if (g_hwMain)
                PostMessageW(g_hwMain, MSG_LAUNCHER_UPDATE_AVAILABLE, 0, 0);
        }
    }

    // Diagnostic log — overwritten every run, so it always reflects
    // the most recent check. Lives at <appdir>\assets\last_update_check.log.
    {
        wstring logPath = AppDir() + L"\\assets\\last_update_check.log";
        FILE* f = nullptr;
        _wfopen_s(&f, logPath.c_str(), L"w, ccs=UTF-8");
        if (f) {
            time_t t = time(nullptr);
            struct tm tm_;
            localtime_s(&tm_, &t);
            wchar_t timeStr[64];
            wcsftime(timeStr, 64, L"%Y-%m-%d %H:%M:%S", &tm_);

            fwprintf(f, L"=== Launcher update check ===\n");
            fwprintf(f, L"Time:           %ls\n", timeStr);
            fwprintf(f, L"Current ver:    %ls\n", LAUNCHER_VERSION);
            fwprintf(f, L"API URL:        %ls\n", apiUrl.c_str());
            fwprintf(f, L"HTTP status:    %d\n", r.status);
            fwprintf(f, L"Timed out:      %ls\n", r.timedOut ? L"yes" : L"no");
            fwprintf(f, L"Body length:    %zu chars\n", r.body.size());
            fwprintf(f, L"Parsed tag:     %ls\n",
                     tag.empty() ? L"(none — JSON parse failed or no tag_name)" : tag.c_str());
            fwprintf(f, L"Parsed URL:     %ls\n",
                     url.empty() ? L"(none — no asset with .zip extension found)" : url.c_str());
            fwprintf(f, L"\n");
            if (r.body.size() <= 32768) {
                fwprintf(f, L"--- Response body ---\n");
                fwprintf(f, L"%ls\n", r.body.c_str());
            } else {
                fwprintf(f, L"--- Response body (truncated, first 32KB) ---\n");
                fwprintf(f, L"%.*s\n", 32768, r.body.c_str());
            }
            fclose(f);
        }
    }

    g_launcherUpdateCheckRunning = false;
    return 0;
}

void KickoffLauncherUpdateCheck() {
    if (g_launcherUpdateCheckRunning.exchange(true)) return;
    HANDLE h = CreateThread(nullptr, 0, LauncherUpdateCheckWorker,
                            nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    else   g_launcherUpdateCheckRunning = false;
}

// ── .old cleanup with deferred retry ────────────────────────────────

void CleanupLauncherOldExe() {
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return;
    wstring oldExe = wstring(exePath) + L".old";

    // Fast path: check existence first so the common "no .old here"
    // case doesn't even attempt a delete.
    DWORD attrs = GetFileAttributesW(oldExe.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return;   // nothing to clean

    // Try a quick delete. If the file was orphaned from a long-ago
    // session (no prior process is holding it), this succeeds
    // immediately.
    if (DeleteFileW(oldExe.c_str())) return;
    DWORD err = GetLastError();
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return;

    // A few quick retries — covers the case where the prior process
    // just exited and the OS needs a beat. After ~600ms total, give up
    // and hand off to the deferred timer.
    for (int i = 0; i < 3; ++i) {
        Sleep(200);
        if (DeleteFileW(oldExe.c_str())) return;
        err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return;
    }

    // Still locked. Queue for the deferred retry — the main window
    // timer will keep at it for ~30 more seconds while the prior
    // launcher (which is almost certainly the holder) finishes
    // shutting down.
    g_pendingOldExeDelete   = oldExe;
    g_cleanupOldExeAttempts = 0;
}

void StartDeferredOldExeCleanup() {
    if (g_pendingOldExeDelete.empty()) return;
    if (!g_hwMain) return;
    SetTimer(g_hwMain, IDT_CLEANUP_OLD_EXE, 2000, nullptr);
}

void LogCleanupOldExeGaveUp(const wstring& oldExe, DWORD lastErr) {
    wstring logPath = AppDir() + L"\\assets\\last_update_install.log";
    FILE* f = nullptr;
    _wfopen_s(&f, logPath.c_str(), L"a, ccs=UTF-8");
    if (!f) return;
    time_t t = time(nullptr);
    struct tm tm_;
    localtime_s(&tm_, &t);
    wchar_t ts[32];
    wcsftime(ts, 32, L"%Y-%m-%d %H:%M:%S", &tm_);
    fwprintf(f,
        L"[%ls] CleanupLauncherOldExe: deferred retry exhausted for %ls "
        L"(err=%lu). Scheduled for delete on next reboot via MoveFileEx.\n",
        ts, oldExe.c_str(), lastErr);
    fclose(f);
}

// ── Popup window ────────────────────────────────────────────────────

static LRESULT CALLBACK LauncherUpdatePopupProc(HWND hw, UINT msg,
                                                WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        RECT cr; GetClientRect(hw, &cr);

        // Solid dark background — no PNG dependency.
        HBRUSH bgBrush = CreateSolidBrush(RGB(28, 24, 20));
        FillRect(hdc, &cr, bgBrush);
        DeleteObject(bgBrush);

        // Gold border (2px rectangle) so the popup reads as themed
        // even without the parchment texture.
        HPEN borderPen = CreatePen(PS_SOLID, 2, RGB(200, 150, 60));
        HPEN   oldPen   = (HPEN)SelectObject(hdc, borderPen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, 0, 0, cr.right, cr.bottom);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(borderPen);

        SetBkMode(hdc, TRANSPARENT);
        int padX = (int)(24 * g_dpiScale);

        // Title
        HFONT oldFont = (HFONT)SelectObject(hdc, g_lupopupTitleFont);
        SetTextColor(hdc, RGB(220, 180, 100));
        RECT titleRc = { padX, (int)(24 * g_dpiScale),
                         cr.right - padX, (int)(64 * g_dpiScale) };
        DrawTextW(hdc, L"Updating Angiris Launcher", -1, &titleRc,
                  DT_CENTER | DT_TOP | DT_SINGLELINE);

        // Three stacked status lines. Color + decoration depends on
        // whether each status is past, current, or upcoming.
        SelectObject(hdc, g_lupopupBodyFont);
        const wchar_t* labels[3] = { L"Downloading", L"Updating", L"Complete" };
        int yStart = (int)(96  * g_dpiScale);
        int lineH  = (int)(40 * g_dpiScale);
        for (int i = 0; i < 3; ++i) {
            int statusIdx = i + 1;  // 1, 2, 3
            wstring text;
            COLORREF color;
            if (g_lupopupStatus > statusIdx) {
                // Done — checkmark + green
                text  = wstring(L"\u2713  ") + labels[i];  // ✓
                color = RGB(170, 215, 165);
            } else if (g_lupopupStatus == statusIdx) {
                // Active — ellipsis + gold
                text  = wstring(L"\u25B6  ") + labels[i] + L"\u2026";  // ▶ ...
                color = RGB(255, 220, 120);
            } else {
                // Pending — bullet + dim gray
                text  = wstring(L"\u00B7  ") + labels[i];  // ·
                color = RGB(130, 120, 110);
            }
            SetTextColor(hdc, color);
            RECT lineRc = { padX * 2, yStart + i * lineH,
                            cr.right - padX, yStart + (i + 1) * lineH };
            DrawTextW(hdc, text.c_str(), -1, &lineRc,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(hdc, oldFont);

        EndPaint(hw, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
        if (!dis || dis->CtlID != IDC_LAUNCHER_RESTART_BTN) return FALSE;
        bool enabled = IsWindowEnabled(dis->hwndItem) != FALSE;
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;

        COLORREF bg, fg, border;
        if (!enabled) {
            bg     = RGB(38, 34, 30);
            fg     = RGB(95, 88, 80);
            border = RGB(70, 62, 54);
        } else if (pressed) {
            bg     = RGB(80, 60, 28);
            fg     = RGB(255, 230, 150);
            border = RGB(220, 180, 100);
        } else {
            bg     = RGB(58, 48, 34);
            fg     = RGB(230, 200, 140);
            border = RGB(180, 140, 80);
        }
        HBRUSH bgBrush = CreateSolidBrush(bg);
        FillRect(dis->hDC, &dis->rcItem, bgBrush);
        DeleteObject(bgBrush);
        HPEN bp = CreatePen(PS_SOLID, 2, border);
        HPEN op = (HPEN)SelectObject(dis->hDC, bp);
        HBRUSH ob = (HBRUSH)SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
        Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top,
                  dis->rcItem.right, dis->rcItem.bottom);
        SelectObject(dis->hDC, op);
        SelectObject(dis->hDC, ob);
        DeleteObject(bp);

        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, fg);
        HFONT oldF = (HFONT)SelectObject(dis->hDC, g_lupopupBtnFont);
        wchar_t txt[64] = L"";
        GetWindowTextW(dis->hwndItem, txt, 64);
        DrawTextW(dis->hDC, txt, -1, &dis->rcItem,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dis->hDC, oldF);
        return TRUE;
    }

    case WM_TIMER: {
        if (wp == 1) {
            // Drive the Restart-button enabled state. Enabled iff
            // status has reached 3 AND ≥500 ms have elapsed since
            // the Complete tick was set. (Prevents accidental
            // immediate clicks.)
            if (g_lupopupRestartBtn) {
                bool wantEnabled =
                    (g_lupopupStatus >= 3) &&
                    (g_lupopupCompleteTick != 0) &&
                    ((GetTickCount() - g_lupopupCompleteTick) >= 500);
                BOOL nowEnabled = IsWindowEnabled(g_lupopupRestartBtn);
                if (wantEnabled && !nowEnabled) {
                    EnableWindow(g_lupopupRestartBtn, TRUE);
                    InvalidateRect(g_lupopupRestartBtn, nullptr, TRUE);
                }
            }
        }
        return 0;
    }

    case WM_COMMAND: {
        if (LOWORD(wp) != IDC_LAUNCHER_RESTART_BTN) return 0;
        if (!g_lupopupRestartBtn) return 0;
        if (!IsWindowEnabled(g_lupopupRestartBtn)) return 0;

        // Spawn the new launcher. Best-effort — if it fails, we
        // still tear down and exit so the user can manually relaunch.
        wchar_t curExe[MAX_PATH];
        if (GetModuleFileNameW(nullptr, curExe, MAX_PATH) != 0) {
            wstring curExeStr = curExe;
            size_t slashIdx = curExeStr.find_last_of(L"\\/");
            wstring installDir = (slashIdx != wstring::npos)
                               ? curExeStr.substr(0, slashIdx) : L"";
            STARTUPINFOW si = { sizeof(si) };
            PROCESS_INFORMATION pi = {};
            wstring quoted = L"\"" + curExeStr + L"\"";
            vector<wchar_t> cmdLine(quoted.begin(), quoted.end());
            cmdLine.push_back(0);
            if (CreateProcessW(curExeStr.c_str(), cmdLine.data(),
                               nullptr, nullptr, FALSE, 0, nullptr,
                               installDir.empty() ? nullptr
                                                  : installDir.c_str(),
                               &si, &pi)) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
        }

        DestroyWindow(hw);
        // Main window is hidden but still alive — close it so the
        // process exits cleanly. PostQuitMessage as belt-and-suspenders
        // in case g_hwMain somehow doesn't respond.
        if (g_hwMain && IsWindow(g_hwMain)) {
            PostMessageW(g_hwMain, WM_CLOSE, 0, 0);
        } else {
            PostQuitMessage(0);
        }
        return 0;
    }

    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC:
        // Make Windows happy if it asks for a background brush.
        return (LRESULT)GetStockObject(BLACK_BRUSH);

    case WM_CLOSE:
        // Suppress — user must click Restart (or kill from Task Manager).
        return 0;

    case WM_DESTROY:
        KillTimer(hw, 1);
        if (g_lupopupTitleFont) { DeleteObject(g_lupopupTitleFont); g_lupopupTitleFont = nullptr; }
        if (g_lupopupBodyFont)  { DeleteObject(g_lupopupBodyFont);  g_lupopupBodyFont  = nullptr; }
        if (g_lupopupBtnFont)   { DeleteObject(g_lupopupBtnFont);   g_lupopupBtnFont   = nullptr; }
        if (g_lupopupWnd == hw) g_lupopupWnd = nullptr;
        g_lupopupRestartBtn = nullptr;
        return 0;
    }
    // Status update from the install worker. wp = 1 (Downloading) / 2
    // (Updating) / 3 (Complete). When status hits 3, stamp the tick
    // so the timer-driven enable logic for the Restart button can
    // fire 500 ms later.
    if (msg == MSG_LUPOPUP_STATUS) {
        int newStatus = (int)wp;
        g_lupopupStatus = newStatus;
        if (newStatus >= 3 && g_lupopupCompleteTick == 0) {
            g_lupopupCompleteTick = GetTickCount();
        }
        InvalidateRect(hw, nullptr, TRUE);
        UpdateWindow(hw);
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

void EnsureLauncherUpdatePopupClass() {
    static bool classReg = false;
    if (classReg) return;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = LauncherUpdatePopupProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"AngirisLauncherUpdatePopup";
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
    classReg = true;
}

// Creates and shows the popup. Uses system Segoe UI throughout — no
// dependency on the launcher's font collection or PNG cache.
static void ShowLauncherUpdatePopup() {
    if (g_lupopupWnd) return;
    EnsureLauncherUpdatePopupClass();

    int w  = (int)(440 * g_dpiScale);
    int h  = (int)(340 * g_dpiScale);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x  = (sw - w) / 2;
    int y  = (sh - h) / 2;

    HWND wnd = CreateWindowExW(
        WS_EX_TOPMOST,
        L"AngirisLauncherUpdatePopup",
        L"Updating Angiris Launcher",
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, w, h,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!wnd) return;
    g_lupopupWnd = wnd;

    // System fonts — no PrivateFontCollection involvement. Negative
    // heights are point-size-ish in logical units.
    int titlePx = -(int)(20 * g_dpiScale);
    int bodyPx  = -(int)(16 * g_dpiScale);
    int btnPx   = -(int)(15 * g_dpiScale);
    auto mkFont = [&](int px, int weight) {
        return CreateFontW(px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    };
    g_lupopupTitleFont = mkFont(titlePx, FW_BOLD);
    g_lupopupBodyFont  = mkFont(bodyPx,  FW_NORMAL);
    g_lupopupBtnFont   = mkFont(btnPx,   FW_BOLD);

    // Restart button — owner-drawn so we can theme it without
    // dependencies, always present but starts disabled.
    int btnW = (int)(180 * g_dpiScale);
    int btnH = (int)(50  * g_dpiScale);
    int btnX = (w - btnW) / 2;
    int btnY = h - btnH - (int)(28 * g_dpiScale);
    g_lupopupRestartBtn = CreateWindowExW(
        0, L"BUTTON", L"Restart",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        btnX, btnY, btnW, btnH,
        wnd, (HMENU)(INT_PTR)IDC_LAUNCHER_RESTART_BTN,
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g_lupopupRestartBtn, WM_SETFONT,
                 (WPARAM)g_lupopupBtnFont, TRUE);
    EnableWindow(g_lupopupRestartBtn, FALSE);   // enabled later by timer

    // 100ms timer drives the "enable Restart after 500ms post-complete"
    // logic. Cheap, runs only while the popup exists.
    SetTimer(wnd, 1, 100, nullptr);

    ShowWindow(wnd, SW_SHOW);
    UpdateWindow(wnd);
    SetForegroundWindow(wnd);
}

// ── Install worker ──────────────────────────────────────────────────
//
// Worker thread for the launcher self-update install. Sends three
// MSG_LUPOPUP_STATUS messages to drive the popup forward (1, 2, 3).
// File overwrites are safe by the time this runs because
// StartLauncherUpdateInstall has already torn down the asset cache
// and font collection on the UI thread.
static DWORD WINAPI LauncherUpdateInstallWorker(LPVOID lp) {
    (void)lp;

    // Diagnostic log appended each run.
    wstring logPath = AppDir() + L"\\assets\\last_update_install.log";
    FILE* logF = nullptr;
    _wfopen_s(&logF, logPath.c_str(), L"a, ccs=UTF-8");
    auto LOG = [&](const wchar_t* fmt, ...) {
        if (!logF) return;
        time_t t = time(nullptr);
        struct tm tm_;
        localtime_s(&tm_, &t);
        wchar_t ts[32];
        wcsftime(ts, 32, L"%H:%M:%S", &tm_);
        fwprintf(logF, L"[%ls] ", ts);
        va_list ap;
        va_start(ap, fmt);
        vfwprintf(logF, fmt, ap);
        va_end(ap);
        fwprintf(logF, L"\n");
        fflush(logF);
    };
    auto CLOSELOG = [&]() { if (logF) { fclose(logF); logF = nullptr; } };

    auto sendStatus = [&](int s) {
        // Route directly to the popup window. The popup's WindowProc
        // handles MSG_LUPOPUP_STATUS (updates g_lupopupStatus,
        // stamps g_lupopupCompleteTick on 3, invalidates) — keeps
        // the status state file-local to this module rather than
        // forcing MainProc to know about popup internals.
        if (g_lupopupWnd) {
            SendMessageW(g_lupopupWnd, MSG_LUPOPUP_STATUS, (WPARAM)s, 0);
        }
    };

    LOG(L"=== LauncherUpdateInstallWorker (popup-driven) ===");
    LOG(L"Download URL: %ls", g_launcherUpdateDownloadUrl.c_str());

    sendStatus(1);   // Downloading

    wstring tmpDir = MakeTempInstallDir();
    LOG(L"Temp dir: %ls", tmpDir.c_str());
    if (tmpDir.empty()) { LOG(L"FAIL: MakeTempInstallDir"); CLOSELOG(); return 1; }

    wstring zipPath = tmpDir + L"\\release.zip";
    int dlResult = HttpDownloadFile(g_launcherUpdateDownloadUrl,
                                    zipPath, 60000);
    LOG(L"Download result: %d (0=ok)", dlResult);
    if (dlResult != 0) {
        DeleteFolderRecursive(tmpDir);
        CLOSELOG(); return 1;
    }

    sendStatus(2);   // Updating

    wstring extractDir = tmpDir + L"\\extracted";
    CreateDirectoryW(extractDir.c_str(), nullptr);
    // Skip *.ttf at extraction. Windows holds an internal mapping for
    // any font that's been used to rasterize glyphs in the current
    // process, which RemoveFontResourceEx doesn't tear down — so the
    // active font's .ttf always fails CopyFileW with err=1224 during
    // a self-update. Fonts essentially never change between launcher
    // releases anyway; skipping them gives clean install logs and a
    // PARTIAL result becomes OK. Excluded at extraction so the files
    // never even land in temp.
    bool tarOk = RunTarExtract(zipPath, extractDir, L"*.ttf");
    LOG(L"Extract: %ls (TTFs excluded)", tarOk ? L"OK" : L"FAILED");
    if (!tarOk) { DeleteFolderRecursive(tmpDir); CLOSELOG(); return 1; }

    wstring newExePath = FindAngirisExeInTree(extractDir);
    LOG(L"New exe found: %ls",
        newExePath.empty() ? L"(NONE)" : newExePath.c_str());
    if (newExePath.empty()) {
        DeleteFolderRecursive(tmpDir); CLOSELOG(); return 1;
    }
    size_t lastSlash = newExePath.find_last_of(L"\\/");
    if (lastSlash == wstring::npos) {
        DeleteFolderRecursive(tmpDir); CLOSELOG(); return 1;
    }
    wstring releaseRoot = newExePath.substr(0, lastSlash);
    LOG(L"Release root: %ls", releaseRoot.c_str());

    wchar_t curExe[MAX_PATH];
    if (GetModuleFileNameW(nullptr, curExe, MAX_PATH) == 0) {
        LOG(L"FAIL: GetModuleFileNameW");
        DeleteFolderRecursive(tmpDir); CLOSELOG(); return 1;
    }
    wstring curExeStr   = curExe;
    size_t  curLastSlash = curExeStr.find_last_of(L"\\/");
    if (curLastSlash == wstring::npos) {
        DeleteFolderRecursive(tmpDir); CLOSELOG(); return 1;
    }
    wstring installDir = curExeStr.substr(0, curLastSlash);
    LOG(L"Install dir: %ls", installDir.c_str());

    wstring oldExe = curExeStr + L".old";
    DeleteFileW(oldExe.c_str());
    BOOL renameOk = MoveFileW(curExeStr.c_str(), oldExe.c_str());
    LOG(L"Rename current → .old: %ls (err=%lu)",
        renameOk ? L"OK" : L"FAILED",
        renameOk ? 0 : GetLastError());
    if (!renameOk) { DeleteFolderRecursive(tmpDir); CLOSELOG(); return 1; }

    // The big payoff: by this point the main window is hidden, the
    // asset cache is destroyed, and the font collection is unloaded.
    // PNG/font files are no longer locked and CopyFileW can overwrite
    // them without ACCESS_DENIED. If any file is STILL locked despite
    // all that, the per-file log shows which one — and we proceed to
    // status 3 anyway so the popup completes and the user can click
    // Restart. A partial copy where Angiris.exe succeeded is much
    // better UX than hanging on "Updating" forever.
    int failCount = 0;
    LOG(L"--- Begin copy ---");
    bool copyOk = CopyTreeIntoLogged(releaseRoot, installDir,
                                     logF, &failCount);
    LOG(L"--- End copy ---  result=%ls  failures=%d",
        copyOk ? L"OK" : L"PARTIAL", failCount);

    DeleteFolderRecursive(tmpDir);
    LOG(L"Install %ls — waiting 1s before signalling Complete",
        copyOk ? L"complete" : L"partial (see fails above)");

    // Per UX spec: 1-second beat between actual finish and showing
    // "Complete" — gives the filesystem and any background indexers
    // a moment to settle before the user can act on the new files.
    Sleep(1000);

    sendStatus(3);   // Complete

    LOG(L"=== Worker done ===");
    CLOSELOG();
    return 0;
}

void StartLauncherUpdateInstall(HWND parent) {
    if (g_launcherUpdateDownloadUrl.empty()) {
        // No downloadable .zip on the release — fall back to opening
        // the GitHub releases page in the browser. Don't tear down
        // anything in this branch since we're not actually installing.
        wstring releasesUrl = wstring(L"https://github.com/")
                            + LAUNCHER_GITHUB_OWNER + L"/"
                            + LAUNCHER_GITHUB_REPO
                            + L"/releases/latest";
        ShellExecuteW(nullptr, L"open", releasesUrl.c_str(),
                      nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }

    // Reset popup state for a fresh run.
    g_lupopupStatus       = 0;
    g_lupopupCompleteTick = 0;

    // Hide the main GUI. Its child windows + paint code won't fire
    // anymore, so the asset/font teardown below is safe.
    if (g_hwMain && IsWindow(g_hwMain)) {
        ShowWindow(g_hwMain, SW_HIDE);
    }

    // Release everything the installer would otherwise have to fight.
    // GDI+ Bitmap destructors release their underlying file handles;
    // PrivateFontCollection destructor releases the .ttf handles.
    DestroyAssetCache();
    UnloadFonts();

    // Show the popup *after* the asset/font teardown, since it uses
    // only system resources (Segoe UI + GDI) and is therefore not
    // affected by what we just released.
    ShowLauncherUpdatePopup();

    HANDLE t = CreateThread(nullptr, 0,
                            LauncherUpdateInstallWorker,
                            parent, 0, nullptr);
    if (t) CloseHandle(t);
}
