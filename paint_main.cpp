// ═══════════════════════════════════════════════════════════════════════
//  paint_main.cpp — see paint_main.h for the public entry
// ═══════════════════════════════════════════════════════════════════════
//
//  Phase 7c extraction. PaintBody is the public entry point;
//  PaintLeftRail / PaintModDescription / PaintLaunchOptions /
//  PaintBottomPanel / PaintToolbarControl / BreakLaunchArgsAtDash
//  are file-static helpers called by PaintBody.
//
//  Many globals are read from Angiris.cpp via ui_state.h and
//  layout.h. Phase 7d will move the rect-computing code (Layout,
//  ComputeBodyLayout, etc.) into layout.cpp, eliminating this
//  read-only coupling.

#include "paint_main.h"
#include "core.h"             // g_hwMain, g_bottomExpanded
#include "scaling.h"          // S, SF, U, g_scale, MemDC
#include "colors.h"           // Tok::*, g_colorPresets
#include "assets.h"           // AssetImage, DrawAsset*
#include "fonts.h"            // g_f* font globals
#include "layout.h"           // LO::*, BodyLayout, ComputeBodyLayout, Body*Rect
#include "ui_state.h"         // g_loaderOpts, g_tbPressed, rect globals, seed input state, font caches
#include "paint_helpers.h"    // FillSolid, DrawGoldText, DrawFlagCheckbox, OPDrawBtnFrame, PaintTopOrnament, PaintCornerAccents
#include "buttons.h"          // ButtonKind (for PaintBody's button list awareness)
#include "config.h"           // g_cfg.d2rPath (path bar text)
#include "update_cache.h"     // g_updateInfo (per-mod update status badges)
#include "mod_scan.h"         // g_mods, g_selMod
#include "mod_config.h"       // g_modSettings (per-mod launch flags)
#include "launch_flags.h"     // FLAGS table (flag labels + locked status)
#include "seeds.h"            // g_seedNames, g_seedValues, etc. (seed dropdown contents)
#include "launcher_self_update.h"   // g_launcherUpdateAvailable (header badge)

// Forward declarations for the file-static helpers that PaintBody
// orchestrates. Sources follow the order PaintBody calls them in.
static void    PaintLeftRail(Gdiplus::Graphics& g, int W, int H);
static void    PaintModDescription(Gdiplus::Graphics& g, const BodyLayout& B);
static wstring BreakLaunchArgsAtDash(Gdiplus::Graphics& g, const wstring& s,
                                     Gdiplus::Font* f, int maxWidth);
static void    PaintLaunchOptions(Gdiplus::Graphics& g, const BodyLayout& B);
static void    PaintBottomPanel(Gdiplus::Graphics& g, int W, int H);
static void    PaintToolbarControl(Gdiplus::Graphics& g, const RECT& r,
                                   const wchar_t* glyph, bool pressed,
                                   bool isClose);

static void PaintLeftRail(Graphics& g, int /*W*/, int /*H*/) {
    // The D2RLOADER wordmark + "Diablo II: Resurrected / Mod Launcher"
    // subtitle are all baked into logo_d2rloader.png (drawn earlier in
    // PaintBody). No live text needed here for the logo.
    using namespace LO;
    StringFormat sfC;
    sfC.SetAlignment(StringAlignmentCenter);
    sfC.SetLineAlignment(StringAlignmentCenter);
    SolidBrush gold(Tok::Gold);
    SolidBrush dim(Tok::GoldDim);

    // LOADER OPTIONS section sublabel + the two dropdown rows
    LoaderOptHits L = ComputeLoaderOptRects();
    StringFormat sfL;
    sfL.SetAlignment(StringAlignmentNear);
    sfL.SetLineAlignment(StringAlignmentCenter);
    SolidBrush sublbl(Tok::GoldDim);

    // ── Backdrop: bg_loader_options.png ─────────────────────────────────
    // Section stack (top → bottom):
    //   LOADER OPTIONS header
    //   Loader Dir path bar
    //   Stash Tabs dropdown
    //   Plugins button   (a real HWND, not painted here)
    // The backdrop spans from above the header to just below the
    // Plugins button. Bottom is derived from g_hwLoaderPlugins's
    // position rather than g_stashDropdownRect so the backdrop
    // grows to cover the button properly.
    constexpr int LO_HDR_H    = 40;
    constexpr int LO_HDR_PAD  = 8;     // pad between header and path bar
    constexpr int LO_BG_PADT  = 8;     // breathing room above header
    constexpr int LO_BG_PADB  = 12;    // breathing room below Plugins button
    int hdrY  = g_loaderDirRect.top - LO_HDR_PAD - LO_HDR_H;
    int bgX = g_loaderDirRect.left - 12;
    int bgY = hdrY - LO_BG_PADT;
    int bgW = 300;     // wider than the content column, but left X stays anchored
    // Section bottom comes from the Plugins button's HWND rect — convert
    // to client-rail logical coords via GetWindowRect + ScreenToClient
    // would be heavy here, so just derive from g_stashDropdownRect plus
    // the known gap + Plugins art height.
    int pluginsBot = g_stashDropdownRect.bottom + 6 + 54;
    int bgH = (pluginsBot - hdrY) + LO_BG_PADT + LO_BG_PADB;
    if (Gdiplus::Bitmap* lobg = AssetImage(L"bg_loader_options.png")) {
        InterpolationMode prev = g.GetInterpolationMode();
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(lobg, bgX, bgY, bgW, bgH);
        g.SetInterpolationMode(prev);
    }

    // Header sits above the Loader Dir path bar. NoWrap + a slightly
    // wider rect (the path bar itself is 233 logical; the header was
    // wrapping to two lines at 150% DPI × 100% UI scale because the
    // rendered "LOADER OPTIONS" hits ~250 logical at SF(14) caps).
    StringFormat sfLOHdr;
    sfLOHdr.SetAlignment(StringAlignmentNear);
    sfLOHdr.SetLineAlignment(StringAlignmentCenter);
    sfLOHdr.SetFormatFlags(sfLOHdr.GetFormatFlags() | StringFormatFlagsNoWrap);
    g.DrawString(L"LOADER OPTIONS", -1, g_fColHdrSm,
                 RectF((REAL)(g_loaderDirRect.left + 12), (REAL)hdrY,
                       275.0f,
                       (REAL)LO_HDR_H),
                 &sfLOHdr, &gold);

    // One dropdown row: "Stash Tabs  [ N  ▾ ]"
    //
    // The label is inset 10 px from the row's left edge, and the value
    // box is inset 60 px from the row's right edge — both moved inward
    // by 10 px each (the previous layout was 0 / 50), bringing the label
    // and value 20 logical px closer to each other. loValueBox below
    // returns matching coordinates so popup-anchor and paint stay synced.
    auto drawDD = [&](const RECT& r, const wchar_t* label, int value) {
        SolidBrush textBr(Tok::TextParchment);
        SolidBrush valBr(Tok::Gold);

        // Value box on the right (~70 px wide, sitting 60 px in from the
        // row's right edge). The label rect extends from r.left + 10
        // (10 px inset) up to just before the value box, with NoWrap so
        // long labels like "Dmg Display" stay on a single line at any
        // scale instead of breaking into "DMG / DISPLAY".
        constexpr int LABEL_INSET_L = 10;
        constexpr int VALUE_INSET_R = 60;
        int boxW = 70;
        int bx = r.right - boxW - VALUE_INSET_R;
        int by = r.top + 2;
        int bw = boxW;
        int bh = (r.bottom - r.top) - 4;

        StringFormat sfLbl;
        sfLbl.SetAlignment(StringAlignmentNear);
        sfLbl.SetLineAlignment(StringAlignmentCenter);
        sfLbl.SetFormatFlags(sfLbl.GetFormatFlags() | StringFormatFlagsNoWrap);
        int labelL = r.left + LABEL_INSET_L;
        g.DrawString(label, -1, g_fBtn,
                     RectF((REAL)labelL, (REAL)r.top,
                           (REAL)(bx - 6 - labelL),
                           (REAL)(r.bottom - r.top)),
                     &sfLbl, &textBr);

        // text_box.png provides the bronze chrome; dropdown_chevron.png
        // is the chevron glyph. Box shifted 50 px LEFT from the row's
        // right edge.
        if (Gdiplus::Bitmap* tb = AssetImage(L"text_box.png")) {
            InterpolationMode prev = g.GetInterpolationMode();
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.DrawImage(tb, bx, by, bw, bh);
            g.SetInterpolationMode(prev);
        } else {
            OPDrawBtnFrame(g, bx, by, bw, bh, false);
        }

        wchar_t buf[16]; swprintf(buf, 16, L"%d", value);
        StringFormat sfC2;
        sfC2.SetAlignment(StringAlignmentCenter);
        sfC2.SetLineAlignment(StringAlignmentCenter);
        // Leave room on the right for the chevron asset
        g.DrawString(buf, -1, g_fBtn,
                     RectF((REAL)bx, (REAL)by, (REAL)(bw - 22), (REAL)bh),
                     &sfC2, &valBr);

        // Chevron at the right edge
        if (Gdiplus::Bitmap* ch = AssetImage(L"dropdown_chevron.png")) {
            int chW = (int)ch->GetWidth();
            int chH = (int)ch->GetHeight();
            // Fit to half the box height so it doesn't dominate
            int targetH = bh - 4;
            int targetW = chW * targetH / chH;
            int cx = bx + bw - targetW - 4;
            int cy = by + (bh - targetH) / 2;
            InterpolationMode prev = g.GetInterpolationMode();
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.DrawImage(ch, cx, cy, targetW, targetH);
            g.SetInterpolationMode(prev);
        } else {
            // Programmatic fallback: Unicode ▾
            StringFormat sfR;
            sfR.SetAlignment(StringAlignmentCenter);
            sfR.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(L"\u25BE", -1, g_fBtn,
                         RectF((REAL)(bx + bw - 16), (REAL)by, 16.0f, (REAL)bh),
                         &sfR, &valBr);
        }
    };
    drawDD(L.stash, L"Stash Tabs",  g_loaderOpts.extraSharedTabs);
    // DMG Display dropdown removed — the Plugins button (a real HWND)
    // now occupies the slot it used to fill in the Loader Options stack.

    // ── Loader Dir path bar ─────────────────────────────────────────────
    // Drawn programmatically (no Win32 HWND). text_box.png as backdrop,
    // path text rendered with ellipsis trimming so long paths stay readable.
    {
        const RECT& r = g_loaderDirRect;
        int rw = r.right - r.left;
        int rh = r.bottom - r.top;
        if (Gdiplus::Bitmap* tb = AssetImage(L"text_box.png")) {
            InterpolationMode prev = g.GetInterpolationMode();
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.DrawImage(tb, r.left, r.top, rw, rh);
            g.SetInterpolationMode(prev);
        } else {
            SolidBrush bg(Tok::BgDeep);
            g.FillRectangle(&bg, r.left, r.top, rw, rh);
            Pen border(Tok::Bronze, 1.0f);
            g.DrawRectangle(&border, r.left, r.top, rw - 1, rh - 1);
        }
        StringFormat sfPath;
        sfPath.SetAlignment(StringAlignmentNear);
        sfPath.SetLineAlignment(StringAlignmentCenter);
        sfPath.SetTrimming(StringTrimmingEllipsisPath);
        SolidBrush pathBr(Tok::TextParchment);
        g.DrawString(g_cfg.d2rPath.c_str(), -1, g_fBtn,
                     RectF((REAL)(r.left + 8), (REAL)r.top,
                           (REAL)(rw - 16), (REAL)rh),
                     &sfPath, &pathBr);
    }
}

