// ═══════════════════════════════════════════════════════════════════════
//  readme_reader.h — a reusable themed text reader (v1.6)
// ═══════════════════════════════════════════════════════════════════════
//
//  A small modal that renders a text file in the launcher's themed reader
//  (Georgia body / MakeReaderFont, wheel scroll, frame_modbanner chrome).
//  Distilled from the Help modal's reader — no Discord, no FAQ specifics.
//  Used by the plugin manager to show plugin/patch READMEs (v1.6 drag-drop
//  feature), and reusable anywhere a file needs a themed read.

#pragma once

#include "angiris_common.h"

// Open `filePath` in a themed reader titled `title`. Modal-owned pump;
// returns when closed (Close button or Esc). If the file is missing or
// unreadable, shows a short "couldn't open" message in the reader instead.
void ShowReadmeReader(HWND parent, const wstring& title, const wstring& filePath);
