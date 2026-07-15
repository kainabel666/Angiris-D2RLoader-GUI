// ═══════════════════════════════════════════════════════════════════════
//  loader_options_modal.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Themed modals shown by the LOADER OPTIONS panel's Basic Options and
//  Developer Options buttons. Each writes directly to D2RLoader.toml on
//  every change (apply-immediately semantics); there's no Save/Cancel
//  distinction because D2RLoader only reads its toml at launch time
//  anyway, so there's no in-game feedback to preserve or roll back.

#pragma once

#include "angiris_common.h"

// Open the Basic Options modal. Modal-owned pump; returns when the user
// closes the popup. Reads/writes g_loaderOpts + D2RLoader.toml directly.
void ShowBasicOptionsModal(HWND parent);

// Open the Developer Options modal. Same modal machinery as Basic; a
// different row list with a cascade group under "Enable Logging" (the
// 7 log-detail toggles grey out when logging is off).
void ShowDeveloperOptionsModal(HWND parent);
