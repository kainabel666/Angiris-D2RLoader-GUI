// ═══════════════════════════════════════════════════════════════════════
//  repo_browser.h — repository browser modal (v1.6.2)
// ═══════════════════════════════════════════════════════════════════════
//
//  A themed modal launched from the Plugin Manager. Fetches the catalog
//  (repository.cpp), shows entries in a list on the left with a detail panel
//  on the right, and a scope switch (Global / Mod Local) + a mod dropdown.
//  Installing an entry downloads + verifies + routes it through the normal
//  drop-install flow (see InstallRepoEntry / HandleRepoInstall).
//
//  `defaultMod` is the mod selected when the Plugin Manager opened — it's the
//  dropdown's initial selection (where a drop would currently go).

#pragma once

#include "angiris_common.h"

void ShowRepoBrowser(HWND parent, const wstring& defaultMod);
