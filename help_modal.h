// ═══════════════════════════════════════════════════════════════════════
//  help_modal.h
// ═══════════════════════════════════════════════════════════════════════
//
//  The modal opened by the Help nav button. A scrollable reader that
//  renders FAQ.txt, with a "Need More Help?" prompt and the D2RLoader
//  Discord button beneath it. Mirrors the About modal's reader view
//  (themed EDIT, wheel-scroll subclass, frame_modbanner border, nested
//  pump) but is reader-only — no hub, no view switching.

#pragma once

#include "angiris_common.h"

// Open the Help modal (FAQ reader). Modal-owned pump; returns when closed.
// `parent` is disabled while open.
void ShowHelpModal(HWND parent);
