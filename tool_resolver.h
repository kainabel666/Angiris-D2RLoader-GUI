// ═══════════════════════════════════════════════════════════════════════
//  tool_resolver.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Locating and launching the external modding tools the launcher
//  exposes via the Modding column buttons (AFJ Sheet Editor Pro,
//  VS Code, SpriteEdit, etc.). Each tool has a cached absolute path
//  in g_cfg; if that path is missing or stale, we fall back to a
//  recursive search under the user's configured tools folder, and
//  finally to a "locate manually" file picker.
//
//  Depends on:
//    config — for g_cfg.toolsDir and SaveCfg (when a tool gets a
//             newly resolved path it's cached back to disk).
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// Resolve a Windows .lnk shortcut to its target executable path.
// Used internally by SearchToolRecursive / BrowseForTool but also
// publicly so the rest of the launcher can adopt the same .lnk
// handling pattern if needed. Returns empty on failure.
wstring ResolveShortcut(const wstring& lnkPath);

// Recursive depth-bounded search for a tool executable (or its
// .lnk shortcut) under `root`. Matches `targetExe` directly OR
// `targetExe`'s stem + ".lnk" (in which case the link is resolved
// and the target returned). Capped at 6 levels deep + 5000 visited
// to avoid pathological infinite-symlink loops. Returns empty on
// miss.
wstring SearchToolRecursive(const wstring& root, const wstring& targetExe,
                            int depth = 0, int* visited = nullptr);

// Pop a "locate manually" Open File dialog. Filter accepts .exe
// and .lnk; .lnk picks are auto-resolved to their target. Returns
// empty on cancel.
wstring BrowseForTool(HWND owner, const wstring& title);

// Top-level handler for a tool button click. Three-tier resolution:
//   1. If `cachedPath` is set and still exists → launch it.
//   2. Otherwise, recursively search g_cfg.toolsDir for `exeName`.
//      On hit, store the path back in cachedPath, SaveCfg, launch.
//   3. On miss, prompt the user with a Yes/No "browse manually?"
//      dialog. On Yes, run BrowseForTool, cache, SaveCfg, launch.
//
// `cachedPath` is a non-const reference to the relevant g_cfg.toolXxx
// field so this helper can update it in place.
void LaunchTool(HWND owner, wstring& cachedPath,
                const wstring& exeName, const wstring& friendlyName);
