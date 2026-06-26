// ═══════════════════════════════════════════════════════════════════════
//  mod_updates.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Mod update checking — hits GitHub Releases / manifest URLs for each
//  opted-in mod, parses the response, updates g_updateInfo (in
//  update_cache), and pokes the UI when finished. All HTTP work runs
//  on detached worker threads; the UI thread learns about completion
//  via MSG_UPDATE_CHECK_DONE.
//
//  Depends on:
//    core         — JsonStr/JsonInt
//    version      — CompareVersions
//    http         — HttpGet, HttpResult
//    update_cache — UpdateInfo, g_updateInfo, SaveUpdateCache
//    mod_types    — ModInfo
//    mod_scan     — g_mods (KickUpdateChecks iterates them)
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"
#include "mod_types.h"
#include "update_cache.h"     // UpdateInfo

// HTTP fetch timeouts. Fast = startup background scan (don't block
// the UI for slow servers). Slow = retry after the user explicitly
// clicks Refresh Mod List.
constexpr int  UPDATE_HTTP_TIMEOUT_FAST  = 5000;
constexpr int  UPDATE_HTTP_TIMEOUT_SLOW  = 10000;

// Cache TTL: skip a re-fetch if the last successful check completed
// within this window. Two hours strikes a balance between freshness
// and avoiding rate-limit hits on the GitHub API for users who
// relaunch the launcher repeatedly.
constexpr int  UPDATE_CACHE_TTL_SECONDS  = 2 * 3600;

// Posted to g_hwMain after every worker fetch completes (whether
// successful, timed out, or whatever). MainProc reacts by invalidating
// the mod-list row so any new "Update available" banner paints. Fires
// per-mod, not just once at the end of a batch — that way the UI
// reflects progress as each result comes in.
constexpr UINT MSG_UPDATE_CHECK_DONE     = WM_USER + 20;

// Kick off background fetches for every opted-in mod (those with a
// non-empty updateGithub or updateManifest field). force=true
// bypasses the TTL check so the user's Refresh Mod List button does
// a real refetch. Returns immediately; the workers run on their
// own threads.
void KickUpdateChecks(bool force);

// Read-only accessor for paint code. Returns nullptr if no cache
// entry exists for this folder. The pointer is invalidated by any
// subsequent call into mod_updates from the UI thread, so consumers
// should copy any fields they need.
const UpdateInfo* GetUpdateInfo(const wstring& folder);