static void PaintModDescription(Graphics& g, const BodyLayout& B) {
    using namespace LO;
    // No background fill / border drawn here — frame_panel_right.png (painted
    // earlier in PaintBody) supplies the panel chrome. The stone shows
    // through inside the panel where the asset is transparent.

    // Header
    StringFormat sfL;
    sfL.SetAlignment(StringAlignmentNear);
    sfL.SetLineAlignment(StringAlignmentCenter);
    SolidBrush gold(Tok::Gold);
    g.DrawString(L"MOD DESCRIPTION", -1, g_fColHdrMed,
                 RectF((REAL)(B.descX + 12), (REAL)(B.descY + 4),
                       (REAL)(B.descW - 24), 44.0f),
                 &sfL, &gold);
    // Bronze separator under the header
    Pen sep(Tok::Bronze, 1.0f);
    g.DrawLine(&sep,
               B.descX + 12, B.descY + 34,
               B.descX + B.descW - 12, B.descY + 34);

    // Body text — prefer overview, fall back to description
    if (g_selMod >= 0 && g_selMod < (int)g_mods.size()) {
        const ModInfo& mod = g_mods[g_selMod];
        const wstring& body = !mod.overview.empty() ? mod.overview : mod.description;
        if (!body.empty()) {
            StringFormat sfBody;
            sfBody.SetAlignment(StringAlignmentNear);
            sfBody.SetLineAlignment(StringAlignmentNear);
            sfBody.SetTrimming(StringTrimmingWord);
            SolidBrush textBr(Tok::TextParchment);
            g.DrawString(body.c_str(), -1, g_fBtn,
                         RectF((REAL)(B.descX + 12), (REAL)B.descBodyY,
                               (REAL)(B.descW - 24), (REAL)B.descBodyH),
                         &sfBody, &textBr);
        }

        // Update-available bar at the bottom of the panel
        auto it = g_updateInfo.find(mod.folder);
        if (it != g_updateInfo.end() && it->second.available) {
            int bx = B.descX + 8;
            int by = B.descUpdateBarY;
            int bw = B.descW - 16;
            int bh = B.descUpdateBarH;
            SolidBrush barBg(GPA(64, 0xC8, 0xA8, 0x4B));
            g.FillRectangle(&barBg, bx, by, bw, bh);
            Pen barBorder(Tok::GoldDim, 1.0f);
            g.DrawRectangle(&barBorder, bx, by, bw - 1, bh - 1);

            wstring msg = L"\u2191 Update available: " + it->second.localVersion
                        + L" \u2192 " + it->second.remoteVersion;
            g.DrawString(msg.c_str(), -1, g_fBtn,
                         RectF((REAL)(bx + 8), (REAL)by,
                               (REAL)(bw - 80), (REAL)bh),
                         &sfL, &gold);
            // [Details] link on the right
            StringFormat sfR;
            sfR.SetAlignment(StringAlignmentFar);
            sfR.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(L"[Details]", -1, g_fBtn,
                         RectF((REAL)bx, (REAL)by,
                               (REAL)(bw - 8), (REAL)bh),
                         &sfR, &gold);
        }
    } else {
        // No mod selected — show a hint
        StringFormat sfC;
        sfC.SetAlignment(StringAlignmentCenter);
        sfC.SetLineAlignment(StringAlignmentCenter);
        SolidBrush dim(Tok::TextDim);
        g.DrawString(L"(Select a mod to view its description)",
                     -1, g_fBtn,
                     RectF((REAL)B.descX, (REAL)B.descBodyY,
                           (REAL)B.descW, (REAL)B.descBodyH),
                     &sfC, &dim);
    }
}

