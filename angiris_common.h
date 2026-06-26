// ═══════════════════════════════════════════════════════════════════════
//  angiris_common.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Shared umbrella header. Every Angiris module includes this so they all
//  see the same Win32 / GDI+ / std-lib surface in the same order. Avoids
//  per-file repetition of the same ~20 includes.
//
//  using-namespace lines here are deliberate — the entire codebase was
//  written against `wstring` / `vector` / `Color` (etc.) without the
//  `std::` / `Gdiplus::` prefixes. Forcing prefixes during the refactor
//  would touch thousands of lines for no functional gain, so we accept
//  the namespace pollution here in exchange for source compatibility.
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <gdiplus.h>
#include <winhttp.h>

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <thread>
#include <atomic>
#include <mutex>

using namespace std;
using namespace Gdiplus;
