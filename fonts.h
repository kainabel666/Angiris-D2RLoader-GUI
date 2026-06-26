// ═══════════════════════════════════════════════════════════════════════
//  fonts.h
// ═══════════════════════════════════════════════════════════════════════
//
//  Font loading + storage for the launcher.
//
//  This module owns:
//    • The PrivateFontCollection that holds every bundled .ttf
//    • The 17 GDI+ Font objects used by paint code (g_f*)
//    • The 5 GDI+ FontFamily objects used to construct those Fonts
//    • Process-private font registration via AddFontResourceEx
//    • The scan of assets\fonts\ on startup
//
//  This module does NOT own:
//    • Font construction at runtime sizes — CreateGdipFonts /
//      DestroyGdipFonts live in Angiris.cpp because they touch the
//      g_scale macro (SF) and write the layout-coupled
//      g_seedLabelLogicalW value. Those move with the paint code
//      in a later phase.
//    • The Font dropdown's user-facing array of font picks
//      (g_availableFonts/Families/Styles/Abbrevs) — toolbar UI
//      state, stays in Angiris.cpp.
//
//  All g_f* globals are written by CreateGdipFonts (in Angiris.cpp)
//  and read by paint code throughout. Storage lives here so the
//  extern declarations are visible to every translation unit that
//  paints.
//
//  Depends on core (for AppDir) and the Win32/GDI+ surface from
//  angiris_common.
//
// ═══════════════════════════════════════════════════════════════════════

#pragma once

#include "angiris_common.h"

// ── Process-private font registration ────────────────────────────────
//
// Bundled .ttf files are registered via AddFontResourceEx (FR_PRIVATE)
// so GDI+ can find them by name but they're invisible to other apps.
// The paths kept here are used by UnloadFonts to RemoveFontResourceEx
// each one back out at exit.
extern vector<wstring> g_loadedFonts;

// Persistent PrivateFontCollection holding every bundled .ttf for the
// app's lifetime. Originally LoadFonts used a throwaway local PFC per
// file just to extract family names, relying on AddFontResourceEx
// (FR_PRIVATE) to make later FontFamily(name) lookups resolve. That
// silently failed for the user-font override path on at least some
// systems — keeping the PFC alive lets us pass &g_pfc as the
// FontCollection arg to FontFamily(name), guaranteeing the lookup
// finds the file we just added.
extern Gdiplus::PrivateFontCollection* g_pfc;

// ── Font families (one per typeface) ─────────────────────────────────
extern Gdiplus::FontFamily* g_ffCinzel;
extern Gdiplus::FontFamily* g_ffCinzelBold;
extern Gdiplus::FontFamily* g_ffFell;
extern Gdiplus::FontFamily* g_ffExocet;     // D2 menu font (Exocet Blizzard)
extern Gdiplus::FontFamily* g_ffGeorgia;    // fallback when bundled fonts missing

// ── User font override ───────────────────────────────────────────────
//
// Created lazily from g_cfg.fontName when the user picks a font in the
// toolbar dropdown. When set, CreateGdipFonts uses this family (with
// g_userFontStyleOverride style bits) in place of every Exocet-family
// font in the UI. The Georgia-family fonts (italic body text) stay
// default — most custom fonts don't ship an italic face.
extern Gdiplus::FontFamily* g_userFontFamilyOverride;
extern INT                  g_userFontStyleOverride;

// ── GDI+ Font instances (constructed by CreateGdipFonts) ─────────────
//
// Exocet (D2 menu font) carries the launcher's identity; Georgia is
// used where dense legibility matters (cmd preview, mod description
// body, hero meta italics). All pixel sizes are LOGICAL — actual
// rasterization size = logical * g_scale.
extern Gdiplus::Font* g_fHeroName;    // Exocet 38px  (mod row title fallback)
extern Gdiplus::Font* g_fHeroMeta;    // Georgia italic, 14px
extern Gdiplus::Font* g_fTitle;       // Exocet 26px  (D2RLOADER wordmark)
extern Gdiplus::Font* g_fSubtitle;    // Exocet 11px  (subtitle lines)
extern Gdiplus::Font* g_fColHdr;      // Exocet 32px  (MODS — largest)
extern Gdiplus::Font* g_fColHdrMed;   // Exocet 24px  (most section titles at 75%)
extern Gdiplus::Font* g_fColHdrSm;    // Exocet 16px  (LOADER OPTIONS at 50%)
extern Gdiplus::Font* g_fExpHdr;      // Exocet 18px  (bottom expand-panel section titles)
extern Gdiplus::Font* g_fSubLbl;      // Exocet 11px  (sublabels)
extern Gdiplus::Font* g_fBtn;         // Exocet 13px  (button text)
extern Gdiplus::Font* g_fCmdArgs;     // Exocet 11px  (launch args preview)
extern Gdiplus::Font* g_fNav;         // Exocet 26px  (nav button text)
extern Gdiplus::Font* g_fNavSm;       // Exocet 18.2px  (Nexus/Update + expansion-panel buttons)
extern Gdiplus::Font* g_fBtnLaunch;   // Exocet 40px  (PLAY button — doubled)
extern Gdiplus::Font* g_fStatus;      // Georgia italic, 12px
extern Gdiplus::Font* g_fModName;     // Exocet 18px  (mod row name)
extern Gdiplus::Font* g_fModSub;      // Georgia italic, 12px
extern Gdiplus::Font* g_fModPath;     // Georgia, 11px

// ── Entry points ─────────────────────────────────────────────────────

// Scan assets\fonts\ for .ttf files. Each is registered with the
// process as a private font (FR_PRIVATE — invisible to other apps)
// and added to the persistent g_pfc PrivateFontCollection.
//
// In parallel, this fills out the four toolbar-dropdown arrays
// (g_availableFonts / Families / Styles / Abbrevs) which still live
// in Angiris.cpp — passed in by reference so this module doesn't
// need to know about the dropdown's data shape.
void LoadFonts(vector<wstring>& outFonts,
               vector<wstring>& outFamilies,
               vector<INT>&     outStyles,
               vector<wstring>& outAbbrevs);

// Unregister every bundled font and clear g_loadedFonts. Called at
// exit AND before an in-place self-update install (releases the
// .ttf file handles so the installer can overwrite them — except
// for the active font's mapping, which Windows holds onto past
// RemoveFontResourceEx; see RunTarExtract's TTF exclude).
void UnloadFonts();

// Try to construct a FontFamily by name; if it doesn't exist (font
// wasn't loaded), return the Georgia fallback so callers never see
// a null. NEVER returns nullptr unless even Georgia is missing.
Gdiplus::FontFamily* MakeFamily(const wchar_t* primary);
