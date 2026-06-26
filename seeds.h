// ═══════════════════════════════════════════════════════════════════════
//  seeds.h
// ═══════════════════════════════════════════════════════════════════════
//
//  The launch-flags column exposes a Seed dropdown populated from
//  assets/seeds.json. The file holds two arrays:
//
//    "seeds"   — fixed presets, edited only by the user / a packaged
//                seeds.json from a mod author. Pass through unchanged
//                on save.
//
//    "recent"  — rolling 3-slot history of values the user typed
//                manually. Slot 0 = oldest (rendered as "Recent1"),
//                slot 2 = newest (rendered as "Recent3"). The
//                launcher rewrites this array whenever a new value
//                is committed via CommitTypedSeedToRecents.
//
//  In memory the two lists are kept separate (g_seedNames /
//  g_seedValues for presets, g_recentSeeds for the rolling history).
//  The dropdown renders them as one list with recents shown first
//  (newest at top) for fast re-pick.
//
//  Depends on core for ReadTextFile/WriteTextFile/JSON helpers.
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// Presets — paired arrays. names[i] is the display label for values[i].
// When the JSON has no "name" field for an entry, the value is used as
// the label too. LoadSeedsJson seeds these from a built-in fallback set
// if seeds.json is missing or has no "seeds" array.
extern vector<wstring> g_seedNames;
extern vector<wstring> g_seedValues;

// Rolling history of user-typed values. 0..3 entries, back == newest.
extern vector<wstring> g_recentSeeds;

// Last index the user picked from the dropdown — used as the default
// when the user re-checks the Seed checkbox after having cleared it.
// Not persisted; resets to 0 on launch.
extern int g_lastSeedIdx;

// Load presets and recents from assets/seeds.json. Missing file or
// empty "seeds" array falls back to a built-in preset list.
void LoadSeedsJson();

// Rewrite assets/seeds.json preserving the presets and updating the
// recents array. Called by CommitTypedSeedToRecents.
void SaveSeedsJson();

// Parse a JSON array body ([...]) into a list of {name, value} pairs
// and emit them via the callback. Used for both the "seeds" and
// "recent" arrays since they share the same entry shape.
void ParseSeedArray(const wstring& body,
                    void (*emit)(const wstring& name, const wstring& value,
                                 void* user),
                    void* user);

// Add `value` to the recents history, shifting older entries down.
// If `value` matches an existing preset or recent, it's not
// duplicated. Returns true if the recents list was changed (caller
// can decide whether to redraw / re-save).
bool CommitTypedSeedToRecents(const wstring& value);

// Return the index in g_seedValues whose value matches `val`, or
// -1 when nothing matches (e.g. a previously saved seed that no
// longer exists in the dropdown list).
int FindSeedIndexForValue(const wstring& val);
