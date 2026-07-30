// ═══════════════════════════════════════════════════════════════════════
//  mods_modal.h — Mods list modal + modinfo editor (v1.6)
// ═══════════════════════════════════════════════════════════════════════
//
//  Lists every folder under <D2R>\mods\ (including ones WITHOUT a
//  modinfo.json, greyed + "---- NO MODINFO FOUND"). Right-click a row to
//  view/edit its modinfo (or create one for a folder that lacks it). The
//  editor is a form of all modinfo fields with a read-only/edit state
//  machine. Replaces the Mods nav button's old open-in-Explorer behavior.

#pragma once

#include "angiris_common.h"

void ShowModsModal(HWND parent);
