// ═══════════════════════════════════════════════════════════════════════
//  core.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Foundational utilities used by almost every other module. Kept
//  deliberately small and free of UI / Win32-message-loop concerns so
//  it can sit at the bottom of the dependency graph.
//
//  Contents:
//    • Path helpers      — AppDir
//    • File I/O          — ReadTextFile, WriteTextFile (UTF-8, BOM-aware)
//    • Charset           — Utf8ToWide
//    • JSON extraction   — EscapeJson + JsonStr / JsonBool / JsonInt /
//                          JsonDouble (read-only; whole-file string-based
//                          scans, no AST)
//
//  Conventions:
//    • All strings are wide (wstring). Narrow strings are only used
//      when calling APIs that demand them (HTTP body bytes, etc.).
//    • JSON helpers return defaults on missing key or malformed value
//      rather than throwing. The launcher's JSON files are tightly
//      controlled, so we trade strictness for simplicity.
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// Returns the directory the running executable lives in (no trailing
// backslash). The launcher uses this as the anchor for everything in
// assets\, config files written next to the exe, etc.
wstring AppDir();

// Read a whole UTF-8 text file into a wstring. Returns empty on any
// failure (missing file, read error, etc.). Strips a leading BOM if
// present.
wstring ReadTextFile(const wstring& path);

// Write a wstring to disk as UTF-8 (no BOM). Silent no-op on failure
// — callers that need to know should check the file afterwards.
void    WriteTextFile(const wstring& path, const wstring& content);

// Convert a UTF-8 byte string to a wide string. Defined here (not in
// http.cpp where it was originally) because it's broadly useful and
// has no networking dependency.
wstring Utf8ToWide(const string& s);

// Escape backslashes and double quotes for safe inclusion as a JSON
// string value. Does NOT add the surrounding quotes — caller writes
// those.
wstring EscapeJson(const wstring& s);

// Extract a string value from a JSON blob by key. Round-trip-safe
// with EscapeJson (decodes \\ and \" back to literals). Also handles
// common JSON escapes (\n, \t, \r, \/). Returns empty on missing key.
wstring JsonStr(const wstring& j, const wstring& key);

// Boolean variant — reads unquoted "true"/"false". Returns def
// otherwise (missing key, malformed value, etc.).
bool    JsonBool(const wstring& j, const wstring& key, bool def = false);

// Integer variant — reads an unquoted decimal number (optionally
// signed). Returns def on missing key or non-numeric value.
int     JsonInt(const wstring& j, const wstring& key, int def = 0);

// Floating-point variant — reads an unquoted decimal number with
// optional sign and a single fractional point. No exponent / NaN /
// Inf support — we don't need it for scale factors and color values.
double  JsonDouble(const wstring& j, const wstring& key, double def = 0.0);

// ── Shared DPI scale ─────────────────────────────────────────────────
//
// System DPI ratio (96 dpi = 1.0). Read at startup, never changes
// during a session. Lives here (rather than in some UI module)
// because persistence-layer modules need to read/write it on
// save/load to detect a between-session DPI change.
extern double g_dpiScale;

// ── Main window handle ───────────────────────────────────────────────
//
// HWND of the main launcher window. Set by wWinMain after window
// creation; null before that. Used by background threads (update
// check, mod watcher, zip install) to PostMessage results back to
// the UI thread. Lives in core.h rather than each consumer module
// because so many places need it.
extern HWND g_hwMain;
extern HINSTANCE g_hInst;
// True while the bottom expansion panel is open. Read by paint code
// (Arrow button paint flips the chevron art when expanded; bottom-panel
// paint runs only when this is true). Toggled from the Expand button's
// click handler in Angiris.cpp.
extern bool g_bottomExpanded;
