// ═══════════════════════════════════════════════════════════════════════
//  about_modal.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Themed About modal — replaces the old "About button shell-opens
//  README.txt in Notepad" behaviour. Shows launcher + D2RLoader version
//  info and a set of links (Download D2RLoader, README, FAQ, Discord).
//  README and FAQ open in a scrollable in-modal text view rather than
//  bouncing out to Notepad.

#pragma once

#include "angiris_common.h"

// Open the About modal. Modal-owned message pump; returns when the user
// closes the popup.
void ShowAboutModal(HWND parent);
