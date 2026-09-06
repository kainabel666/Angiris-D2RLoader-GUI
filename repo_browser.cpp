// ═══════════════════════════════════════════════════════════════════════
//  repo_browser.cpp — repository browser modal (v1.7)
//  See repo_browser.h for the flow.
// ═══════════════════════════════════════════════════════════════════════

#include "repo_browser.h"
#include "repository.h"    // FetchRepoManifest, RepoEntry, InstallRepoEntry, REPO_MANIFEST_URL
#include "core.h"          // g_hInst, g_scale
#include "config.h"        // g_cfg
#include "mod_scan.h"      // g_mods, ModInfo
#include "colors.h"        // Tok::Gold
#include "fonts.h"         // g_fModName, g_fNavSm, g_userFontFamilyOverride
#include "assets.h"        // AssetImage, DrawButton9Slice
#include "buttons.h"       // MkStdBtn, PaintOwnerDrawButton, ButtonKind
#include "scaling.h"       // S()

#include <windows.h>
#include <windowsx.h>
#include <vector>

using namespace Gdiplus;
using std::wstring;
using std::vector;

namespace {

// Two-column framed layout (mockup Repo_Layout.png):
//   left  = titled, framed catalog list (each row its own bordered slot)
//   right = "INSTALL TO" dropdown + titled, framed detail panel
// Sizes are logical; scaled by g_scale at layout time.
constexpr int RB_W          = 900;
constexpr int RB_H          = 680;
constexpr int RB_PAD        = 22;
constexpr int RB_TITLE_TOP  = 12;   // title baseline row
constexpr int RB_TITLE_H    = 30;
constexpr int RB_COL_GAP    = 24;   // gap between left + right columns
constexpr int RB_PANEL_TOP  = 84;   // both framed panels start here (left)
constexpr int RB_RPANEL_TOP = 112;  // right panel starts lower (below dropdown)
constexpr int RB_ROW_H      = 62;    // list row height (framed slot)
constexpr int RB_ROW_GAP    = 8;     // gap between framed rows
constexpr int RB_BTN_H      = 50;
constexpr int RB_BTN_BOT    = 18;
constexpr int RB_FRAME_INSET= 16;    // 9-slice corner inset for panel borders
constexpr int RB_MENU_ITEM_H= 30;    // owner-drawn mod-dropdown menu row height
constexpr int RB_MENU_GUTTER= 26;    // left gutter in menu rows (check dot)

// Controls.
constexpr int RB_IDC_LIST    = 100;

// ── Themed scrollbars ────────────────────────────────────────────────
// Same asset family and native-pixel rule as mod_list / plugin_manager:
// the art is blitted at native size, so scaling the gutter would push
// the arrow caps out of line with the track.
constexpr int RB_SB_W         = 30;
constexpr int RB_SB_GAP       = 4;
constexpr int RB_SB_THUMB_W   = 15;
constexpr int RB_SB_MIN_THUMB = 40;
constexpr int RB_SB_THUMB_CAP = 16;
constexpr int RB_SB_UP_H_FB   = 35;
constexpr int RB_SB_DOWN_H_FB = 32;
constexpr int RB_IDC_INSTALL = 101;
constexpr int RB_IDC_CLOSE   = 102;
constexpr int RB_IDC_MODCB   = 105;   // mod dropdown
constexpr int RB_IDC_REFRESH = 106;

HWND    g_rbHwnd    = nullptr;
HWND    g_rbList    = nullptr;
HWND    g_rbInstall = nullptr;
HWND    g_rbClose   = nullptr;
HWND    g_rbRefresh = nullptr;
bool    g_rbReg     = false;
RECT    g_rbToggleRect = {};   // scope toggle (btn_toggle1/3) click target
HFONT   g_rbListFont  = nullptr; // reader font for the list (matches mod list)
int     g_rbListX = 0, g_rbListY = 0;  // listbox window-relative origin (stone sampling)
RECT    g_rbDropRect     = {};   // painted mod-dropdown box (text_box.png) click target
RECT    g_rbScopeBoxRect = {};   // scope label box (text_box.png)
vector<wstring> g_rbMods;        // mod folder names for the dropdown
int     g_rbModSel = 0;          // selected mod index into g_rbMods
// True only while the mod dropdown's TrackPopupMenu is up. WM_MEASUREITEM /
// WM_DRAWITEM fire during that blocking call, and the modal also owner-draws
// its listbox + buttons, so the menu handlers gate on this to avoid claiming
// messages that belong to the other owner-drawn controls.
bool    g_rbMenuOpen = false;
int     g_rbMenuItemW = 0;       // measured menu row width (physical px)
HBRUSH  g_rbMenuBgBrush = nullptr; // MIM_BACKGROUND brush (kills the white gutter)
int     g_rbColSplit  = 0;     // x where the left column ends (computed at layout)

// Computed geometry for the two-column layout (all physical px). Both the
// paint pass and control placement derive from this so they stay in sync.
struct RbGeom {
    int W, H;
    int leftX, leftW;      // left framed list panel
    int listTop, listH;
    int rightX, rightW;    // right framed detail panel
    int rightTop, rightH;
    int cbX, cbY, cbW;     // dropdown (top-right)
    int btnY;              // bottom button row
    // Left list interior (inside the thin border panel).
    int listInX, listInY, listInW, listInH;
    int panelBottom;       // where the left frame's bottom edge sits
};

RbGeom ComputeRbGeom(int W, int H) {
    RbGeom g; g.W = W; g.H = H;
    int pad = (int)(RB_PAD * g_scale);
    int colGap = (int)(RB_COL_GAP * g_scale);
    // Left column ~52% of the usable width.
    int usable = W - pad * 2;
    g.leftW  = (int)(usable * 0.52) - colGap / 2;
    g.leftX  = pad;
    g.rightX = g.leftX + g.leftW + colGap;
    g.rightW = W - pad - g.rightX;

    int btnBlock = (int)((RB_BTN_H + RB_BTN_BOT + 14) * g_scale);
    g.listTop = (int)(RB_PANEL_TOP * g_scale);
    // Panels extend down to just above the button row (thin borders, so this
    // is a clean rectangle that meets the bottom margin).
    g.panelBottom = H - (int)((RB_BTN_H + RB_BTN_BOT + 6) * g_scale);
    g.listH   = g.panelBottom - g.listTop;

    g.rightTop = (int)(RB_RPANEL_TOP * g_scale);
    g.rightH   = g.panelBottom - g.rightTop;

    // List interior: a simple even inset inside the thin border (no rail to
    // avoid now — we use a clean 9-slice border, not the ornate rail asset).
    int inset = (int)(14 * g_scale);
    g.listInX = g.leftX + inset;
    g.listInW = g.leftW - inset * 2;
    g.listInY = g.listTop + inset;
    g.listInH = g.listH - inset * 2;

    // Dropdown sits in the top-right, under "INSTALL TO".
    g.cbW = g.rightW - (int)(20 * g_scale);
    g.cbX = g.rightX + (g.rightW - g.cbW) / 2;
    g.cbY = (int)(64 * g_scale);

    g.btnY = H - (int)(RB_BTN_BOT * g_scale) - (int)(RB_BTN_H * g_scale);
    return g;
}

RepoManifest g_rbManifest;
int     g_rbSel     = -1;      // selected entry index
bool    g_rbModLocal = false;  // scope switch state (false = Global)
bool    g_rbLoading = false;
wstring g_rbStatus;            // status/loading/error line

// Consent memory for the Global-mode "install mod-local file in X?" confirm.
// Reset when the switch flips to Global or the dropdown mod changes.
bool    g_rbConsented = false;
wstring g_rbConsentMod;

wstring CurrentDropdownMod() {
    if (g_rbModSel < 0 || g_rbModSel >= (int)g_rbMods.size()) return L"";
    return g_rbMods[g_rbModSel];
}

Gdiplus::Font* MakeUiFont(int px, bool bold) {
    Gdiplus::FontStyle style = bold ? FontStyleBold : FontStyleRegular;
    if (g_userFontFamilyOverride)
        return new Gdiplus::Font(g_userFontFamilyOverride, (REAL)px,
                                 (Gdiplus::FontStyle)(g_userFontStyleOverride | (bold ? FontStyleBold : 0)),
                                 UnitPixel);
    return new Gdiplus::Font(L"Segoe UI", (REAL)px, style, UnitPixel);
}

// Populate the mod list from g_mods; select `defaultMod` (or index 0).
void FillModCombo(const wstring& defaultMod) {
    g_rbMods.clear();
    g_rbModSel = 0;
    int i = 0;
    for (const ModInfo& m : g_mods) {
        wstring label = m.folder.empty() ? m.name : m.folder;
        g_rbMods.push_back(label);
        if (!defaultMod.empty() && _wcsicmp(label.c_str(), defaultMod.c_str()) == 0)
            g_rbModSel = i;
        ++i;
    }
}

// Fetch the catalog (blocking) and refresh the list.
void DoFetch() {
    g_rbLoading = true;
    g_rbStatus = L"Fetching catalog...";
    if (g_rbList) InvalidateRect(g_rbHwnd, nullptr, FALSE);
    // Blocking fetch — small JSON, acceptable for now. (A future rev could
    // thread this; for a hand-managed catalog the pause is brief.)
    g_rbManifest = FetchRepoManifest(REPO_MANIFEST_URL, 12000);
    g_rbLoading = false;
    g_rbSel = g_rbManifest.entries.empty() ? -1 : 0;

    if (g_rbList) {
        SendMessageW(g_rbList, LB_RESETCONTENT, 0, 0);
        for (const RepoEntry& e : g_rbManifest.entries)
            SendMessageW(g_rbList, LB_ADDSTRING, 0, (LPARAM)e.name.c_str());
        if (g_rbSel >= 0) SendMessageW(g_rbList, LB_SETCURSEL, g_rbSel, 0);
    }

    if (!g_rbManifest.ok)
        g_rbStatus = g_rbManifest.error;
    else if (g_rbManifest.entries.empty())
        g_rbStatus = L"The catalog is empty.";
    else
        g_rbStatus = L"";

    if (g_rbHwnd) InvalidateRect(g_rbHwnd, nullptr, FALSE);
}

} // namespace