static wstring BreakLaunchArgsAtDash(Graphics& g, const wstring& s,
                                     Font* font, StringFormat* fmt,
                                     REAL maxWidth) {
    if (s.empty()) return s;
    // Tokenize: split on whitespace, then group so each "-flag" pulls
    // its (optional) following non-flag argument with it. Avoids
    // breaking between "-mod" and "ROK".
    vector<wstring> tokens;
    {
        size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && iswspace(s[i])) ++i;
            if (i >= s.size()) break;
            size_t start = i;
            while (i < s.size() && !iswspace(s[i])) ++i;
            tokens.push_back(s.substr(start, i - start));
        }
    }
    // Coalesce: a token that does NOT start with '-' attaches to the
    // previous token via a space.
    vector<wstring> groups;
    for (auto& t : tokens) {
        if (!groups.empty() && !t.empty() && t[0] != L'-') {
            groups.back() += L" " + t;
        } else {
            groups.push_back(t);
        }
    }
    // Greedy line packing
    wstring out;
    wstring line;
    auto measure = [&](const wstring& str) -> REAL {
        if (str.empty()) return 0.0f;
        RectF r;
        g.MeasureString(str.c_str(), -1, font,
                        RectF(0, 0, 4096, 4096), fmt, &r);
        return r.Width;
    };
    for (size_t i = 0; i < groups.size(); ++i) {
        wstring candidate = line.empty() ? groups[i]
                                         : (line + L" " + groups[i]);
        if (measure(candidate) <= maxWidth || line.empty()) {
            line = candidate;
        } else {
            // Group doesn't fit on current line — push current and
            // start a new line with this group.
            if (!out.empty()) out += L"\n";
            out += line;
            line = groups[i];
        }
    }
    if (!line.empty()) {
        if (!out.empty()) out += L"\n";
        out += line;
    }
    return out;
}

static void PaintLaunchOptions(Graphics& g, const BodyLayout& B) {
    using namespace LO;

    // No background fill / border drawn here — frame_panel_right.png supplies
    // the panel chrome. Stone shows through where the asset is transparent.

    // Header
    StringFormat sfL;
    sfL.SetAlignment(StringAlignmentNear);
    sfL.SetLineAlignment(StringAlignmentCenter);
    SolidBrush gold(Tok::Gold);
    g.DrawString(L"LAUNCH OPTIONS", -1, g_fColHdrMed,
                 RectF((REAL)(B.loX + 12), (REAL)(B.loY + 4),
                       (REAL)(B.loW - 24), 44.0f),
                 &sfL, &gold);
    Pen sep(Tok::Bronze, 1.0f);
    g.DrawLine(&sep,
               B.loX + 12, B.loY + 50,
               B.loX + B.loW - 12, B.loY + 50);

    // Flag checkboxes
    SolidBrush textBr(Tok::TextParchment);
    SolidBrush dimBr(Tok::TextDim);
    for (int i = 0; i < (int)(sizeof(FLAGS) / sizeof(FLAGS[0])); ++i) {
        const FlagDef& f = FLAGS[i];
        RECT fr = BodyFlagRect(B, i);
        bool checked = g_modSettings.*(f.member);

        // Checkbox glyph (27×28 native asset, or programmatic fallback)
        constexpr int CB_H = 28;
        int cbY = fr.top + ((fr.bottom - fr.top) - CB_H) / 2;
        DrawFlagCheckbox(g, (REAL)fr.left, (REAL)cbY, checked, false, f.isLocked);

        // Label to the right of the box (18 px Exocet, larger than other
        // UI text so it reads clearly inside the larger panel chrome).
        // NoWrap so multi-word labels ("Reset Maps", "Skip Intro") stay
        // on a single line at every scale — wrap was breaking them into
        // "RESET / MAPS" at 150% DPI × 100% UI. The rect width has an
        // extra 10 logical px past the cell's right edge — GDI+'s
        // MeasureString slightly underestimates Exocet at high DPI,
        // and without the extra room "Skip Intro" was clipping to
        // "Skip Intr". The +10 only consumes space if the label
        // needs it (NoWrap + left-aligned), and the next column's
        // own label rect starts well to the right of where this one
        // extends, so there's no visible overlap.
        SolidBrush* lblBr = f.isLocked ? &dimBr : &textBr;
        StringFormat sfFlag;
        sfFlag.SetAlignment(StringAlignmentNear);
        sfFlag.SetLineAlignment(StringAlignmentCenter);
        sfFlag.SetFormatFlags(sfFlag.GetFormatFlags() | StringFormatFlagsNoWrap);
        g.DrawString(f.name, -1, g_fModName,
                     RectF((REAL)(fr.left + 34), (REAL)fr.top,
                           (REAL)(fr.right - fr.left - 34 + 10),
                           (REAL)(fr.bottom - fr.top)),
                     &sfFlag, lblBr);
    }

    // ── Seed row (between flag grid and cmd preview) ─────────────────────
    // Checkbox toggles whether `-seed VALUE` actually gets appended to
    // the command line. Text input on the right shows / accepts the
    // numeric value; the arrow slice on the input's right edge opens
    // the seeds dropdown (presets from seeds.json + the rolling 3-slot
    // recents history). The input and arrow share one chrome (single
    // text_box.png stretched across both) but split into separate
    // hit-targets — see BodySeedInputRect / BodySeedArrowRect.
    {
        bool seedOn = g_modSettings.useSeed;
        RECT cbR = BodySeedCheckRect(B);
        DrawFlagCheckbox(g, (REAL)cbR.left, (REAL)cbR.top, seedOn, false, false);

        // "Seed" label — sits between the checkbox and the combo, same
        // font/treatment as the flag-grid labels so the seed row reads
        // as part of the same family of controls. The combo starts at
        // labelX + g_seedLabelLogicalW + 20 (see BodySeedComboRect), so
        // the gap from the label's rendered right edge to the combo's
        // left edge is a constant 20 px regardless of font family.
        //
        // The rect width is g_seedLabelLogicalW + 20 (not + 4) — a
        // generous safety pad so the text never clips even when
        // MeasureString slightly underestimates the actual rendered
        // glyph extent (some fonts include sub-pixel right-side
        // bearings that the default measurement skips). The combo
        // still sits at the originally-computed +20 gap from the
        // measured end, so the visible gap between "Seed" and the
        // input is preserved.
        SolidBrush seedLbl(Tok::TextParchment);
        StringFormat sfSeed;
        sfSeed.SetAlignment(StringAlignmentNear);
        sfSeed.SetLineAlignment(StringAlignmentCenter);
        sfSeed.SetFormatFlags(sfSeed.GetFormatFlags() | StringFormatFlagsNoWrap);
        g.DrawString(L"Seed", -1, g_fModName,
                     RectF((REAL)(cbR.right + 8), (REAL)B.loSeedY,
                           (REAL)g_seedLabelLogicalW + 30.0f, (REAL)B.loSeedH),
                     &sfSeed, &seedLbl);

        // Combined chrome — text_box.png stretched across the full
        // input+arrow rect. Drawing once (instead of separately per
        // half) keeps the bronze frame continuous and avoids a visible
        // seam between the two halves.
        RECT combo = BodySeedComboRect(B);
        int cx = combo.left, cy = combo.top;
        int cw = combo.right - combo.left, ch = combo.bottom - combo.top;
        if (Gdiplus::Bitmap* tb = AssetImage(L"text_box.png")) {
            InterpolationMode prev = g.GetInterpolationMode();
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.DrawImage(tb, cx, cy, cw, ch);
            g.SetInterpolationMode(prev);
        } else {
            SolidBrush bg(Tok::BgDeep);
            g.FillRectangle(&bg, cx, cy, cw, ch);
            Pen border(Tok::Bronze, 1.0f);
            g.DrawRectangle(&border, cx, cy, cw - 1, ch - 1);
        }

        // Input text — current seedArg, with placeholder when empty
        // and unfocused. Colour follows the checkbox state so the
        // user can see at a glance whether the value will actually be
        // used.
        RECT inR = BodySeedInputRect(B);
        const wstring& val = g_modSettings.seedArg;
        bool showPlaceholder = val.empty() && !g_seedInputFocused;
        const wchar_t* shown = showPlaceholder ? L"enter seed…" : val.c_str();
        Color textColor = showPlaceholder
                            ? Tok::GoldDim
                            : (seedOn ? Tok::Gold : Tok::GoldDim);
        SolidBrush textBr(textColor);
        StringFormat sfIn;
        sfIn.SetAlignment(StringAlignmentNear);
        sfIn.SetLineAlignment(StringAlignmentCenter);
        sfIn.SetFormatFlags(sfIn.GetFormatFlags() | StringFormatFlagsNoWrap);
        REAL inTextX = (REAL)(inR.left + 6);
        REAL inTextY = (REAL)inR.top;
        REAL inTextW = (REAL)(inR.right - inR.left - 8);
        REAL inTextH = (REAL)(inR.bottom - inR.top);
        // Selection highlight — drawn BEFORE the text so the text reads
        // on top. We measure the prefix to each end of the selection
        // and fill a rect between them. Semi-transparent gold so the
        // underlying chrome still shows through, and so it reads as an
        // overlay rather than a separate field.
        if (g_seedInputFocused && SeedHasSelection() && !val.empty()) {
            int lo = SeedSelLo(), hi = SeedSelHi();
            REAL lx = inTextX, hx = inTextX;
            if (lo > 0) {
                RectF mr;
                g.MeasureString(val.substr(0, lo).c_str(), -1, g_fBtn,
                                PointF(inTextX, inTextY), &sfIn, &mr);
                lx = inTextX + mr.Width;
            }
            if (hi > 0) {
                RectF mr;
                g.MeasureString(val.substr(0, hi).c_str(), -1, g_fBtn,
                                PointF(inTextX, inTextY), &sfIn, &mr);
                hx = inTextX + mr.Width;
            }
            // Alpha-blended gold over the bronze chrome reads as a
            // recognizable selection highlight without inventing a new
            // theme color. Inset 3 px top/bottom so the highlight
            // doesn't touch the chrome border.
            SolidBrush selBr(Color(0x80, 0xC8, 0xA8, 0x4B));
            g.FillRectangle(&selBr,
                            lx, (REAL)(inR.top + 3),
                            hx - lx, (REAL)(inR.bottom - inR.top - 6));
        }

        g.DrawString(shown, -1, g_fBtn,
                     RectF(inTextX, inTextY, inTextW, inTextH),
                     &sfIn, &textBr);

        // Caret — only when focused and the blink state is "on". Position
        // by measuring the prefix string up to the caret index; an empty
        // prefix yields width 0 so the caret sits at the left padding.
        if (g_seedInputFocused && g_seedCaretVisible) {
            int caret = g_seedCaretPos;
            if (caret < 0) caret = 0;
            if (caret > (int)val.size()) caret = (int)val.size();
            REAL prefixW = 0.0f;
            if (caret > 0) {
                wstring prefix = val.substr(0, caret);
                RectF mr;
                g.MeasureString(prefix.c_str(), -1, g_fBtn,
                                PointF(inTextX, inTextY), &sfIn, &mr);
                prefixW = mr.Width;
            }
            // 1-px-thick vertical line, inset 4 px from top/bottom of
            // the input rect so it doesn't touch the chrome border.
            Pen caretPen(Tok::Gold, 1.5f);
            REAL caretX = inTextX + prefixW;
            g.DrawLine(&caretPen,
                       caretX, (REAL)(inR.top + 4),
                       caretX, (REAL)(inR.bottom - 4));
        }

        // Arrow button — separate visual slice on the right edge.
        // Vertical hairline divider distinguishes it from the input.
        RECT arR = BodySeedArrowRect(B);
        Pen div(Tok::BronzeDim, 1.0f);
        g.DrawLine(&div, (INT)arR.left, (INT)(arR.top + 4),
                         (INT)arR.left, (INT)(arR.bottom - 4));
        StringFormat sfA;
        sfA.SetAlignment(StringAlignmentCenter);
        sfA.SetLineAlignment(StringAlignmentCenter);
        SolidBrush arrowBr(Tok::Gold);
        g.DrawString(L"\u25BE", -1, g_fBtn,
                     RectF((REAL)arR.left, (REAL)arR.top,
                           (REAL)(arR.right - arR.left),
                           (REAL)(arR.bottom - arR.top)),
                     &sfA, &arrowBr);
    }

    // ── Cmd preview text bar ──────────────────────────────────────────────
    // Dynamic height: 1 / 2 / 3 wrapped lines all fit. To choose the
    // box height we wrap the args FIRST (need a Graphics for measurement,
    // which is why this lives at paint time rather than in ComputeBodyLayout),
    // count newlines, and pick a height row-per-row. Box shrinks when the
    // user disables flags and the wrapped form fits on fewer lines.
    wstring cmd = BuildLaunchArgs();
    if (cmd.empty()) cmd = L"(no mod selected)";
    StringFormat sfPrev;
    sfPrev.SetAlignment(StringAlignmentNear);
    sfPrev.SetLineAlignment(StringAlignmentNear);   // top-anchored when wrapped
    int px = B.loX + 12, py = B.loCmdPreviewY;
    int pw = B.loW - 24;
    REAL maxW = (REAL)(pw - 16);
    wstring wrapped = BreakLaunchArgsAtDash(g, cmd, g_fCmdArgs, &sfPrev, maxW);

    int lineCount = 1;
    for (wchar_t c : wrapped) if (c == L'\n') ++lineCount;
    if (lineCount < 1) lineCount = 1;
    if (lineCount > 3) lineCount = 3;
    // Logical box height per line count: 1 → 28, 2 → 44, 3 → 60.
    // Matches the previous static 44 at the 2-line case (the common one).
    int ph = 12 + lineCount * 16;

    if (Gdiplus::Bitmap* tb = AssetImage(L"text_box.png")) {
        InterpolationMode prev = g.GetInterpolationMode();
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(tb, px, py, pw, ph);
        g.SetInterpolationMode(prev);
    } else {
        SolidBrush previewBg(Tok::BgDeep);
        g.FillRectangle(&previewBg, px, py, pw, ph);
        Pen previewBorder(Tok::Bronze, 1.0f);
        g.DrawRectangle(&previewBorder, px, py, pw - 1, ph - 1);
    }

    SolidBrush previewText(Tok::GoldDim);
    g.DrawString(wrapped.c_str(), -1, g_fCmdArgs,
                 RectF((REAL)(px + 8), (REAL)(py + 4),
                       (REAL)(pw - 16), (REAL)(ph - 8)),
                 &sfPrev, &previewText);
}

