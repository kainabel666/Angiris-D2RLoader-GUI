// ═══════════════════════════════════════════════════════════════════════
//  plugin_drop_ui.h — entry points for drag-drop plugin install (v1.6)
// ═══════════════════════════════════════════════════════════════════════
//
//  The WM_DROPFILES handlers call these after PeekZipKind identifies a
//  zip as a plugin. Each supplies the prompt dialogs and runs the full
//  inspect → prompt → execute flow via the routing core.

#pragma once

#include "angiris_common.h"

// Main-window drop of a plugin zip → GLOBAL scope install.
void HandleMainWindowPluginDrop(HWND parent, const wstring& zipPath);

// Plugin-manager drop of a plugin zip → MOD scope (the selected mod's
// folder name under <d2r>\mods\).
void HandlePluginManagerDrop(HWND parent, const wstring& zipPath,
                             const wstring& selectedModFolder);

// Bare .json patch drops (no zip, no manifest) → the patches folder.
// Main window = global scope; plugin manager = the selected mod (allowlist-
// gated). See HandleBarePatchDrop in plugin_install.
void HandleMainWindowPatchDrop(HWND parent, const wstring& jsonPath);
void HandlePluginManagerPatchDrop(HWND parent, const wstring& jsonPath,
                                  const wstring& selectedModFolder);

// Patch-bundle zip drops (manifest-less zip of .json files) → patches
// folder. Extracts only the .json files. Main window = global; plugin
// manager = the selected mod (each JSON allowlist-gated).
void HandleMainWindowPatchBundle(HWND parent, const wstring& zipPath);
void HandlePluginManagerPatchBundle(HWND parent, const wstring& zipPath,
                                    const wstring& selectedModFolder);
