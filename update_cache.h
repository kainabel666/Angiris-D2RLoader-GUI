// ═══════════════════════════════════════════════════════════════════════
//  update_cache.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Mod update tracking: the UpdateInfo data type and disk-cache I/O
//  for assets/update_cache.json.
//
//  UpdateInfo is the canonical record describing one mod's update
//  state — local version, last fetched remote version + manifest
//  fields, fetched-at timestamp, and the "skipped version" tag.
//  It's defined here (the persistence layer) because it predates
//  any consumer; mod_updates (Phase 3) will produce these values
//  via HTTP fetches, and the paint/dialog code consumes them.
//
//  g_updateInfo is the live map keyed by mod folder name. Cached
//  entries with a fetchedAt within UPDATE_CACHE_TTL_SECONDS are
//  reused on startup instead of re-fetching from GitHub.
//
//  Depends on core for ReadTextFile/WriteTextFile/JSON helpers.
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

struct UpdateInfo {
    bool    available    = false;   // remote version > local version
    bool    fetched      = false;   // true if we have a successful fetch
    bool    timedOut     = false;   // set on a timeout failure
    wstring localVersion;
    wstring remoteVersion;
    wstring changelog;
    wstring downloadUrl;
    wstring sourceUrl;
    wstring releaseDate;
    wstring sha256;                 // optional integrity check
    wstring skippedVersion;         // user clicked "Skip this version"
    time_t  fetchedAt    = 0;
    int     httpStatus   = 0;
};

// Live cache, keyed by mod folder name. All readers (paint code,
// menu, dialogs) and writers (the update check worker) share this
// instance through the extern.
extern map<wstring, UpdateInfo> g_updateInfo;

// Replace g_updateInfo from assets/update_cache.json. Missing file
// is silently treated as empty map.
void LoadUpdateCache();

// Serialize g_updateInfo back to assets/update_cache.json. Called
// after any update check completes so the cache survives a restart.
void SaveUpdateCache();