static void PaintBottomPanel(Graphics& g, int W, int H) {
    using namespace LO;
    // bodyH matches Layout() — always the frame's native height. The
    // expansion panel sits below this line, not by shrinking the body.
    int frameNativeH = 1024;
    if (Gdiplus::Bitmap* fm = AssetImage(L"frame_main.png"))
        frameNativeH = (int)fm->GetHeight();
    int bodyH = frameNativeH;

    // The frame_main / frame_expand assets provide all the bottom chrome.
    // Nothing to draw if the panel is collapsed.
    if (!g_bottomExpanded) return;

    // Inset content by frame_expand.png's measured filigree (mirrors the
    // child-button positioning in Layout()).
    FrameInset fe = MeasureFrameInset(L"frame_expand.png");
    int feL = (fe.left  > 0 ? fe.left  : 16) + 8;
    int feR = (fe.right > 0 ? fe.right : 16) + 8;
    int feT = (fe.top   > 0 ? fe.top   : 22) + 4;

    int panelTop = bodyH + feT;
    int colGap   = EXP_COL_GAP;          // internal Tools-column gap (no divider)
    int secGap   = EXP_SEC_GAP;          // section gap (holds a divider)

    // Match Layout()'s section X positions. Sections 1 and 3 are EXP_BTN_W
    // wide (single column); section 2 (Tools) is two columns plus the
    // internal gap. Section gaps (secGap) are wider than the internal gap so
    // the dividers clear the button HWNDs.
    int sec1W = EXP_BTN_W;
    int sec3W = EXP_BTN_W;
    int sec2W = EXP_BTN_W * 2 + colGap;
    int framedW   = W - feL - feR;
    int totalBtnW = sec1W + sec2W + sec3W;
    int slack     = framedW - totalBtnW - secGap * 2;
    int leftPad   = slack / 2;
    int sec1X = feL + leftPad;
    int sec2X = sec1X + sec1W + secGap;
    int sec3X = sec2X + sec2W + secGap;

    // Divider centers, and column-centered X for the single-column sections
    // (mirror Layout()). References/Downloads are centered between the panel
    // edge and the nearest divider rather than anchored at sec1X/sec3X.
    int div1X = sec1X + sec1W + secGap / 2;
    int div2X = sec2X + sec2W + secGap / 2;
    int refBtnX = (feL + div1X - EXP_BTN_W) / 2;
    int dlBtnX  = (div2X + (W - feR) - EXP_BTN_W) / 2;

    StringFormat sfC;
    sfC.SetAlignment(StringAlignmentCenter);
    sfC.SetLineAlignment(StringAlignmentCenter);
    SolidBrush gold(Tok::Gold);

    // Smaller title font (g_fExpHdr, 18px) in the tightened header band.
    auto hdr = [&](const wchar_t* text, int x, int w) {
        g.DrawString(text, -1, g_fExpHdr,
                     RectF((REAL)x, (REAL)panelTop, (REAL)w, (REAL)EXP_HDR_H),
                     &sfC, &gold);
    };
    // References/Downloads titles centered over their (column-centered)
    // buttons; Tools stays centered over its two-column block.
    hdr(L"REFERENCES",         refBtnX, EXP_BTN_W);
    hdr(L"TOOLS AND PROGRAMS", sec2X,   sec2W);
    hdr(L"DOWNLOADS",          dlBtnX,  EXP_BTN_W);

    // ── Vertical dividers flanking the Tools column ──────────────────────
    // A mirrored pair centered in the two inter-section gaps, running the
    // full section height. Asset-driven; a thin bronze rule fades in as a
    // fallback if a PNG is missing.
    const int divTop = panelTop + LO::EXP_DIVIDER_TOP_PAD;
    auto divider = [&](const wchar_t* name, int gapCenter) {
        if (Gdiplus::Bitmap* dv = AssetImage(name)) {
            int dw = (int)dv->GetWidth();
            DrawAssetAt(g, dv, gapCenter - dw / 2, divTop);
        } else {
            // Fallback: a 2px bronze rule that fades at both ends so it
            // doesn't read as a hard bar.
            int h = LO::EXP_DIVIDER_FALLBACK_H;
            REAL x = (REAL)gapCenter - 1.0f;
            LinearGradientBrush rule(
                RectF(x, (REAL)divTop, 2.0f, (REAL)h),
                GPA(0,   0x8B, 0x5A, 0x2B),
                GPA(200, 0x8B, 0x5A, 0x2B),
                LinearGradientModeVertical);
            // Symmetric fade: transparent at top & bottom, solid mid.
            REAL pos[3]   = { 0.0f, 0.5f, 1.0f };
            Color cols[3] = { GPA(0, 0x8B,0x5A,0x2B),
                              GPA(210,0x8B,0x5A,0x2B),
                              GPA(0, 0x8B,0x5A,0x2B) };
            rule.SetInterpolationColors(cols, pos, 3);
            g.FillRectangle(&rule, RectF(x, (REAL)divTop, 2.0f, (REAL)h));
        }
    };
    divider(L"left_expand_divider.png",  div1X + LO::EXP_DIVIDER_X_NUDGE);
    divider(L"right_expand_divider.png", div2X + LO::EXP_DIVIDER_X_NUDGE);
}