// Owner-draw one list row: entry name (gold) + summary (dim) beneath.
static void RbDrawListRow(DRAWITEMSTRUCT* di) {
    if (di->itemID == (UINT)-1) return;
    if (di->itemID >= g_rbManifest.entries.size()) return;
    const RepoEntry& e = g_rbManifest.entries[di->itemID];
    HDC dc = di->hDC;
    RECT full = di->rcItem;
    bool sel = (di->itemState & ODS_SELECTED) != 0;

    // Transparent background: sample bg_stone at coordinates matching the
    // window's own stone tiling, so the pattern is continuous and the row
    // shows the panel background through — same technique the mod/plugin
    // list uses (PMDrawItem). g_rbListX/Y are the listbox's window-relative
    // origin, set when the list is created.
    Gdiplus::Graphics g(dc);
    int rowW = full.right - full.left, rowH = full.bottom - full.top;
    if (Gdiplus::Bitmap* stone = AssetImage(L"bg_stone.png")) {
        int srcX = g_rbListX + full.left;
        int srcY = g_rbListY + full.top;
        int sw = (int)stone->GetWidth(), sh = (int)stone->GetHeight();
        // Wrap source coords into the tile so sampling stays in-bounds.
        srcX %= sw; srcY %= sh;
        if (srcX < 0) srcX += sw; if (srcY < 0) srcY += sh;
        Gdiplus::Rect dst(full.left, full.top, rowW, rowH);
        // Draw with wrap by tiling a texture brush would be ideal; simplest
        // is to draw the stone shifted so the sampled region lands right.
        g.DrawImage(stone, dst, srcX, srcY, rowW, rowH, Gdiplus::UnitPixel);
    } else {
        HBRUSH bg = CreateSolidBrush(RGB(20, 17, 14));
        FillRect(dc, &full, bg); DeleteObject(bg);
    }

    // The framed "slot" is the cell inset by the row gap.
    int gap = (int)(RB_ROW_GAP * g_scale) / 2;
    RECT rc = full;
    rc.top += gap; rc.bottom -= gap;
    rc.left += (int)(2 * g_scale); rc.right -= (int)(2 * g_scale);

    // Selected row gets a translucent gold wash; unselected stays transparent
    // so the stone shows through cleanly.
    if (sel) {
        Gdiplus::SolidBrush wash(Gdiplus::Color(70, 0xC8, 0xA8, 0x50));
        g.FillRectangle(&wash, (INT)rc.left, (INT)rc.top,
                        (INT)(rc.right - rc.left), (INT)(rc.bottom - rc.top));
    }
    // Slot border — gold, brighter when selected.
    Gdiplus::Pen pen(sel ? Gdiplus::Color(0xC8, 0xA8, 0x60)
                         : Gdiplus::Color(0x6A, 0x5C, 0x40), 1.0f);
    g.DrawRectangle(&pen, (INT)rc.left, (INT)rc.top,
                    (INT)(rc.right - rc.left - 1), (INT)(rc.bottom - rc.top - 1));

    SetBkMode(dc, TRANSPARENT);
    int pad = (int)(12 * g_scale);
    // Text uses the listbox's own font (set via WM_SETFONT).
    SetTextColor(dc, sel ? RGB(0xF0, 0xDC, 0xA0) : RGB(0xD8, 0xC7, 0xA0));
    RECT nr = rc; nr.left += pad; nr.top += (int)(8 * g_scale); nr.right -= pad;
    DrawTextW(dc, e.name.c_str(), -1, &nr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (!e.summary.empty()) {
        SetTextColor(dc, RGB(0x9A, 0x8C, 0x70));
        RECT sr = rc; sr.left += pad; sr.top += (int)(30 * g_scale); sr.right -= pad;
        DrawTextW(dc, e.summary.c_str(), -1, &sr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    SetTextColor(dc, RGB(0x88, 0x7A, 0x60));
    RECT kr = rc; kr.right -= pad; kr.top += (int)(8 * g_scale);
    DrawTextW(dc, e.kind.c_str(), -1, &kr, DT_RIGHT | DT_TOP | DT_SINGLELINE);
}

// Owner-draw one row of the mod dropdown's popup menu. Mirrors
// PaintMenuItem in loader_options_modal.cpp so the two themed menus look
// identical: stone-toned cell, bronze separator, gold hairline on the
// hovered row, gold dot marking the current selection.
//
// Menu item IDs are index+1 — TrackPopupMenu returns 0 for "cancelled",
// so 0 can't be a real item.
static void RbDrawMenuItem(DRAWITEMSTRUCT* d) {
    int idx = (int)d->itemID - 1;
    if (idx < 0 || idx >= (int)g_rbMods.size()) return;

    Gdiplus::Graphics g(d->hDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    int rl = (int)d->rcItem.left;
    int rt = (int)d->rcItem.top;
    int rR = (int)d->rcItem.right;
    int rB = (int)d->rcItem.bottom;
    int rw = rR - rl;
    int rh = rB - rt;

    bool selected = (d->itemState & ODS_SELECTED) != 0;
    bool checked  = (d->itemState & ODS_CHECKED)  != 0;

    // Cell background. Hovered row lifts to BgPanel2, matching the
    // options modal's menu.
    Gdiplus::SolidBrush bg(selected ? Tok::BgPanel2 : Tok::BgPanel);
    g.FillRectangle(&bg, (INT)rl, (INT)rt, (INT)rw, (INT)rh);

    // Hairline separator along the bottom edge.
    Gdiplus::Pen sep(Tok::BronzeDim, 1.0f);
    g.DrawLine(&sep, (INT)rl, (INT)(rB - 1), (INT)rR, (INT)(rB - 1));

    // Hovered row gets an inset gold outline.
    if (selected) {
        Gdiplus::Pen glow(Tok::Gold, 1.0f);
        g.DrawRectangle(&glow, (INT)(rl + 1), (INT)(rt + 1),
                        (INT)(rw - 3), (INT)(rh - 3));
    }

    // Current selection marker — a small gold dot in the left gutter.
    // Owner-drawn menu items get no system check mark, so this is ours
    // to draw.
    if (checked) {
        int dd = (int)(7 * g_scale);
        if (dd < 4) dd = 4;
        Gdiplus::SolidBrush dot(Tok::GoldBright);
        g.FillEllipse(&dot, (INT)(rl + (int)(9 * g_scale)),
                      (INT)(rt + (rh - dd) / 2), (INT)dd, (INT)dd);
    }

    // Mod name, left-aligned past the gutter, vertically centred.
    int gut = (int)(RB_MENU_GUTTER * g_scale);
    Gdiplus::Font* mf = MakeUiFont((int)(14 * g_scale), false);
    if (mf) {
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentNear);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        Gdiplus::SolidBrush txt(selected ? Tok::GoldBright : Tok::Gold);
        g.DrawString(g_rbMods[idx].c_str(), -1, mf,
                     Gdiplus::RectF((REAL)(rl + gut), (REAL)rt,
                                    (REAL)(rw - gut - (int)(10 * g_scale)),
                                    (REAL)rh),
                     &sf, &txt);
        delete mf;
    }
}

// Paint the detail panel (right side) for the selected entry.
// ── Shared scrollbar ─────────────────────────────────────────────────
// Drives both the catalog listbox (which scrolls by ITEM) and the
// description body (which scrolls by PIXEL), so `pos`/`maxPos` are in
// whatever unit the caller works in.

struct RbSb {
    bool present = false;
    RECT area = {}, up = {}, down = {}, track = {}, thumb = {};
    int  trackTop = 0, trackH = 0;
    int  maxPos = 0;
};

static RbSb RbScrollbarGeom(const RECT& bar, int pos, int maxPos,
                            int visibleSpan, int contentSpan) {
    RbSb s;
    int h = (int)(bar.bottom - bar.top);
    if (h <= 0 || maxPos <= 0 || contentSpan <= 0) return s;

    int upH = RB_SB_UP_H_FB, downH = RB_SB_DOWN_H_FB;
    if (Gdiplus::Bitmap* a = AssetImage(L"scroll_up.png"))   upH   = (int)a->GetHeight();
    if (Gdiplus::Bitmap* a = AssetImage(L"scroll_down.png")) downH = (int)a->GetHeight();

    s.present = true;
    s.maxPos  = maxPos;
    s.area = bar;
    s.up   = { bar.left, bar.top,            bar.right, bar.top + upH };
    s.down = { bar.left, bar.bottom - downH, bar.right, bar.bottom };
    s.trackTop = (int)bar.top + upH;
    int trackBot = (int)bar.bottom - downH;
    s.trackH = trackBot - s.trackTop;
    if (s.trackH < 0) s.trackH = 0;
    s.track = { bar.left, s.trackTop, bar.right, trackBot };

    int thumbH = (int)((long long)s.trackH * visibleSpan / contentSpan);
    if (thumbH < RB_SB_MIN_THUMB) thumbH = RB_SB_MIN_THUMB;
    if (thumbH > s.trackH)        thumbH = s.trackH;
    int travel = s.trackH - thumbH;
    int top = s.trackTop;
    if (travel > 0) {
        if (pos < 0) pos = 0;
        if (pos > maxPos) pos = maxPos;
        top = s.trackTop + (int)((long long)pos * travel / maxPos);
    }
    int x = (int)bar.left + (RB_SB_W - RB_SB_THUMB_W) / 2;
    s.thumb = { x, top, x + RB_SB_THUMB_W, top + thumbH };
    return s;
}

static void RbDrawThumb(Graphics& g, Gdiplus::Bitmap* b,
                        int x, int y, int w, int h, int cap) {
    if (!b) return;
    int sw = (int)b->GetWidth(), sh = (int)b->GetHeight();
    if (h >= sh && h > cap * 2 && sh > cap * 2) {
        g.DrawImage(b, Rect(x, y, w, cap), 0, 0, sw, cap, UnitPixel);
        g.DrawImage(b, Rect(x, y + cap, w, h - cap * 2),
                    0, cap, sw, sh - cap * 2, UnitPixel);
        g.DrawImage(b, Rect(x, y + h - cap, w, cap),
                    0, sh - cap, sw, cap, UnitPixel);
    } else {
        g.DrawImage(b, Rect(x, y, w, h), 0, 0, sw, sh, UnitPixel);
    }
}

static void RbPaintScrollbar(Graphics& g, const RbSb& s) {
    if (!s.present) return;
    int aw = (int)(s.area.right - s.area.left);
    int ah = (int)(s.area.bottom - s.area.top);
    if (Gdiplus::Bitmap* tk = AssetImage(L"scrollbar_track.png")) {
        g.DrawImage(tk, Rect((INT)s.area.left, (INT)s.area.top, (INT)aw, (INT)ah),
                    0, 0, (INT)tk->GetWidth(), (INT)tk->GetHeight(), UnitPixel);
    } else {
        SolidBrush groove(Color(150, 0x10, 0x0A, 0x06));
        g.FillRectangle(&groove, (INT)s.area.left, (INT)s.area.top, (INT)aw, (INT)ah);
    }
    int tw = (int)(s.thumb.right - s.thumb.left);
    int th = (int)(s.thumb.bottom - s.thumb.top);
    if (Gdiplus::Bitmap* tb = AssetImage(L"scroll.png")) {
        RbDrawThumb(g, tb, (INT)s.thumb.left, (INT)s.thumb.top, tw, th,
                    RB_SB_THUMB_CAP);
    } else {
        SolidBrush grip(Tok::BronzeBright);
        g.FillRectangle(&grip, (INT)s.thumb.left, (INT)s.thumb.top, (INT)tw, (INT)th);
    }
    if (Gdiplus::Bitmap* up = AssetImage(L"scroll_up.png"))
        g.DrawImage(up, (INT)s.up.left, (INT)s.up.top,
                    (INT)up->GetWidth(), (INT)up->GetHeight());
    if (Gdiplus::Bitmap* dn = AssetImage(L"scroll_down.png"))
        g.DrawImage(dn, (INT)s.down.left, (INT)s.down.top,
                    (INT)dn->GetWidth(), (INT)dn->GetHeight());
}

// Description scroll state. Only the body scrolls — the title, byline
// and tag line stay pinned, so you keep sight of what you're reading
// about. Reset whenever the selection changes.
static int  g_rbDescScroll  = 0;   // physical px
static int  g_rbDescMax     = 0;   // computed during paint
static RECT g_rbDescBar     = {0,0,0,0};
static RECT g_rbDescView    = {0,0,0,0};
static bool g_rbDescDrag    = false;
static int  g_rbDescGrabDY  = 0;

// Draw a block of text wrapped to `w`, and return the height it
// actually consumed. The detail panel used to advance y by hardcoded
// amounts, which assumed every field fit on one line — a two-line name
// was clipped and the byline drew straight over it. Measuring first
// lets each block push the ones below it down.
//
// MeasureString is given the SAME layout rect and format as DrawString,
// so the height it reports is the height that will actually be painted.
static int RbDrawBlock(Graphics& g, const wchar_t* text,
                       Gdiplus::Font* f, const Brush& br,
                       int x, int y, int w, int maxH) {
    if (!f || !text || !*text || maxH <= 0) return 0;
    StringFormat sf;
    sf.SetAlignment(StringAlignmentNear);
    sf.SetLineAlignment(StringAlignmentNear);
    RectF layout((REAL)x, (REAL)y, (REAL)w, (REAL)maxH);
    RectF bounds;
    g.MeasureString(text, -1, f, layout, &sf, &bounds);
    g.DrawString(text, -1, f, layout, &sf, &br);
    // Round up — a fractional line height would otherwise let the next
    // block creep a pixel into the descenders of this one.
    int h = (int)(bounds.Height + 0.999f);
    if (h > maxH) h = maxH;
    return h;
}

static void RbPaintDetail(Graphics& g, int panelX, int panelY, int panelW, int panelH) {
    if (g_rbSel < 0 || g_rbSel >= (int)g_rbManifest.entries.size()) {
        SolidBrush dim(Color(0x90, 0x82, 0x68));
        Gdiplus::Font* f = g_fNavSm;
        if (f) {
            StringFormat sf; sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(g_rbStatus.empty() ? L"Select an item" : g_rbStatus.c_str(),
                -1, f, RectF((REAL)panelX, (REAL)panelY, (REAL)panelW, (REAL)panelH), &sf, &dim);
        }
        return;
    }
    const RepoEntry& e = g_rbManifest.entries[g_rbSel];
    int x = panelX, y = panelY;
    // Remaining vertical room at any point in the flow.
    auto avail = [&]() { return panelH - (y - panelY); };

    // Name (gold, large). Wraps to as many lines as it needs.
    SolidBrush gold(Tok::Gold);
    Gdiplus::Font* nameF = MakeUiFont((int)(22 * g_scale), true);
    y += RbDrawBlock(g, e.name.c_str(), nameF, gold, x, y, panelW, avail());
    delete nameF;
    y += (int)(10 * g_scale);

    // Meta line: author, version, updated.
    SolidBrush meta(Color(0xB0, 0xA0, 0x80));
    Gdiplus::Font* metaF = MakeUiFont((int)(13 * g_scale), false);
    wstring metaLine;
    if (!e.author.empty())  metaLine += L"by " + e.author;
    if (!e.version.empty()) metaLine += (metaLine.empty() ? L"" : L"   ") + (L"v" + e.version);
    if (!e.updated.empty()) metaLine += (metaLine.empty() ? L"" : L"   ") + (L"updated " + e.updated);
    if (!metaLine.empty() && avail() > 0) {
        y += RbDrawBlock(g, metaLine.c_str(), metaF, meta, x, y, panelW, avail());
        y += (int)(8 * g_scale);
    }
    delete metaF;

    // Kind + tags. Also wraps — a long tag list was being clipped
    // mid-word at the panel edge.
    SolidBrush tagBr(Color(0x88, 0x9A, 0x70));
    Gdiplus::Font* tagF = MakeUiFont((int)(12 * g_scale), false);
    wstring tagLine = e.kind;
    for (const wstring& t : e.tags) tagLine += L"  #" + t;
    if (avail() > 0) {
        y += RbDrawBlock(g, tagLine.c_str(), tagF, tagBr, x, y, panelW, avail());
        y += (int)(12 * g_scale);
    }
    delete tagF;

    // ── Description body — the only part that scrolls ────────────────
    // Everything above stays pinned: scrolling a long description
    // shouldn't cost you sight of which plugin you're reading about.
    SolidBrush desc(Color(0xD0, 0xC2, 0xA0));
    Gdiplus::Font* descF = MakeUiFont((int)(14 * g_scale), false);
    wstring body = e.description.empty() ? e.summary : e.description;
    int viewH = avail();
    if (viewH > 0 && descF) {
        // Measure the full body first to find out whether it overflows.
        StringFormat sfB;
        sfB.SetAlignment(StringAlignmentNear);
        sfB.SetLineAlignment(StringAlignmentNear);
        RectF probe((REAL)x, (REAL)y, (REAL)panelW, (REAL)100000);
        RectF bounds;
        g.MeasureString(body.c_str(), -1, descF, probe, &sfB, &bounds);
        int contentH = (int)(bounds.Height + 0.999f);

        bool scrolls = contentH > viewH;
        int textW = panelW;
        if (scrolls) textW = panelW - (RB_SB_W + RB_SB_GAP);
        if (textW < (int)(40 * g_scale)) { textW = panelW; scrolls = false; }

        // Re-measure at the narrower width — reserving the gutter makes
        // the text wrap differently, so the first measurement would
        // under-report the height and clip the last lines.
        if (scrolls) {
            RectF probe2((REAL)x, (REAL)y, (REAL)textW, (REAL)100000);
            g.MeasureString(body.c_str(), -1, descF, probe2, &sfB, &bounds);
            contentH = (int)(bounds.Height + 0.999f);
        }

        g_rbDescMax  = scrolls ? (contentH - viewH) : 0;
        if (g_rbDescScroll > g_rbDescMax) g_rbDescScroll = g_rbDescMax;
        if (g_rbDescScroll < 0)           g_rbDescScroll = 0;
        g_rbDescView = { x, y, x + textW, y + viewH };
        g_rbDescBar  = scrolls
            ? RECT{ x + panelW - RB_SB_W, y, x + panelW, y + viewH }
            : RECT{ 0, 0, 0, 0 };

        // Clip to the viewport so scrolled text can't spill over the
        // pinned header above or the panel border below.
        Gdiplus::Region prevClip;
        g.GetClip(&prevClip);
        g.SetClip(Rect((INT)x, (INT)y, (INT)textW, (INT)viewH),
                  CombineModeIntersect);
        g.DrawString(body.c_str(), -1, descF,
                     RectF((REAL)x, (REAL)(y - g_rbDescScroll),
                           (REAL)textW, (REAL)contentH),
                     &sfB, &desc);
        g.SetClip(&prevClip, CombineModeReplace);

        if (scrolls) {
            RbPaintScrollbar(g, RbScrollbarGeom(g_rbDescBar, g_rbDescScroll,
                                                g_rbDescMax, viewH, contentH));
        }
    } else {
        g_rbDescMax = 0;
        g_rbDescBar = { 0, 0, 0, 0 };
    }
    delete descF;
}

static LRESULT CALLBACK RepoBrowserProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        int W = rc.right, H = rc.bottom;
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);
        {
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            if (Gdiplus::Bitmap* stone = AssetImage(L"bg_stone.png")) {
                int sw=(int)stone->GetWidth(), sh=(int)stone->GetHeight();
                for (int yy=0; yy<H; yy+=sh) for (int xx=0; xx<W; xx+=sw)
                    g.DrawImage(stone, xx, yy, sw, sh);
            } else { SolidBrush b(Color(28,24,20)); g.FillRectangle(&b,0,0,W,H); }
            if (Gdiplus::Bitmap* frame = AssetImage(L"frame_modbanner.png"))
                DrawButton9Slice(g, frame, 0,0, W,H, 24);

            RbGeom gm = ComputeRbGeom(W, H);
            Gdiplus::Font* lf = g_fNavSm;
            Gdiplus::Font* tf = g_fModName ? g_fModName : g_fNavSm;
            SolidBrush gold(Tok::Gold);

            // ── Left title: catalog name ──
            if (tf) {
                StringFormat sfL; sfL.SetAlignment(StringAlignmentNear);
                sfL.SetLineAlignment(StringAlignmentCenter);
                wstring title = g_rbManifest.repoName.empty()
                    ? L"Plugin Repository" : g_rbManifest.repoName;
                g.DrawString(title.c_str(), -1, tf,
                    RectF((REAL)gm.leftX, (REAL)S(RB_TITLE_TOP),
                          (REAL)gm.leftW, (REAL)S(RB_TITLE_H + 8)), &sfL, &gold);
            }
            // ── Right title: "Install To" ──
            if (tf) {
                StringFormat sfC; sfC.SetAlignment(StringAlignmentCenter);
                sfC.SetLineAlignment(StringAlignmentCenter);
                g.DrawString(L"Install To", -1, tf,
                    RectF((REAL)gm.rightX, (REAL)S(RB_TITLE_TOP),
                          (REAL)gm.rightW, (REAL)S(RB_TITLE_H + 8)), &sfC, &gold);
            }

            // ── Both panels: clean thin 9-slice border (frame_modbanner has
            //    only top+bottom border lines, transparent interior — no baked
            //    rail or dividers to scramble). ──
            {
                int lpH = gm.panelBottom - gm.listTop;
                if (Gdiplus::Bitmap* pf = AssetImage(L"frame_modbanner.png"))
                    DrawButton9Slice(g, pf, gm.leftX, gm.listTop,
                                     gm.leftW, lpH, (int)(RB_FRAME_INSET * g_scale));
                int rpH = gm.panelBottom - gm.rightTop;
                if (Gdiplus::Bitmap* pf = AssetImage(L"frame_modbanner.png"))
                    DrawButton9Slice(g, pf, gm.rightX, gm.rightTop,
                                     gm.rightW, rpH, (int)(RB_FRAME_INSET * g_scale));
            }

            // ── Detail content inside the right panel ──
            if (g_rbLoading && lf) {
                SolidBrush st(Color(0xD0, 0xC0, 0x98));
                StringFormat sfc; sfc.SetAlignment(StringAlignmentCenter);
                sfc.SetLineAlignment(StringAlignmentCenter);
                g.DrawString(g_rbStatus.c_str(), -1, lf,
                    RectF((REAL)gm.rightX, (REAL)gm.rightTop,
                          (REAL)gm.rightW, (REAL)gm.rightH), &sfc, &st);
            } else {
                int dpad = (int)(20 * g_scale);
                RbPaintDetail(g, gm.rightX + dpad, gm.rightTop + dpad,
                              gm.rightW - dpad * 2, gm.rightH - dpad * 2);
            }

            // ── Mod dropdown (top-right): text_box.png chrome + chevron +
            //    current mod text (painted, click pops a menu) ──
            {
                const RECT& dr = g_rbDropRect;
                int dw = dr.right - dr.left, dh = dr.bottom - dr.top;
                if (Gdiplus::Bitmap* tb = AssetImage(L"text_box.png")) {
                    InterpolationMode prev = g.GetInterpolationMode();
                    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                    g.DrawImage(tb, (INT)dr.left, (INT)dr.top, dw, dh);
                    g.SetInterpolationMode(prev);
                } else {
                    SolidBrush box(Color(24, 20, 16)); g.FillRectangle(&box, (INT)dr.left, (INT)dr.top, dw, dh);
                    Pen bd(Color(0x6A, 0x5C, 0x40)); g.DrawRectangle(&bd, (INT)dr.left, (INT)dr.top, dw-1, dh-1);
                }
                // Current mod text (leave room for the chevron on the right).
                SolidBrush txt(Color(0xF0, 0xDC, 0xA0));
                StringFormat sfm; sfm.SetLineAlignment(StringAlignmentCenter);
                sfm.SetAlignment(StringAlignmentNear);
                sfm.SetFormatFlags(sfm.GetFormatFlags() | StringFormatFlagsNoWrap);
                wstring cur = (g_rbModSel >= 0 && g_rbModSel < (int)g_rbMods.size())
                              ? g_rbMods[g_rbModSel] : L"(no mods)";
                if (lf) g.DrawString(cur.c_str(), -1, lf,
                    RectF((REAL)(dr.left + S(12)), (REAL)dr.top,
                          (REAL)(dw - S(40)), (REAL)dh), &sfm, &txt);
                // Chevron at the right edge.
                if (Gdiplus::Bitmap* ch = AssetImage(L"dropdown_chevron.png")) {
                    int chW=(int)ch->GetWidth(), chH=(int)ch->GetHeight();
                    int th2 = dh - S(10); int tw2 = chW * th2 / chH;
                    int cx = dr.right - tw2 - S(8), cy = dr.top + (dh - th2)/2;
                    InterpolationMode prev = g.GetInterpolationMode();
                    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                    g.DrawImage(ch, cx, cy, tw2, th2);
                    g.SetInterpolationMode(prev);
                }
            }

            // ── Detail content inside the right panel ──
            if (g_rbLoading && lf) {
                SolidBrush st(Color(0xD0, 0xC0, 0x98));
                StringFormat sfc; sfc.SetAlignment(StringAlignmentCenter);
                sfc.SetLineAlignment(StringAlignmentCenter);
                g.DrawString(g_rbStatus.c_str(), -1, lf,
                    RectF((REAL)gm.rightX, (REAL)gm.rightTop,
                          (REAL)gm.rightW, (REAL)gm.rightH), &sfc, &st);
            } else {
                int dpad = (int)(20 * g_scale);
                RbPaintDetail(g, gm.rightX + dpad, gm.rightTop + dpad,
                              gm.rightW - dpad * 2, gm.rightH - dpad * 2);
            }

            // ── Bottom-right: scope label in a text_box.png + the toggle ──
            {
                const RECT& sb = g_rbScopeBoxRect;
                int sbw = sb.right - sb.left, sbh = sb.bottom - sb.top;
                if (Gdiplus::Bitmap* tb = AssetImage(L"text_box.png")) {
                    InterpolationMode prev = g.GetInterpolationMode();
                    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                    g.DrawImage(tb, (INT)sb.left, (INT)sb.top, sbw, sbh);
                    g.SetInterpolationMode(prev);
                } else {
                    SolidBrush box(Color(24, 20, 16)); g.FillRectangle(&box, (INT)sb.left, (INT)sb.top, sbw, sbh);
                    Pen bd(Color(0x6A, 0x5C, 0x40)); g.DrawRectangle(&bd, (INT)sb.left, (INT)sb.top, sbw-1, sbh-1);
                }
                SolidBrush stlab(Tok::Gold);
                if (lf) {
                    StringFormat sfCl; sfCl.SetAlignment(StringAlignmentCenter);
                    sfCl.SetLineAlignment(StringAlignmentCenter);
                    g.DrawString(g_rbModLocal ? L"Mod Local" : L"Global", -1, lf,
                        RectF((REAL)sb.left, (REAL)sb.top, (REAL)sbw, (REAL)sbh),
                        &sfCl, &stlab);
                }

                const RECT& tr = g_rbToggleRect;
                int tw = tr.right - tr.left, th = tr.bottom - tr.top;
                const wchar_t* toggleAsset = g_rbModLocal ? L"btn_toggle3.png"
                                                          : L"btn_toggle1.png";
                Gdiplus::Bitmap* tog = AssetImage(toggleAsset);
                if (tog) {
                    InterpolationMode prev = g.GetInterpolationMode();
                    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                    g.DrawImage(tog, (INT)tr.left, (INT)tr.top, tw, th);
                    g.SetInterpolationMode(prev);
                } else {
                    SolidBrush track(Color(60, 52, 40));
                    g.FillRectangle(&track, (INT)tr.left, (INT)tr.top, tw, th);
                    SolidBrush knob(Tok::Gold);
                    int kw = tw / 2;
                    g.FillRectangle(&knob, (INT)(g_rbModLocal ? tr.left + kw : tr.left),
                                    (INT)tr.top, kw, th);
                }
            }
        }
        BitBlt(hdc, 0,0, W,H, memDC, 0,0, SRCCOPY);
        SelectObject(memDC, oldBM); DeleteObject(memBM); DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        // Wheel scrolls the description when the cursor is over it.
        // WM_MOUSEWHEEL carries SCREEN coordinates, unlike the button
        // messages — converting is required or the hit test is nonsense.
        if (g_rbDescMax > 0) {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hw, &pt);
            RECT hot = g_rbDescView;
            if (g_rbDescBar.right > g_rbDescBar.left) hot.right = g_rbDescBar.right;
            if (PtInRect(&hot, pt)) {
                int delta = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
                g_rbDescScroll -= delta * (int)(48 * g_scale);
                if (g_rbDescScroll < 0) g_rbDescScroll = 0;
                if (g_rbDescScroll > g_rbDescMax) g_rbDescScroll = g_rbDescMax;
                RECT rp = g_rbDescView;
                rp.right = max(rp.right, g_rbDescBar.right);
                InvalidateRect(hw, &rp, FALSE);
                return 0;
            }
        }
        break;
    }

    case WM_MOUSEMOVE: {
        if (!g_rbDescDrag) break;
        RbSb s = RbScrollbarGeom(g_rbDescBar, g_rbDescScroll, g_rbDescMax,
                                 g_rbDescView.bottom - g_rbDescView.top,
                                 (g_rbDescView.bottom - g_rbDescView.top)
                                     + g_rbDescMax);
        int thumbH = (int)(s.thumb.bottom - s.thumb.top);
        int travel = s.trackH - thumbH;
        if (s.present && travel > 0) {
            int top = GET_Y_LPARAM(lp) - g_rbDescGrabDY;
            if (top < s.trackTop)          top = s.trackTop;
            if (top > s.trackTop + travel) top = s.trackTop + travel;
            g_rbDescScroll = (int)((long long)(top - s.trackTop)
                                   * g_rbDescMax / travel);
            RECT rp = g_rbDescView;
            rp.right = max(rp.right, g_rbDescBar.right);
            InvalidateRect(hw, &rp, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_rbDescDrag) {
            g_rbDescDrag = false;
            ReleaseCapture();
            InvalidateRect(hw, &g_rbDescBar, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        auto inRect = [&](const RECT& r){ return mx>=r.left && mx<r.right && my>=r.top && my<r.bottom; };

        // Description scrollbar claims the click before anything else.
        if (g_rbDescMax > 0 && g_rbDescBar.right > g_rbDescBar.left
            && inRect(g_rbDescBar)) {
            int viewH = g_rbDescView.bottom - g_rbDescView.top;
            RbSb s = RbScrollbarGeom(g_rbDescBar, g_rbDescScroll, g_rbDescMax,
                                     viewH, viewH + g_rbDescMax);
            POINT pt = { mx, my };
            int step = (int)(48 * g_scale);
            int newPos = g_rbDescScroll;
            if (s.present && PtInRect(&s.thumb, pt)) {
                g_rbDescDrag   = true;
                g_rbDescGrabDY = my - (int)s.thumb.top;
                SetCapture(hw);
                return 0;
            } else if (s.present && PtInRect(&s.up, pt)) {
                newPos -= step;
            } else if (s.present && PtInRect(&s.down, pt)) {
                newPos += step;
            } else if (s.present && PtInRect(&s.track, pt)) {
                newPos += (my < s.thumb.top) ? -viewH : viewH;
            } else {
                return 0;
            }
            if (newPos < 0) newPos = 0;
            if (newPos > g_rbDescMax) newPos = g_rbDescMax;
            if (newPos != g_rbDescScroll) {
                g_rbDescScroll = newPos;
                RECT rp = g_rbDescView;
                rp.right = max(rp.right, g_rbDescBar.right);
                InvalidateRect(hw, &rp, FALSE);
            }
            return 0;
        }

        // Scope toggle OR its label box → flip scope.
        if (inRect(g_rbToggleRect) || inRect(g_rbScopeBoxRect)) {
            g_rbModLocal = !g_rbModLocal;
            if (!g_rbModLocal) g_rbConsented = false;
            InvalidateRect(hw, nullptr, FALSE);
            return 0;
        }

        // Mod dropdown → themed popup menu of mods.
        if (inRect(g_rbDropRect) && !g_rbMods.empty()) {
            HMENU menu = CreatePopupMenu();

            // Owner-drawn items so RbDrawMenuItem paints them in the
            // stone/bronze theme instead of the system's grey menu.
            for (int i = 0; i < (int)g_rbMods.size(); ++i) {
                MENUITEMINFOW mii = { sizeof(mii) };
                mii.fMask  = MIIM_FTYPE | MIIM_ID | MIIM_STATE;
                mii.fType  = MFT_OWNERDRAW;
                mii.fState = (i == g_rbModSel) ? MFS_CHECKED : MFS_UNCHECKED;
                mii.wID    = (UINT)(i + 1);   // +1 so 0 can mean "cancelled"
                InsertMenuItemW(menu, (UINT)i, TRUE, &mii);
            }

            // Paint the menu's own background (the thin margin around the
            // items) dark, so no white system gutter shows at the edges.
            // The brush is kept for the modal's lifetime and freed in
            // WM_DESTROY.
            if (!g_rbMenuBgBrush)
                g_rbMenuBgBrush = CreateSolidBrush(Tok::crBgPanel);
            if (g_rbMenuBgBrush) {
                MENUINFO mi = { sizeof(mi) };
                mi.fMask   = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
                mi.hbrBack = g_rbMenuBgBrush;
                SetMenuInfo(menu, &mi);
            }

            // Menu rows match the dropdown box width so the popup lines up
            // flush under the control it drops from.
            g_rbMenuItemW = g_rbDropRect.right - g_rbDropRect.left;
            if (g_rbMenuItemW < (int)(120 * g_scale))
                g_rbMenuItemW = (int)(120 * g_scale);

            POINT pt = { g_rbDropRect.left, g_rbDropRect.bottom };
            ClientToScreen(hw, &pt);
            g_rbMenuOpen = true;
            int chosen = (int)TrackPopupMenu(menu,
                TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_LEFTBUTTON,
                pt.x, pt.y, 0, hw, nullptr);
            g_rbMenuOpen = false;
            DestroyMenu(menu);
            if (chosen > 0 && (chosen - 1) != g_rbModSel) {
                g_rbModSel = chosen - 1;
                g_rbConsented = false;   // mod changed → re-ask consent
                InvalidateRect(hw, nullptr, FALSE);
            }
            return 0;
        }
        return 0;
    }

    case WM_CTLCOLORLISTBOX: {
        // List background: a bg_stone pattern brush so the empty area below
        // rows shows stone (not grey). Rows themselves sample stone in
        // RbDrawListRow; this covers the gap beneath the last row. Text
        // transparent + gold. The brush origin is aligned to the list's
        // window position so the tiling lines up with the panel stone.
        HDC lbDC = (HDC)wp;
        SetBkMode(lbDC, TRANSPARENT);
        SetTextColor(lbDC, RGB(0xD8, 0xC7, 0xA0));
        static HBRUSH s_rbStoneBrush = nullptr;
        if (!s_rbStoneBrush) {
            if (Gdiplus::Bitmap* stone = AssetImage(L"bg_stone.png")) {
                HBITMAP hbm = nullptr;
                stone->GetHBITMAP(Gdiplus::Color(0,0,0), &hbm);
                if (hbm) { s_rbStoneBrush = CreatePatternBrush(hbm); DeleteObject(hbm); }
            }
            if (!s_rbStoneBrush) s_rbStoneBrush = CreateSolidBrush(RGB(20, 17, 14));
        }
        // Align the pattern to the list's window-relative origin so the
        // stone is continuous with the surrounding panel. SetBrushOrgEx
        // wraps the origin into the pattern automatically.
        SetBrushOrgEx(lbDC, -g_rbListX, -g_rbListY, nullptr);
        return (LRESULT)s_rbStoneBrush;
    }

    case WM_MEASUREITEM: {
        // Only the mod dropdown's popup menu is measured here. The
        // LBS_OWNERDRAWFIXED listbox also sends one WM_MEASUREITEM at
        // creation, but g_rbMenuOpen is false then so it falls through to
        // DefWindowProc — and LB_SETITEMHEIGHT sets the real row height
        // straight after, which wins regardless.
        MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*)lp;
        if (mis->CtlType == ODT_MENU && g_rbMenuOpen) {
            mis->itemWidth  = (UINT)g_rbMenuItemW;
            mis->itemHeight = (UINT)(RB_MENU_ITEM_H * g_scale);
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
        if (di->CtlType == ODT_MENU && g_rbMenuOpen) {
            RbDrawMenuItem(di); return TRUE;
        }
        if (di->CtlID == RB_IDC_LIST) { RbDrawListRow(di); return TRUE; }
        if (PaintOwnerDrawButton(di)) return TRUE;
        break;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wp), code = HIWORD(wp);
        if (id == RB_IDC_LIST && code == LBN_SELCHANGE) {
            // New entry — start its description at the top.
            g_rbDescScroll = 0;
            g_rbSel = (int)SendMessageW(g_rbList, LB_GETCURSEL, 0, 0);
            InvalidateRect(hw, nullptr, FALSE);
            return 0;
        }
        if (id == RB_IDC_REFRESH) { DoFetch(); return 0; }
        if (id == RB_IDC_INSTALL) { PostMessageW(hw, WM_APP + 1, 0, 0); return 0; }
        if (id == RB_IDC_CLOSE || id == IDCANCEL) { DestroyWindow(hw); return 0; }
        break;
    }

    case WM_APP + 1: {   // deferred install (so the button paint completes)
        if (g_rbSel < 0 || g_rbSel >= (int)g_rbManifest.entries.size()) return 0;
        const RepoEntry e = g_rbManifest.entries[g_rbSel];
        wstring mod = CurrentDropdownMod();

        // Global-mode consent for mod-local files: if installing globally but
        // the plugin could contain mod-local files, confirm the target mod
        // once (per Global session / per mod).
        RepoInstallScope scope = g_rbModLocal ? RepoInstallScope::ModLocal
                                              : RepoInstallScope::Global;
        if (!g_rbModLocal) {
            if (!g_rbConsented || _wcsicmp(g_rbConsentMod.c_str(), mod.c_str()) != 0) {
                wstring q = L"If \"" + e.name + L"\" includes mod-local files "
                            L"(like excel data), install them into:\n\n    " + mod +
                            L"\n\nOK to proceed?";
                int r = MessageBoxW(hw, q.c_str(), L"Install location",
                                    MB_OKCANCEL | MB_ICONQUESTION);
                if (r != IDOK) {
                    MessageBoxW(hw,
                        L"Use the dropdown menu to select where mod-local files "
                        L"should be installed.",
                        L"Install location", MB_OK | MB_ICONINFORMATION);
                    return 0;   // back to browser
                }
                g_rbConsented = true;
                g_rbConsentMod = mod;
            }
        }

        RepoInstallResult res = InstallRepoEntry(hw, e, scope, mod);
        UINT icon = (res == RepoInstallResult::Success) ? MB_ICONINFORMATION
                  : (res == RepoInstallResult::InstallDeclined) ? MB_ICONINFORMATION
                  : MB_ICONWARNING;
        MessageBoxW(hw, RepoInstallResultText(res, e).c_str(),
                    L"Repository", MB_OK | icon);
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(hw); return 0; }
        break;

    case WM_CLOSE: DestroyWindow(hw); return 0;

    case WM_DESTROY:
        if (HWND parent = GetWindow(hw, GW_OWNER)) {
            EnableWindow(parent, TRUE); SetActiveWindow(parent);
        }
        g_rbHwnd = nullptr; g_rbList = nullptr;
        g_rbInstall = nullptr; g_rbClose = nullptr; g_rbRefresh = nullptr;
        g_rbManifest = RepoManifest();
        if (g_rbListFont) { DeleteObject(g_rbListFont); g_rbListFont = nullptr; }
        if (g_rbMenuBgBrush) { DeleteObject(g_rbMenuBgBrush); g_rbMenuBgBrush = nullptr; }
        g_rbMenuOpen = false;
        g_rbSel = -1; g_rbConsented = false;
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

void ShowRepoBrowser(HWND parent, const wstring& defaultMod) {
    if (g_rbHwnd) return;
    if (g_cfg.d2rPath.empty()) {
        MessageBoxW(parent, L"Set your Diablo II: Resurrected path first.",
                    L"Repository", MB_OK | MB_ICONWARNING);
        return;
    }

    if (!g_rbReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = RepoBrowserProc;
        wc.hInstance     = g_hInst;
        wc.lpszClassName = L"AngirisRepoBrowser";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        g_rbReg = true;
    }

    int physW = (int)(RB_W * g_scale), physH = (int)(RB_H * g_scale);
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - physW) / 2;
    int y = pr.top  + ((pr.bottom - pr.top) - physH) / 2;

    g_rbHwnd = CreateWindowExW(0,
        L"AngirisRepoBrowser", L"Plugin Repository",
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, physW, physH, parent, nullptr, g_hInst, nullptr);
    if (!g_rbHwnd) return;

    RbGeom gm = ComputeRbGeom(physW, physH);

    // ── Mod dropdown: painted text_box.png + chevron (like Seed/On-Launch),
    //    NOT a system combobox. Click pops a themed menu. The rect is stored
    //    for painting + hit-testing; no child control is created.
    g_rbDropRect = { gm.cbX, gm.cbY, gm.cbX + gm.cbW, gm.cbY + (int)(34 * g_scale) };
    if (!g_rbListFont) g_rbListFont = MakeReaderFont(12);
    FillModCombo(defaultMod);   // populate g_rbMods + g_rbModSel

    // ── Left list: inside the left panel's thin border ──
    int listX = gm.listInX;
    int listY = gm.listInY;
    int listW = gm.listInW;
    int listH = gm.listInH;
    g_rbListX = listX; g_rbListY = listY;   // for bg_stone sampling in rows
    g_rbList = CreateWindowExW(0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED,
        listX, listY, listW, listH, g_rbHwnd, (HMENU)(UINT_PTR)RB_IDC_LIST,
        g_hInst, nullptr);
    if (g_rbList) {
        SendMessageW(g_rbList, LB_SETITEMHEIGHT, 0,
                     (LPARAM)(int)((RB_ROW_H + RB_ROW_GAP) * g_scale));
        if (g_rbListFont)
            SendMessageW(g_rbList, WM_SETFONT, (WPARAM)g_rbListFont, TRUE);
    }

    // ── Bottom row ──
    // Left: Install + Cancel. Right (left of Refresh): scope toggle with its
    // label in a text_box.png chrome (like ON LAUNCH's Min/Close box).
    int bw = (int)(150 * g_scale), bh = (int)(RB_BTN_H * g_scale);
    int by = gm.btnY;
    int gap = (int)(14 * g_scale);

    // Install + Cancel, left-aligned under the left panel.
    int leftBtnX = gm.leftX + (int)(4 * g_scale);
    g_rbInstall = MkStdBtn(g_rbHwnd, L"Install", RB_IDC_INSTALL,
                           leftBtnX, by, bw, bh, true, ButtonKind::Plugins);
    g_rbClose = MkStdBtn(g_rbHwnd, L"Cancel", RB_IDC_CLOSE,
                         leftBtnX + bw + gap, by, bw, bh, true, ButtonKind::Plugins);

    // Refresh (baked art), far bottom-right.
    int refW = (int)(138 * g_scale), refH = (int)(52 * g_scale);
    int refX = physW - (int)(RB_PAD * g_scale) - refW;
    g_rbRefresh = MkStdBtn(g_rbHwnd, L"Refresh", RB_IDC_REFRESH,
                           refX, by + (bh - refH) / 2, refW, refH,
                           true, ButtonKind::Refresh);

    // Scope toggle + its text_box label, positioned to the LEFT of Refresh.
    // Layout:  [ MOD LOCAL / GLOBAL  text_box ]   [toggle]      [Refresh]
    int togW = (int)(53 * g_scale), togH = (int)(26 * g_scale);
    int tbW  = (int)(150 * g_scale), tbH = (int)(34 * g_scale);
    int togX = refX - (int)(24 * g_scale) - togW;
    int togY = by + (bh - togH) / 2;
    g_rbToggleRect = { togX, togY, togX + togW, togY + togH };
    int tbX = togX - (int)(14 * g_scale) - tbW;
    int tbY = by + (bh - tbH) / 2;
    g_rbScopeBoxRect = { tbX, tbY, tbX + tbW, tbY + tbH };

    EnableWindow(parent, FALSE);
    ShowWindow(g_rbHwnd, SW_SHOW);
    UpdateWindow(g_rbHwnd);
    SetActiveWindow(g_rbHwnd);

    // Fetch after the window is up (so the loading state shows).
    DoFetch();

    MSG m;
    while (g_rbHwnd) {
        BOOL got = GetMessageW(&m, nullptr, 0, 0);
        if (got == 0 || got == -1) { if (got == 0) PostQuitMessage((int)m.wParam); break; }
        if (g_rbHwnd && IsDialogMessageW(g_rbHwnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}
