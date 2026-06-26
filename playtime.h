// ═══════════════════════════════════════════════════════════════════════
//  playtime.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Per-mod playtime tracking. Each mod's cumulative session time and
//  last-played timestamp are persisted in assets/playtime.json keyed
//  by mod folder name (so the data survives reinstalls).
//
//  Recording happens once per session: the launcher polls D2R every
//  second after a mod is launched, records the elapsed seconds to the
//  selected mod's row when D2R exits, and writes the JSON. The
//  display surface (the hover tooltip) uses Format* helpers below
//  to turn the stored values into "2h 14m" / "3 days ago".
//
//  Depends on core for ReadTextFile/WriteTextFile/JSON helpers.
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

struct PlaytimeRec {
    uint64_t seconds   = 0;
    time_t   lastPlayed = 0;     // 0 == never
};

// Map keyed by mod folder name. Lives in playtime.cpp; modules that
// read it (hover tip paint, mod row badges) and write it (the launch
// monitor) all see the same instance through this extern.
extern map<wstring, PlaytimeRec> g_playtimes;

// Replace g_playtimes from assets/playtime.json. Missing file is
// silently treated as empty map.
void LoadPlaytimes();

// Serialize g_playtimes back to assets/playtime.json. Called from
// RecordPlaytime after every session, so the file is always
// up-to-date even if the launcher is killed unexpectedly.
void SavePlaytimes();

// Add a session's elapsed seconds to a mod's accumulator and stamp
// last_played to now. Persists immediately — playtime entries are
// small enough that the write cost is trivial.
void RecordPlaytime(const wstring& modFolder, uint64_t secondsToAdd);

// Format a raw second count as "2h 14m" / "32m" / "< 1m". Used by
// the hover tooltip.
wstring FormatPlaytime(uint64_t seconds);

// Format a last-played timestamp relative to now ("Just now",
// "5 minutes ago", "2 days ago"). Switches to absolute date
// (YYYY-MM-DD) after 30 days.
wstring FormatLastPlayed(time_t lastPlayed);
