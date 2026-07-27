// ═══════════════════════════════════════════════════════════════════════
//  fonts.cpp — see fonts.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "fonts.h"
#include "core.h"     // AppDir, g_dpiScale
#include "scaling.h"  // g_scale (reader font follows UI scale)
#include "config.h"   // g_cfg.fontName (opt-in reader font)
#include "layout.h"   // LayoutReaderUseAppFont

// ── Storage for the extern declarations in fonts.h ───────────────────

vector<wstring>                  g_loadedFonts;
Gdiplus::PrivateFontCollection*  g_pfc                    = nullptr;
Gdiplus::FontFamily*             g_ffCinzel               = nullptr;
Gdiplus::FontFamily*             g_ffCinzelBold           = nullptr;
Gdiplus::FontFamily*             g_ffFell                 = nullptr;
Gdiplus::FontFamily*             g_ffExocet               = nullptr;
Gdiplus::FontFamily*             g_ffGeorgia              = nullptr;
Gdiplus::FontFamily*             g_userFontFamilyOverride = nullptr;
INT                              g_userFontStyleOverride  = Gdiplus::FontStyleRegular;

Gdiplus::Font*  g_fHeroName   = nullptr;
Gdiplus::Font*  g_fHeroMeta   = nullptr;
Gdiplus::Font*  g_fTitle      = nullptr;
Gdiplus::Font*  g_fSubtitle   = nullptr;
Gdiplus::Font*  g_fColHdr     = nullptr;
Gdiplus::Font*  g_fColHdrMed  = nullptr;
Gdiplus::Font*  g_fColHdrSm   = nullptr;
Gdiplus::Font*  g_fExpHdr     = nullptr;
Gdiplus::Font*  g_fSubLbl     = nullptr;
Gdiplus::Font*  g_fBtn        = nullptr;
Gdiplus::Font*  g_fCmdArgs    = nullptr;
Gdiplus::Font*  g_fNav        = nullptr;
Gdiplus::Font*  g_fNavSm      = nullptr;
Gdiplus::Font*  g_fBtnLaunch  = nullptr;
Gdiplus::Font*  g_fStatus     = nullptr;
Gdiplus::Font*  g_fModName    = nullptr;
Gdiplus::Font*  g_fModSub     = nullptr;
Gdiplus::Font*  g_fModPath    = nullptr;

// ── Private helpers ──────────────────────────────────────────────────

