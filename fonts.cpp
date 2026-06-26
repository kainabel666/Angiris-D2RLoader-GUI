// ═══════════════════════════════════════════════════════════════════════
//  fonts.cpp — see fonts.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "fonts.h"
#include "core.h"   // AppDir

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
