// ═══════════════════════════════════════════════════════════════════════
//  logs_modal.h — Logs viewer modal (v1.6)
// ═══════════════════════════════════════════════════════════════════════
//
//  Lists <D2R>\logs\ files with a right-click "Copy File" (CF_HDROP) so
//  users can paste a log straight into Discord. Replaces the Logs nav
//  button's old open-in-Explorer behavior.

#pragma once

#include "angiris_common.h"

void ShowLogsModal(HWND parent);
