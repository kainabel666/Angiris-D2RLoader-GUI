// ═══════════════════════════════════════════════════════════════════════
//  section_modal.h
// ═══════════════════════════════════════════════════════════════════════
//
//  The modal opened by each of the four expand-panel SECTION buttons
//  (References / Tools / Downloads / Tutorials). One parameterised modal
//  serves all four: it's handed a title and a list of {label, action}
//  items and renders them as a vertical stack of buttons. An action is
//  either "open a URL in the browser" or "launch a resolved tool exe".
//
//  Mirrors the modal machinery of loader_options_modal (registered class,
//  TOPMOST popup over a disabled parent, frame_modbanner border, nested
//  message pump, ESC / click-Close to dismiss) but with a simpler content
//  model — no per-row edit controls, just clickable action buttons.

#pragma once

#include "angiris_common.h"

// One item in a section modal: a labelled button plus what it does.
enum class SectionActionKind {
    OpenUrl,      // ShellExecute the url
    LaunchTool,   // resolve + launch a tool exe (see tool_resolver)
};

struct SectionItem {
    const wchar_t*    label;    // button caption
    SectionActionKind kind;
    const wchar_t*    url;      // OpenUrl: the address
    // LaunchTool fields (ignored for OpenUrl):
    wstring*          toolCachedPath;   // &g_cfg.toolXxx — resolved path cache
    const wchar_t*    toolExeHint;      // filename to search for
    const wchar_t*    toolFriendly;     // name shown in the "locate" prompt
};

// Open the section modal for the given title + item list. Modal-owned
// pump; returns when the user closes it. `parent` is disabled while open.
void ShowSectionModal(HWND parent, const wchar_t* title,
                      const SectionItem* items, int itemCount);