static void PaintToolbarControl(Graphics& g, const RECT& r,
                                const wchar_t* label, int labelW,
                                const wchar_t* valueText,
                                const wchar_t* valueFontFamily,
                                INT valueFontStyle,
                                const COLORREF* swatch,
                                bool showChevron,
                                bool cycleHint = false) {
    SolidBrush textBr(Tok::TextParchment);
    SolidBrush valBr(Tok::Gold);

    int rowH = r.bottom - r.top;

    // Label — right-aligned, NoWrap. Right-alignment puts the text flush
    // against the value box on its right with any unused label rect
    // sitting on the LEFT (which abuts the previous control or column
    // padding). Was previously left-aligned, which created a visible
    // gap between the title text and the textbox at every scale.
    //
    // Skip the label draw entirely when caller passes null/empty —
    // value-only callers (e.g. the On Launch textbox, whose label
    // "ON LAUNCH" is rendered as a separate header above the row) use
    // labelW=0 and don't want any text on the left.
    StringFormat sfL;
    sfL.SetAlignment(StringAlignmentFar);
    sfL.SetLineAlignment(StringAlignmentCenter);
    sfL.SetFormatFlags(sfL.GetFormatFlags() | StringFormatFlagsNoWrap);
    if (label && *label && labelW > 0) {
        g.DrawString(label, -1, g_fBtn,
                     RectF((REAL)r.left, (REAL)r.top,
                           (REAL)labelW, (REAL)rowH),
                     &sfL, &textBr);
    }

    // Value box: from r.left + labelW + 1 to r.right
    int bx = r.left + labelW + 1;
    int by = r.top + 2;
    int bw = r.right - bx;
    int bh = rowH - 4;
    if (bw <= 0) return;     // degenerate (caller picked too-narrow rect)
    if (Gdiplus::Bitmap* tb = AssetImage(L"text_box.png")) {
        InterpolationMode prev = g.GetInterpolationMode();
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(tb, bx, by, bw, bh);
        g.SetInterpolationMode(prev);
    } else {
        OPDrawBtnFrame(g, bx, by, bw, bh, false);
    }

    int chevronW = (showChevron || cycleHint) ? 22 : 0;
    int contentL = bx + 6;
    int contentR = bx + bw - chevronW - 4;

    if (swatch) {
        // Small square swatch centered in the content area.
        int swH = bh - 8;
        int swW = swH;       // square
        int swX = contentL + ((contentR - contentL) - swW) / 2;
        if (swX < contentL) swX = contentL;
        int swY = by + (bh - swH) / 2;
        SolidBrush sw(Color(GetRValue(*swatch),
                            GetGValue(*swatch),
                            GetBValue(*swatch)));
        g.FillRectangle(&sw, swX, swY, swW, swH);
        Pen swBorder(Tok::BronzeDim, 1.0f);
        g.DrawRectangle(&swBorder, swX, swY, swW, swH);
    } else if (valueText) {
        StringFormat sfC;
        sfC.SetAlignment(StringAlignmentCenter);
        sfC.SetLineAlignment(StringAlignmentCenter);
        sfC.SetFormatFlags(sfC.GetFormatFlags() | StringFormatFlagsNoWrap);

        // When a font family is specified, render the value in that
        // face (Font dropdown's "STYLE" preview). Falls back to g_fBtn
        // when the family lookup fails so the cell never goes blank.
        bool drewWithFamily = false;
        if (valueFontFamily && *valueFontFamily) {
            FontFamily fam(valueFontFamily);
            if (fam.GetLastStatus() == Ok) {
                REAL fontPx = (REAL)max(10, (int)(bh * 0.65f));
                Font tempFont(&fam, fontPx, valueFontStyle, UnitPixel);
                g.DrawString(valueText, -1, &tempFont,
                             RectF((REAL)contentL, (REAL)by,
                                   (REAL)(contentR - contentL), (REAL)bh),
                             &sfC, &valBr);
                drewWithFamily = true;
            }
        }
        if (!drewWithFamily) {
            g.DrawString(valueText, -1, g_fBtn,
                         RectF((REAL)contentL, (REAL)by,
                               (REAL)(contentR - contentL), (REAL)bh),
                         &sfC, &valBr);
        }
    }

    if (showChevron) {
        if (Gdiplus::Bitmap* ch = AssetImage(L"dropdown_chevron.png")) {
            int chW = (int)ch->GetWidth();
            int chH = (int)ch->GetHeight();
            int targetH = bh - 4;
            int targetW = chW * targetH / chH;
            int cx = bx + bw - targetW - 4;
            int cy = by + (bh - targetH) / 2;
            InterpolationMode prev = g.GetInterpolationMode();
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.DrawImage(ch, cx, cy, targetW, targetH);
            g.SetInterpolationMode(prev);
        } else {
            StringFormat sfR;
            sfR.SetAlignment(StringAlignmentCenter);
            sfR.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(L"\u25BE", -1, g_fBtn,
                         RectF((REAL)(bx + bw - 16), (REAL)by, 16.0f, (REAL)bh),
                         &sfR, &valBr);
        }
    }
    if (cycleHint) {
        // Circular arrow (U+21BB) — the visual affordance for a cycling
        // button. Drawn at the same right-edge slot the chevron uses on
        // dropdown controls, so the spacing matches. GDI+ font linking
        // substitutes a system font if Exocet doesn't ship the glyph.
        StringFormat sfR;
        sfR.SetAlignment(StringAlignmentCenter);
        sfR.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"\u21BB", -1, g_fBtn,
                     RectF((REAL)(bx + bw - 22), (REAL)by, 20.0f, (REAL)bh),
                     &sfR, &valBr);
    }
}

