// ═══════════════════════════════════════════════════════════════════════
//  version.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Version-string normalization and comparison. Used by both the mod
//  update checker (mod's local vs remote) and the launcher's self-update
//  check (LAUNCHER_VERSION vs GitHub release tag).
//
//  Semantic-ish: compares dot-separated numeric segments first, then any
//  trailing string suffix lexicographically. Leading "v" stripped.
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// Returns the input with leading "v"/"V" stripped and outer whitespace
// trimmed. Inner content untouched.
wstring NormalizeVersion(const wstring& v);

// Compares two version strings after normalization.
// Returns -1 if a < b, 0 if equal, +1 if a > b.
int     CompareVersions(const wstring& a, const wstring& b);
