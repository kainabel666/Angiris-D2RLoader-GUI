// ═══════════════════════════════════════════════════════════════════════
//  http.cpp — see http.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "http.h"
#include "core.h"   // Utf8ToWide

bool ParseUrl(const wstring& url,
              wstring& outHost, wstring& outPath,
              bool& outHttps, INTERNET_PORT& outPort) {
    if (url.rfind(L"https://", 0) == 0) {
        outHttps = true; outPort = INTERNET_DEFAULT_HTTPS_PORT;
    } else if (url.rfind(L"http://", 0) == 0) {
        outHttps = false; outPort = INTERNET_DEFAULT_HTTP_PORT;
    } else {
        return false;
    }
    size_t schemeEnd = url.find(L"://") + 3;
    size_t pathStart = url.find(L'/', schemeEnd);
    if (pathStart == wstring::npos) {
        outHost = url.substr(schemeEnd);
        outPath = L"/";
    } else {
        outHost = url.substr(schemeEnd, pathStart - schemeEnd);
        outPath = url.substr(pathStart);
    }
    // Strip any ":port" from host (we don't override defaults — keep it simple)
    size_t colon = outHost.find(L':');
    if (colon != wstring::npos) outHost = outHost.substr(0, colon);
    return !outHost.empty();
}

HttpResult HttpGet(const wstring& url, int timeoutMs) {
    HttpResult r;
    wstring host, path;
    bool https; INTERNET_PORT port;
    if (!ParseUrl(url, host, path, https, port)) return r;

    HINTERNET hSession = WinHttpOpen(L"Angiris/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return r;
    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET hConn = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConn) { WinHttpCloseHandle(hSession); return r; }

    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) {
        WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession); return r;
    }

    // GitHub requires a User-Agent for API requests
    WinHttpAddRequestHeaders(hReq,
        L"User-Agent: Angiris-Launcher/1.0\r\n"
        L"Accept: application/vnd.github+json, application/json, */*\r\n",
        (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    BOOL ok = WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_WINHTTP_TIMEOUT) r.timedOut = true;
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
        return r;
    }
    ok = WinHttpReceiveResponse(hReq, nullptr);
    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_WINHTTP_TIMEOUT) r.timedOut = true;
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
        return r;
    }

    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(hReq,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    r.status = (int)status;

    // Drain body
    string body;
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
        size_t off = body.size();
        body.resize(off + avail);
        DWORD read = 0;
        WinHttpReadData(hReq, &body[off], avail, &read);
        body.resize(off + read);
        if (body.size() > 2 * 1024 * 1024) break;  // 2 MB cap — manifests are tiny
    }
    r.body = Utf8ToWide(body);
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
    return r;
}

int HttpDownloadFile(const wstring& url,
                     const wstring& destPath,
                     int timeoutMs) {
    wstring host, path;
    bool https; INTERNET_PORT port;
    if (!ParseUrl(url, host, path, https, port)) return -1;

    HINTERNET hSession = WinHttpOpen(L"Angiris/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return -1;
    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET hConn = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConn) { WinHttpCloseHandle(hSession); return -1; }

    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        https ? WINHTTP_FLAG_SECURE : 0);
    if (!hReq) {
        WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession); return -1;
    }
    // GitHub redirects /releases/download/... to a CDN URL — let WinHttp
    // follow it automatically (HTTP 302 → blob storage).
    DWORD redirOpt = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hReq, WINHTTP_OPTION_REDIRECT_POLICY,
                     &redirOpt, sizeof(redirOpt));

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
        return -1;
    }
    if (!WinHttpReceiveResponse(hReq, nullptr)) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
        return -1;
    }
    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(hReq,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
        return (int)status;
    }

    // Open the destination file. CREATE_ALWAYS replaces any partial
    // leftover from a previous failed download.
    HANDLE hFile = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
        return -1;
    }

    // Stream the body to disk. 64 KB chunks — small enough to keep
    // memory flat, large enough to avoid syscall overhead dominating
    // throughput.
    bool writeOk = true;
    BYTE buf[64 * 1024];
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
        DWORD toRead = (avail > sizeof(buf)) ? sizeof(buf) : avail;
        DWORD read = 0;
        if (!WinHttpReadData(hReq, buf, toRead, &read) || read == 0) break;
        DWORD written = 0;
        if (!WriteFile(hFile, buf, read, &written, nullptr)
            || written != read) {
            writeOk = false;
            break;
        }
    }
    CloseHandle(hFile);
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
    return writeOk ? 0 : -1;
}