void PaintBody(HDC hdc, int W, int H) {
    // W and H arrive in LOGICAL pixels (the caller converts via U() before
    // passing them in). The Graphics gets a ScaleTransform that maps every
    // logical coordinate to its physical pixel position, so the entire
    // paint body below can be written exactly as it was when g_scale was
    // implicitly 1.0 — frame_main, the stone slices, frame_panel_left/right,
    // the gem ornament, mod-list painting, the launch-options panel, the
    // logo, the title-bar buttons, everything inherits the scale.
    Graphics g(hdc);
    g.ScaleTransform((REAL)g_scale, (REAL)g_scale);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // ── Layered backdrop ────────────────────────────────────────────────
    //   1. Stone texture covers the full window (collapsed-state region).
    //      The expanded bottom area below WIN_H is intentionally left
    //      unframed for now — a separate frame_expand.png asset will land
    //      in a later batch.
    //   2. Frame overlay (ornate filigree, gem ornaments) on top.
    //   3. D2RLOADER logo at native size in the left rail.
    //   4. frame_panel_right.png (the right column's framed three-panel
    //      asset) drawn at native size, positioned by ComputeBodyLayout.
    // Any of these may be nullptr (asset missing); the helpers no-op then.
    if (Gdiplus::Bitmap* bgStone = AssetImage(L"bg_stone.png")) {
        // Stone covers the COLLAPSED window region (top 1024px). When the
        // bottom panel is expanded, that area is drawn separately by
        // bg_expand.png below — so we cap at the frame's native height.
        UINT sw = bgStone->GetWidth();
        UINT sh = bgStone->GetHeight();
        int collapsedH = 1024;
        if (Gdiplus::Bitmap* fm = AssetImage(L"frame_main.png"))
            collapsedH = (int)fm->GetHeight();
        int srcW = min((int)sw, W);
        int srcH = min((int)sh, collapsedH);
        g.DrawImage(bgStone,
                    Rect(0, 0, srcW, srcH),
                    0, 0, (REAL)srcW, (REAL)srcH,
                    UnitPixel);
    }
    // Bottom-expansion stone (only when the panel is open). Drawn at
    // native size, 1:1, positioned directly below the main frame.
    if (g_bottomExpanded) {
        if (Gdiplus::Bitmap* bgExp = AssetImage(L"bg_expand.png")) {
            int collapsedH = 1024;
            if (Gdiplus::Bitmap* fm = AssetImage(L"frame_main.png"))
                collapsedH = (int)fm->GetHeight();
            int sw = (int)bgExp->GetWidth();
            int sh = (int)bgExp->GetHeight();
            g.DrawImage(bgExp,
                        Rect(0, collapsedH, min(sw, W), sh),
                        0, 0, (REAL)min(sw, W), (REAL)sh,
                        UnitPixel);
        }
    }

    // ── Per-region stone slices ─────────────────────────────────────────
    // Each of the 7 UI regions (left rail, title band, mod list, mod
    // description, mod-list footer, launch options, play) gets its own
    // chunk of bg_stone.png sampled from a DIFFERENT non-(0,0) source
    // offset. Visually each section reads as its own piece of texture
    // instead of a single uniform background — matching the original
    // mockup's "scattered stone" feel. Drawn over the base bg_stone fill
    // (which still serves as the fallback under buttons and the seam
    // between regions) but BEFORE frame_main and the panel chrome, so the
    // bronze borders mask any overflow at the region edges.
    {
        BodyLayout B  = ComputeBodyLayout(W, H);
        LeftPanelGeom lg = ComputeLeftPanelGeom(B);
        auto stoneSlice = [&](int dx, int dy, int dw, int dh,
                              int sx, int sy) {
            if (dw <= 0 || dh <= 0) return;
            Gdiplus::Bitmap* bg = AssetImage(L"bg_stone.png");
            if (!bg) return;
            int sw = (int)bg->GetWidth(), sh = (int)bg->GetHeight();
            int srcW = min(dw, sw - sx);
            int srcH = min(dh, sh - sy);
            if (srcW <= 0 || srcH <= 0) return;
            g.DrawImage(bg,
                        Rect(dx, dy, srcW, srcH),
                        sx, sy, (REAL)srcW, (REAL)srcH,
                        UnitPixel);
        };
        // The seven offsets are chosen by hand to be distinct, not (0,0),
        // and to keep src+dim within bg_stone's 1536×1024 footprint.
        constexpr int LIST_BAND_H = 60;   // small upper band inside frame_panel_left for MODS+Refresh
        // 1. Left rail / left column interior
        stoneSlice(0, 0, lg.x, 986,                                  80,  30);
        // 2. Title band — inside frame_panel_left, just above the list
        stoneSlice(lg.x, lg.mainTop,  lg.w, LIST_BAND_H,             600, 800);
        // 3. Mod list interior — inside frame_panel_left main region
        stoneSlice(lg.x, lg.mainTop + LIST_BAND_H,
                   lg.w, lg.dividerY - (lg.mainTop + LIST_BAND_H),   300, 200);
        // 4. MOD DESCRIPTION interior (frame_panel_right top region)
        stoneSlice(B.descX, B.descY, B.descW, B.descH,               900, 100);
        // 5. Mod-list footer — inside frame_panel_left below the divider
        stoneSlice(lg.x, lg.dividerY, lg.w, lg.bottom - lg.dividerY, 800, 700);
        // 6. LAUNCH OPTIONS interior (frame_panel_right mid region)
        stoneSlice(B.loX, B.loY, B.loW, B.loH,                       200, 500);
        // 7. PLAY area — bottom region of frame_panel_right, from the
        //    bottom of LAUNCH OPTIONS down to the panel bottom.
        stoneSlice(B.rightX, B.loY + B.loH, B.rightW,
                   (B.rightY + B.rightH) - (B.loY + B.loH),          700, 850);
    }
    if (Gdiplus::Bitmap* frame = AssetImage(L"frame_main.png")) {
        // Draw at native size, top-left aligned. No stretch.
        int frameH = (int)frame->GetHeight();   // native 1024
        DrawAssetStretched(g, frame, 0, 0, W, frameH);
    }
    // Filigree corner accents on the four frame corners. (Asset-or-fallback.)
    PaintCornerAccents(g, W);
    // Bottom-expansion frame (only when panel is open). Native size, 1:1,
    // positioned immediately below the main frame.
    if (g_bottomExpanded) {
        if (Gdiplus::Bitmap* frExp = AssetImage(L"frame_expand.png")) {
            int collapsedH = 1024;
            if (Gdiplus::Bitmap* fm = AssetImage(L"frame_main.png"))
                collapsedH = (int)fm->GetHeight();
            DrawAssetAt(g, frExp, 0, collapsedH);
        }
    }
    if (Gdiplus::Bitmap* logo = AssetImage(L"logo_d2rloader.png")) {
        // The logo's visual weight is left-biased (the flame extends to the
        // image's left edge while the right side has white space after the
        // "™"). Naive center-the-bounding-box positioning makes it visually
        // crowded against the left filigree. A small rightward nudge brings
        // the visual center back into the middle of the rail.
        FrameInset fi2 = MeasureFrameInset(L"frame_main.png");
        constexpr int LOGO_VISUAL_OFFSET = 18;     // was 24; nudged 6 px left
        int lw = (int)logo->GetWidth();
        int lh = (int)logo->GetHeight();
        int lx = (fi2.left + 12) + (LO::LEFT_RAIL_W - lw) / 2 + LOGO_VISUAL_OFFSET;
        int ly = fi2.top + 16;
        DrawAssetAt(g, logo, lx, ly);

        // Version label, ~15 px below the bottom of the logo image.
        // Centered within the logo's horizontal span so it visually
        // hangs off the "Mod Launcher" subtitle baked into the logo.
        // Clickable ONLY when an update has been detected — see the
        // WM_LBUTTONDOWN / WM_SETCURSOR hit-tests, which both gate on
        // g_launcherUpdateAvailable so the label is inert (no hand
        // cursor, no click action) when nothing new is waiting on
        // GitHub. When an update IS pending, the label glows gold and
        // a click re-runs the check bypassing any Skip Version state.
        //
        // Font: g_fColHdrSm (16 pt) — ~25% larger than g_fBtn (13 pt),
        // built from the same display family + style so weight and
        // family selection track the user's UI font choice.
        Font* verFont = g_fColHdrSm ? g_fColHdrSm : g_fBtn;
        if (verFont) {
            wstring verText = wstring(L"v") + LAUNCHER_VERSION;
            int verW = lw;
            int verH = 24;
            // Defaults: centered under the logo at lx, just above its
            // bottom edge (the original computed positions). Overridable
            // via user_layout.json's version_label.x / version_label.y.
            int verX = LayoutVersionLabelX(lx);
            int verY = LayoutVersionLabelY(ly + lh - 10);

            // Remember the hit rect (logical coords) for click +
            // cursor handling. Stored every paint so it tracks any
            // layout shifts (DPI change, window resize, etc.).
            g_versionLabelRect.left   = verX;
            g_versionLabelRect.top    = verY;
            g_versionLabelRect.right  = verX + verW;
            g_versionLabelRect.bottom = verY + verH;

            StringFormat verFmt;
            verFmt.SetAlignment(StringAlignmentCenter);
            verFmt.SetLineAlignment(StringAlignmentNear);
            verFmt.SetFormatFlags(verFmt.GetFormatFlags()
                                  | StringFormatFlagsNoWrap);

            // Glow halo when an update is available — draw the text
            // 8 times at single-pixel offsets in semi-transparent
            // gold BEFORE the solid fill on top. The aliased seams
            // between offsets blend into a soft glow that hugs the
            // letterforms.
            if (g_launcherUpdateAvailable) {
                SolidBrush haloBr(Color(110, 255, 215, 90));
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        if (dx == 0 && dy == 0) continue;
                        RectF haloRect((REAL)(verX + dx),
                                       (REAL)(verY + dy),
                                       (REAL)verW, (REAL)verH);
                        g.DrawString(verText.c_str(), -1, verFont,
                                     haloRect, &verFmt, &haloBr);
                    }
                }
            }

            // Solid text fill — gold when an update is pending
            // (matches the halo color), parchment otherwise so the
            // label reads as ambient info rather than a call to
            // action.
            SolidBrush verFill(g_launcherUpdateAvailable
                               ? Tok::Gold
                               : Tok::TextParchment);
            RectF verRect((REAL)verX, (REAL)verY,
                          (REAL)verW, (REAL)verH);
            g.DrawString(verText.c_str(), -1, verFont,
                         verRect, &verFmt, &verFill);
        }
    }
    // Panel asset draws at native size, positioned per ComputeBodyLayout.
    // Drawn before the panel content so the bronze chrome sits behind text.
    if (Gdiplus::Bitmap* panel = AssetImage(L"frame_panel_right.png")) {
        BodyLayout B = ComputeBodyLayout(W, H);
        DrawAssetAt(g, panel, B.rightX, B.rightY);
    }
    // Center mod-list panel frame: drawn at NATIVE 1:1 (660×955), anchored at
    // (centerX, bodyTop + PANEL_LEFT_Y_NUDGE) so its top sits at the inner
    // edge of the frame_main top filigree.
    if (Gdiplus::Bitmap* lpanel = AssetImage(L"frame_panel_left.png")) {
        BodyLayout Bl = ComputeBodyLayout(W, H);
        LeftPanelGeom lg = ComputeLeftPanelGeom(Bl);
        DrawAssetAt(g, lpanel, lg.x, lg.y);
    }
    // Top centerpiece gem ornament — drawn AFTER frame_panel_left so the gem
    // layers on top where the panel's top extends up into the filigree band.
    PaintTopOrnament(g, W);

    PaintLeftRail(g, W, H);

    BodyLayout B = ComputeBodyLayout(W, H);
    PaintModDescription(g, B);
    PaintLaunchOptions(g, B);

    // Center column "MODS" header — sits under frame_panel_left's top
    // divider, in the same band as Refresh and the list. Inset from the
    // frame's left border by PANEL_LEFT_PAD. 24px g_fColHdrMed.
    using namespace LO;
    LeftPanelGeom lg = ComputeLeftPanelGeom(B);
    SolidBrush gold(Tok::Gold);
    StringFormat sfL;
    sfL.SetAlignment(StringAlignmentNear);
    sfL.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"MODS", -1, g_fColHdrMed,
                 RectF((REAL)(lg.x + PANEL_LEFT_PAD), (REAL)(lg.mainTop + 4),
                       (REAL)(lg.w - PANEL_LEFT_PAD * 2), 44.0f),
                 &sfL, &gold);

    // ── Center-column toolbar (Scale / Font / Colour) ───────────────────
    // Three controls sitting in the strip above the expand arrow. The
    // Scale control is a cycling button (clicking advances through 3
    // presets); Font shows "STYLE" rendered in the currently selected
    // face; Colour shows a small swatch chip. Per-control widths come
    // from TBL:: at file scope.
    {
        // Scale cycler — current preset's label (e.g. "85%"). No chevron.
        wchar_t scaleBuf[16] = L"--";
        for (auto& p : g_scalePresets) {
            if (p.mul == g_cfg.uiScale) {
                lstrcpynW(scaleBuf, p.label, 16);
                break;
            }
        }
        PaintToolbarControl(g, g_scaleDropdownRect,
                            L"Scale", TBL::SCALE_LABEL_W,
                            scaleBuf, nullptr, FontStyleRegular, nullptr,
                            /*showChevron*/ false,
                            /*cycleHint  */ false);

        // 3-state toggle slider beneath the Scale row. The asset family
        // is btn_toggle1.png / btn_toggle2.png / btn_toggle3.png — one
        // per state, picking which the slider knob appears at. Native
        // size is 411×182; we render at 53×23 logical (~12% — a small,
        // unobtrusive control). If the asset for the current state is
        // missing (user is still authoring the variants), fall back to
        // a programmatic track + marker so the control is still
        // clickable even when its art is incomplete.
        {
            int state = ScaleToggleState();
            const wchar_t* assetName =
                (state == 0) ? L"btn_toggle1.png" :
                (state == 1) ? L"btn_toggle2.png" :
                               L"btn_toggle3.png";
            Gdiplus::Bitmap* asset = AssetImage(assetName);
            const RECT& sr = g_scaleSliderRect;
            int sw = sr.right - sr.left;
            int sh = sr.bottom - sr.top;
            if (asset) {
                InterpolationMode prev = g.GetInterpolationMode();
                g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                g.DrawImage(asset, sr.left, sr.top, sw, sh);
                g.SetInterpolationMode(prev);
            } else {
                // Fallback: thin gold track with a circular marker at
                // the active position. Keeps the control discoverable
                // while btn_toggle2/3.png are still in production.
                int trackY = sr.top + sh / 2 - 1;
                Pen track(Tok::BronzeBright, 2.0f);
                g.DrawLine(&track,
                           (INT)(sr.left + 4),  trackY,
                           (INT)(sr.right - 4), trackY);
                int markerW = sh - 4;
                int slot0X = sr.left + 4;
                int slot2X = sr.right - 4 - markerW;
                int markerX = slot0X + ((slot2X - slot0X) * state) / 2;
                SolidBrush markerFill(Tok::GoldBright);
                g.FillEllipse(&markerFill, markerX, sr.top + 2, markerW, markerW);
                Pen markerBorder(Tok::Bronze, 1.0f);
                g.DrawEllipse(&markerBorder, markerX, sr.top + 2, markerW, markerW);
            }
        }

        // Font dropdown — show the abbreviated name of the currently
        // selected face (e.g. "Cin-Bol"). Rendering each face in its
        // own typography was scrapped: Cinzel-Regular and Cinzel-Bold
        // both report family "Cinzel" via TTF metadata, and even with
        // style-from-filename derivation the rendered weights came out
        // identical because the OS lookup didn't separate the two
        // variants under FR_PRIVATE. The abbreviated label is
        // unambiguous and fits in the value box.
        const wchar_t* fontAbbrev = nullptr;
        if (!g_cfg.fontName.empty()) {
            for (size_t i = 0; i < g_availableFonts.size(); ++i) {
                if (g_availableFonts[i] == g_cfg.fontName) {
                    if (i < g_availableAbbrevs.size()
                        && !g_availableAbbrevs[i].empty()) {
                        fontAbbrev = g_availableAbbrevs[i].c_str();
                    }
                    break;
                }
            }
        }
        PaintToolbarControl(g, g_fontDropdownRect,
                            L"Font", TBL::FONT_LABEL_W,
                            fontAbbrev,
                            nullptr,                  // no per-face render
                            FontStyleRegular,
                            nullptr,
                            /*showChevron*/ true);

        // Colour dropdown — current preset as a small square chip.
        if (g_cfg.fontColorIdx >= 0 &&
            g_cfg.fontColorIdx < (int)(sizeof(g_colorPresets)/sizeof(g_colorPresets[0]))) {
            const ColorPreset& cp = g_colorPresets[g_cfg.fontColorIdx];
            PaintToolbarControl(g, g_colorDropdownRect,
                                L"Colour", TBL::COLOR_LABEL_W,
                                nullptr, nullptr, FontStyleRegular, &cp.rgb,
                                /*showChevron*/ true);
        } else {
            // No selection yet — show the default Gold chip.
            COLORREF defGold = RGB(0xE8, 0xC2, 0x5E);
            PaintToolbarControl(g, g_colorDropdownRect,
                                L"Colour", TBL::COLOR_LABEL_W,
                                nullptr, nullptr, FontStyleRegular, &defGold,
                                /*showChevron*/ true);
        }

        // ── On Launch column ────────────────────────────────────────────
        // Header: just "ON LAUNCH" rendered as plain text (no chrome).
        // Same right-aligned style as the other toolbar labels for
        // consistency, except aligned LEFT here so it reads as a
        // column heading sitting above its textbox.
        {
            StringFormat sfHdr;
            sfHdr.SetAlignment(StringAlignmentNear);
            sfHdr.SetLineAlignment(StringAlignmentCenter);
            sfHdr.SetFormatFlags(sfHdr.GetFormatFlags() | StringFormatFlagsNoWrap);
            SolidBrush hdrBr(Tok::TextParchment);
            g.DrawString(L"On Launch", -1, g_fBtn,
                         RectF((REAL)g_onLaunchHeaderRect.left,
                               (REAL)g_onLaunchHeaderRect.top,
                               (REAL)(g_onLaunchHeaderRect.right
                                      - g_onLaunchHeaderRect.left),
                               (REAL)(g_onLaunchHeaderRect.bottom
                                      - g_onLaunchHeaderRect.top)),
                         &sfHdr, &hdrBr);
        }

        // Value-only textbox — no inline label (the "ON LAUNCH" header
        // above is the label). labelW=0 makes PaintToolbarControl skip
        // its label tier and use the full rect width for the value box.
        PaintToolbarControl(g, g_onLaunchRect,
                            L"", 0,
                            OnLaunchStateLabel(),
                            nullptr, FontStyleRegular, nullptr,
                            /*showChevron*/ false,
                            /*cycleHint  */ false);

        // On Launch slider — same asset family (btn_toggle1/2/3.png),
        // rendered at the same 53×23 logical size. Falls back to a
        // programmatic track + marker when the per-state asset is
        // missing (matches what the Scale slider does for the same
        // reason — keeps the control discoverable while assets are
        // still being authored).
        {
            int olState = OnLaunchSliderState();
            const wchar_t* olAssetName =
                (olState == 0) ? L"btn_toggle1.png" :
                (olState == 1) ? L"btn_toggle2.png" :
                                 L"btn_toggle3.png";
            Gdiplus::Bitmap* olAsset = AssetImage(olAssetName);
            const RECT& osr = g_onLaunchSliderRect;
            int osw = osr.right - osr.left;
            int osh = osr.bottom - osr.top;
            if (olAsset) {
                InterpolationMode prev = g.GetInterpolationMode();
                g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                g.DrawImage(olAsset, osr.left, osr.top, osw, osh);
                g.SetInterpolationMode(prev);
            } else {
                int trackY = osr.top + osh / 2 - 1;
                Pen track(Tok::BronzeBright, 2.0f);
                g.DrawLine(&track,
                           (INT)(osr.left + 4),  trackY,
                           (INT)(osr.right - 4), trackY);
                int markerW = osh - 4;
                int slot0X = osr.left + 4;
                int slot2X = osr.right - 4 - markerW;
                int markerX = slot0X + ((slot2X - slot0X) * olState) / 2;
                SolidBrush markerFill(Tok::GoldBright);
                g.FillEllipse(&markerFill, markerX, osr.top + 2, markerW, markerW);
                Pen markerBorder(Tok::Bronze, 1.0f);
                g.DrawEllipse(&markerBorder, markerX, osr.top + 2, markerW, markerW);
            }
        }
    }

    PaintBottomPanel(g, W, H);

    // ── Custom title-bar buttons (minimize + close) ─────────────────────
    // Drawn LAST so they paint over any panel fills that touch the top
    // filigree band. The MOD DESCRIPTION panel begins at insetT=38, which
    // overlaps the bottom of the buttons — without this final pass the
    // panel's solid fill would cover them. Single asset per button; the
    // hover-grows / click-shrinks effect is produced by a GDI+ transform
    // applied at paint time (matching the regular buttons).
    {
        const wchar_t* names[2] = {
            L"btn_minimize.png",
            L"btn_close.png",
        };
        for (int i = 0; i < 2; ++i) {
            Gdiplus::Bitmap* bm = AssetImage(names[i]);
            if (!bm) continue;
            int rightOfClose = W - TB_BTN_INSET_R;
            int closeX = rightOfClose - TB_BTN_W;
            int minX   = closeX - TB_BTN_GAP - TB_BTN_W;
            int x = (i == 0) ? minX : closeX;
            int y = TB_BTN_INSET_T;

            bool isPressed = (g_tbPressed == i);
            // Hover no longer scales — only the click-shrink applies. The
            // hover state itself is still tracked elsewhere (cursor, hit-
            // test) but produces no visible size change here.
            if (isPressed) {
                Gdiplus::Matrix prev;
                g.GetTransform(&prev);
                REAL cx = (REAL)(x + TB_BTN_W * 0.5f);
                REAL cy = (REAL)(y + TB_BTN_H * 0.5f);
                g.TranslateTransform(cx + 1.0f, cy + 1.0f);
                g.ScaleTransform(0.90f, 0.90f);
                g.TranslateTransform(-cx, -cy);
                DrawAssetAt(g, bm, x, y);
                g.SetTransform(&prev);
            } else {
                DrawAssetAt(g, bm, x, y);
            }
        }
    }
}