// Try to register one font file. Successful registrations land in
// g_loadedFonts so UnloadFonts can RemoveFontResourceEx them back
// out at exit / pre-self-update.
static void TryLoadFont(const wstring& filename) {
    wstring path = AppDir() + L"\\assets\\fonts\\" + filename;
    if (GetFileAttributes(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    if (AddFontResourceEx(path.c_str(), FR_PRIVATE, nullptr) > 0) {
        g_loadedFonts.push_back(path);
    }
}

// Abbreviate a font filename stem to a compact label for the toolbar
// dropdown. First three chars of the first segment, "-", first three
// of the last segment. "Cinzel-Bold" → "Cin-Bol",
// "Exocet-Blizzard-Medium" → "Exo-Med". Files with no "-" get the
// first four chars.
//
// Lives here because LoadFonts uses it to populate the abbrev array
// it returns. Logically pairs with the font-scan output.
static wstring AbbreviateFontName(const wstring& name) {
    if (name.empty()) return L"";
    vector<wstring> parts;
    wstring cur;
    for (wchar_t c : name) {
        if (c == L'-') {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
        } else cur.push_back(c);
    }
    if (!cur.empty()) parts.push_back(cur);
    if (parts.empty()) return name.substr(0, 4);
    wstring out = parts[0].substr(0, 3);
    if (parts.size() >= 2) out += L"-" + parts.back().substr(0, 3);
    return out;
}

// ── Entry points ─────────────────────────────────────────────────────

void LoadFonts(vector<wstring>& outFonts,
               vector<wstring>& outFamilies,
               vector<INT>&     outStyles,
               vector<wstring>& outAbbrevs) {
    outFonts.clear();
    outFamilies.clear();
    outStyles.clear();
    outAbbrevs.clear();
    wstring dir = AppDir() + L"\\assets\\fonts\\";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*.ttf").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        wstring name = fd.cFileName;
        wstring path = dir + name;

        // Register with the process so later FontFamily(name) lookups
        // succeed (and so the existing CreateGdipFonts code, which
        // hard-codes face names like "Cinzel", continues to find the
        // files it expects).
        TryLoadFont(name);

        // Also add to the persistent PFC. This is the bulletproof path
        // for the user-font override: FontFamily(name, g_pfc) finds the
        // file we just registered, where FontFamily(name) without a
        // collection silently misses it on some systems (the bug that
        // made every user font selection render in Exocet anyway).
        if (g_pfc) g_pfc->AddFontFile(path.c_str());

        // Pull the family name via a throwaway PrivateFontCollection
        // holding ONLY this file — g_pfc dedupes shared families
        // (Cinzel-Regular and Cinzel-Bold both report family "Cinzel"
        // and become one entry there), so we can't safely use it to
        // attribute a family to a specific file. A per-file PFC holds
        // exactly one family, no ambiguity.
        wstring family;
        {
            Gdiplus::PrivateFontCollection pfc;
            if (pfc.AddFontFile(path.c_str()) == Gdiplus::Ok) {
                INT cnt = pfc.GetFamilyCount();
                if (cnt > 0) {
                    Gdiplus::FontFamily* fams = new Gdiplus::FontFamily[cnt];
                    INT found = 0;
                    pfc.GetFamilies(cnt, fams, &found);
                    if (found > 0) {
                        WCHAR nm[LF_FACESIZE] = { 0 };
                        if (fams[0].GetFamilyName(nm) == Gdiplus::Ok) family = nm;
                    }
                    delete[] fams;
                }
            }
        }
        outFamilies.push_back(family);

        // Strip extension for the display name AND derive the FontStyle
        // bits from the filename suffix. Family alone is ambiguous when
        // multiple .ttf files share a family — Cinzel-Regular.ttf and
        // Cinzel-Bold.ttf both report "Cinzel", and without explicit
        // style bits the preview would draw both at the same weight.
        size_t dot = name.find_last_of(L'.');
        if (dot != wstring::npos) name.resize(dot);

        wstring lc = name;
        for (auto& c : lc) c = (wchar_t)towlower(c);
        INT style = Gdiplus::FontStyleRegular;
        if (lc.find(L"bold")  != wstring::npos)  style |= Gdiplus::FontStyleBold;
        if (lc.find(L"black") != wstring::npos)  style |= Gdiplus::FontStyleBold;
        if (lc.find(L"heavy") != wstring::npos)  style |= Gdiplus::FontStyleBold;
        if (lc.find(L"italic")  != wstring::npos
            || lc.find(L"oblique") != wstring::npos)
            style |= Gdiplus::FontStyleItalic;
        outStyles.push_back(style);

        outFonts.push_back(name);
        outAbbrevs.push_back(AbbreviateFontName(name));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

void UnloadFonts() {
    for (const auto& p : g_loadedFonts)
        RemoveFontResourceEx(p.c_str(), FR_PRIVATE, nullptr);
    g_loadedFonts.clear();
}

Gdiplus::FontFamily* MakeFamily(const wchar_t* primary) {
    Gdiplus::FontFamily* f = new Gdiplus::FontFamily(primary);
    if (f->GetLastStatus() != Gdiplus::Ok) {
        delete f;
        f = new Gdiplus::FontFamily(L"Georgia");
        if (f->GetLastStatus() != Gdiplus::Ok) {
            delete f;
            return nullptr;
        }
    }
    return f;
}

// ── Reader body font (Georgia HFONT) ─────────────────────────────────────
// The README/FAQ readers are Win32 EDIT controls, which take a GDI HFONT,
// not a GDI+ Font. Georgia is the launcher's long-form reading face (the
// same family used for dense text elsewhere), so the readers get a themed
// but still-legible font instead of the stock system one. The user's
// DISPLAY font (Exocet/Cinzel/etc.) is deliberately NOT used here — those
// are decorative faces that read poorly in paragraphs of documentation.
//
// Caller owns the returned HFONT and must DeleteObject it (the modals do
// so on WM_DESTROY). Point size is scaled by the current DPI so the reader
// matches the rest of the UI. Falls back to DEFAULT_GUI_FONT if creation
// fails for any reason.
HFONT MakeReaderFont(int pointSize) {
    // EDIT/GDI want a height in logical units; negative = character height
    // (excludes internal leading), which is the usual choice for point sizes.
    //
    // Scaled by g_scale (= g_userScale * g_dpiScale) so the reader follows
    // the UI-scale slider along with the rest of the modal (window, rects,
    // buttons) and the chrome fonts. The About/Help modals were converted
    // to full g_scale sizing in v1.5.1 so everything grows together.
    int px = (int)(pointSize * 96.0 / 72.0 * g_scale + 0.5);

    // Face selection. Default is Georgia (legible for long documentation).
    // If the user opted in via user_layout.json (reader_use_app_font: true)
    // AND has actually chosen a display font, honor that choice instead —
    // accepting that decorative faces read less comfortably in paragraphs.
    const wchar_t* face = L"Georgia";
    if (LayoutReaderUseAppFont(false) && !g_cfg.fontName.empty()) {
        face = g_cfg.fontName.c_str();
    }

    HFONT hf = CreateFontW(
        -px, 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_ROMAN | VARIABLE_PITCH,
        face);
    // If the chosen face fails to create, fall back to Georgia, then to the
    // stock GUI font — the reader must always have a usable font.
    if (!hf && face != nullptr && lstrcmpW(face, L"Georgia") != 0) {
        hf = CreateFontW(
            -px, 0, 0, 0,
            FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FF_ROMAN | VARIABLE_PITCH,
            L"Georgia");
    }
    if (!hf) hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    return hf;
}
