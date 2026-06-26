// ═══════════════════════════════════════════════════════════════════════
//  ini_editor.h
// ═══════════════════════════════════════════════════════════════════════
//
//  D2RLoader.ini contains user-managed config that we must NOT trash
//  when we touch one of its values. These helpers do line-by-line
//  in-place edits: read preserves the full file as-is, write replaces
//  ONLY the line(s) for the key(s) we care about while keeping every
//  comment, blank line, and unknown key untouched.
//
//  Depends on core for ReadTextFile / WriteTextFile.
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// Trim leading/trailing whitespace (spaces + tabs) from s in place.
// Used by the INI parser; left in the public surface because it's a
// useful primitive and small enough to be cheap to export.
void TrimWs(wstring& s);

// Returns true if `line` (after trimming) sets `key` in any way.
// Output `value` is the trimmed right-hand side; comments past the
// value are NOT stripped — INI comments are conventionally at line
// start ';' which is rejected before this point.
bool ParseIniLine(const wstring& line, const wstring& key, wstring& value);

// Read an INI value by section + key. Returns `def` on missing
// section/key or malformed value.
int  IniGetInt(const wstring& path, const wstring& section,
               const wstring& key, int def);

// Edit one INI value in place. Preserves every other line. Creates
// the file with a minimal stub if it doesn't exist. If the section
// exists but the key doesn't, appends the key inside that section.
// If neither exists, appends both at end of file. CRLF / LF EOL
// style is preserved from the existing file.
void IniSetInt(const wstring& path, const wstring& section,
               const wstring& key, int newValue);
