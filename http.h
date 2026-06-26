// ═══════════════════════════════════════════════════════════════════════
//  http.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Synchronous WinHTTP wrappers — one for text payloads (HttpGet),
//  one for binary streamed-to-disk downloads (HttpDownloadFile).
//
//  Both helpers are blocking on the calling thread; the launcher uses
//  them only from worker threads (update check, self-update install)
//  so the UI thread never stalls on network I/O.
//
//  Depends on core for Utf8ToWide.
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// Synchronous GET result. body is the UTF-8-decoded response body as a
// wstring (we treat all payloads as UTF-8 since we only fetch JSON).
// On failure body is empty and status is 0 (network/timeout error) or
// the HTTP status code (server error).
struct HttpResult {
    int     status   = 0;
    bool    timedOut = false;
    wstring body;
};

// Split "https://host/path?query" into host + path. Returns true on
// success. outPort is set to the default HTTP/HTTPS port; explicit
// :PORT in the URL is stripped from the host (we don't need it).
bool ParseUrl(const wstring& url,
              wstring& outHost, wstring& outPath,
              bool& outHttps, INTERNET_PORT& outPort);

// Synchronous HTTPS/HTTP GET. Decodes the body as UTF-8 into a
// wstring. Caps the body at 2 MB to prevent runaway downloads (the
// launcher only hits this for small JSON responses).
HttpResult HttpGet(const wstring& url, int timeoutMs);

// Binary-safe download — streams the response body straight to a
// file at destPath. Used by the launcher self-update flow where the
// payload is a multi-megabyte zip (not text, so HttpGet's UTF-8
// decode would corrupt it). Follows HTTP 30x redirects (GitHub
// release URLs redirect to CDN blob storage).
//
// Returns 0 on success, or:
//   -1  for network/setup failures
//   N   for HTTP response codes other than 200 (caller can check)
// On failure the destination file is left in a partial state — the
// caller should delete it before retrying.
int HttpDownloadFile(const wstring& url,
                     const wstring& destPath,
                     int timeoutMs);
