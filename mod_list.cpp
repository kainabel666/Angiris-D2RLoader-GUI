// ═══════════════════════════════════════════════════════════════════════
//  mod_list.cpp — see mod_list.h for the public interface
// ═══════════════════════════════════════════════════════════════════════
//
//  Implements the custom mod-list control: paint, hit-test, scroll,
//  hover dwell, selection. Everything below RegisterModListClass is
//  file-internal — the public API is just the registered class name
//  + the two ML_* notification messages declared in mod_list.h.
//

#include "mod_list.h"
#include "core.h"          // g_hwMain, GetClientRect helpers
#include "scaling.h"       // S/SF/U + GetClientRectL + InvalidateRectL
#include "colors.h"        // Tok::Gold, Tok::Bronze, GPA
#include "assets.h"        // AssetImage, DrawAsset*, DrawButton9Slice, MeasureFrameInset
#include "fonts.h"         // g_fModName / g_fModSub / g_fModPath / g_fBtn / g_fHeroMeta
#include "layout.h"        // LO::ROW_H, LO::PANEL_LEFT_PAD
#include "mod_types.h"     // ModInfo
#include "mod_scan.h"      // g_mods, g_selMod
#include "mod_updates.h"   // UpdateInfo, GetUpdateInfo
#include "control_ids.h"
#include "hover_tip.h"     // ShowHoverTip / HideHoverTip
#include "config.h"        // g_cfg.d2rPath

// ═══════════════════════════════════════════════════════════════════════
//  MOD LIST
// ═══════════════════════════════════════════════════════════════════════
//
//  Plain bordered rectangle rows, 96px tall, banner image as background
//  when modinfo.json provides one. When no banner is provided we render
//  a clean text fallback: mod Name top, and "by Author" + version on a
//  second line (version left-anchored at the row's horizontal midpoint).
//
//  Selection: brighter gold border + subtle gold tint overlay. The gold
//  ↑ badge appears in the top-right corner when an update is available.
//
//  Notifications sent to the parent (g_hwMain):
//    ML_REFRESH         — recompute layout + repaint
//    ML_NOTIFY_SELECT   — wp = new selected index

// Hover-tip dwell timer. Armed when the cursor enters a new row;
// fires after 2 s to show the tooltip. Killed on any row change,
// any scrollbar interaction, mod list refresh, or window destroy.
// File-internal — only ModListProc reads or writes the timer.
constexpr UINT IDT_HOVER_TIP = 9102;

// Banner image cache (file-local — only MLGetBanner touches it).
// Single-entry cache keyed by full path. Switching the selected mod
// or the hovered mod swaps the cached banner; the previous one is
// freed. We don't keep a multi-entry cache because banners can be
// large and most mods only ever surface one banner per session.
static Gdiplus::Bitmap* g_bannerCache    = nullptr;
static wstring          g_bannerCacheKey;

namespace ML {
    constexpr int ROW_GAP    = 6;            // vertical gap between rows
    constexpr int ROW_PAD    = 12;           // horizontal padding inside row
    constexpr int BADGE_SIZE = 22;           // gold ↑ update badge

    // ── Custom scrollbar ─────────────────────────────────────────────────
    // Right-edge gutter holding scrollbar_track.png (30 wide), scroll_up /
    // scroll_down arrow caps, and the scroll.png thumb (15 wide, centered).
    constexpr int SB_W         = 30;   // gutter / track / arrow width (asset native)
    constexpr int SB_PAD_R     = 4;    // gap from the list's right edge
    constexpr int SB_ROW_GAP   = 8;    // gap between rows and the scrollbar gutter
    constexpr int SB_THUMB_W   = 15;   // thumb asset native width
    constexpr int SB_MIN_THUMB = 40;   // smallest the proportional thumb shrinks to
    constexpr int SB_THUMB_CAP = 16;   // vertical 3-slice cap so the grip ends stay sharp
    constexpr int SB_UP_H_FB   = 35;   // arrow-cap height fallbacks (asset-measured at runtime)
    constexpr int SB_DOWN_H_FB = 32;
}

enum class SbPart { None, Up, Down, Thumb, Track };

struct MLState {
    int    scrollY    = 0;       // pixel scroll offset
    int    totalH     = 0;       // total content height
    int    hoverRow   = -1;
    SbPart sbHover    = SbPart::None;  // scrollbar element under cursor
    SbPart sbPressed  = SbPart::None;  // element pressed (arrows give visual feedback)
    bool   sbDragging = false;   // thumb being dragged
    int    sbGrabDY   = 0;       // mouse-Y minus thumb-top at drag start
};
static map<HWND, MLState*> g_mlStates;
static MLState* GetMLState(HWND hw) {
    auto it = g_mlStates.find(hw);
    return it == g_mlStates.end() ? nullptr : it->second;
}

static int MLRowTop(int rowIndex) {
    return rowIndex * (LO::ROW_H + ML::ROW_GAP);
}

static int MLRowAt(MLState* st, int clientY, int /*viewportH*/) {
    int surfY = clientY + st->scrollY;
    if (surfY < 0) return -1;
    int row = surfY / (LO::ROW_H + ML::ROW_GAP);
    if (row < 0 || row >= (int)g_mods.size()) return -1;
    int rowTop = MLRowTop(row);
    if (surfY >= rowTop + LO::ROW_H) return -1;     // in the gap
    return row;
}

static void MLClampScroll(MLState* st, int viewportH) {
    int maxScroll = max(0, st->totalH - viewportH);
    if (st->scrollY < 0) st->scrollY = 0;
    if (st->scrollY > maxScroll) st->scrollY = maxScroll;
}

// Returns one row's rect in the mod-list child's CLIENT space (i.e. the
// coordinates InvalidateRect expects when given g_hwList). Used by the
// selection-change handler to invalidate only the rows that actually
// changed visually, instead of repainting the whole list.
//
// Expanded by 4px on all sides to cover the selection halo MLPaintRow
// draws when a row is selected — otherwise the halo from a previously-
// selected row would linger when selection moves.
bool MLRowClientRect(HWND hwList, int rowIndex, RECT* out) {
    if (!hwList || rowIndex < 0 || rowIndex >= (int)g_mods.size()) return false;
    MLState* st = GetMLState(hwList);
    if (!st) return false;
    // Returns the row's rect in LOGICAL coords. Win32's GetClientRect
    // gives physical pixels; convert to logical so this rect can be
    // compared against MLRowTop()/LO::ROW_H (logical) and fed to
    // InvalidateRectL() at the call site.
    RECT rc; GetClientRectL(hwList, &rc);
    int W         = (int)rc.right;
    int viewportH = (int)rc.bottom;
    int top = MLRowTop(rowIndex) - st->scrollY;
    int bot = top + LO::ROW_H;
    if (bot <= 0 || top >= viewportH) return false;
    const int halo = 4;
    out->left   = max(0, ML::ROW_GAP - halo);
    out->top    = max(0, top - halo);
    out->right  = min(W,         W - ML::ROW_GAP + halo);
    out->bottom = min(viewportH, bot + halo);
    return true;
}

// Load a banner image (cached). Returns nullptr if missing/invalid.
static Gdiplus::Bitmap* MLGetBanner(const wstring& path) {
    if (path.empty()) return nullptr;
    if (g_bannerCache && g_bannerCacheKey == path) return g_bannerCache;
    delete g_bannerCache;
    g_bannerCache = nullptr;
    g_bannerCacheKey.clear();
    if (GetFileAttributes(path.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
    Gdiplus::Bitmap* b = new Gdiplus::Bitmap(path.c_str());
    if (!b || b->GetLastStatus() != Ok) { delete b; return nullptr; }
    g_bannerCache    = b;
    g_bannerCacheKey = path;
    return b;
}

// Paint one row at (rx, ry, rw, LO::ROW_H) in client space.
static void MLPaintRow(Graphics& g, const ModInfo& mod, int rx, int ry, int rw,
                       bool selected, bool hover, const UpdateInfo* upd) {
    using namespace LO;
    const int rh = ROW_H;

    // ── Selection halo (drawn FIRST, behind everything) ─────────────────
    // 4px outer gold rectangle, alpha-blended, gives the selected row a
    // soft "lit" feel without changing the frame asset itself.
    if (selected) {
        const int halo = 4;
        SolidBrush haloBr(GPA(70, 0xE8, 0xC2, 0x5E));
        g.FillRectangle(&haloBr, rx - halo, ry - halo,
                        rw + halo * 2, rh + halo * 2);
    }

    // ── Banner / text content (the row's interior) ──────────────────────
    // Inset slightly so the gold frame border (drawn last) doesn't overlap
    // the artwork. The frame asset is 565×124 with ~3-4px border inside,
    // so an inset of 4 keeps banners cleanly inside the frame.
    constexpr int FRAME_INSET = 4;
    int ix = rx + FRAME_INSET;
    int iy = ry + FRAME_INSET;
    int iw = rw - FRAME_INSET * 2;
    int ih = rh - FRAME_INSET * 2;

    // Banner backdrop. With a banner, we fill near-black underneath so any
    // transparent edges in the image look right. Without a banner, leave the
    // interior transparent so the parent panel backdrop shows through and
    // only the text + frame_modbanner border read — the row becomes a clean
    // text-only slot instead of a hard dark box.
    Gdiplus::Bitmap* banner = MLGetBanner(mod.bannerPath);
    if (banner) {
        SolidBrush bgBr(Tok::BgDeep);
        g.FillRectangle(&bgBr, ix, iy, iw, ih);
        // "object-fit: cover": scale to fill the interior, cropping any
        // overflow. Wide banners crop horizontally; tall banners crop top/bottom.
        UINT bw = banner->GetWidth(), bh = banner->GetHeight();
        if (bw > 0 && bh > 0) {
            float rowRatio = (float)iw / (float)ih;
            float imgRatio = (float)bw / (float)bh;
            int sx, sy, sw, sh;
            if (imgRatio > rowRatio) {
                sh = bh; sw = (int)(bh * rowRatio);
                sx = (bw - sw) / 2; sy = 0;
            } else {
                sw = bw; sh = (int)(bw / rowRatio);
                sx = 0; sy = (bh - sh) / 2;
            }
            g.DrawImage(banner,
                        RectF((REAL)ix, (REAL)iy, (REAL)iw, (REAL)ih),
                        (REAL)sx, (REAL)sy, (REAL)sw, (REAL)sh,
                        UnitPixel);
        }
    } else {
        // Text fallback: Mod Name top, "by Author" + "v X.Y.Z" second line.
        StringFormat sfLeft;
        sfLeft.SetAlignment(StringAlignmentNear);
        sfLeft.SetLineAlignment(StringAlignmentNear);
        // Title in Tok::Gold (antique gold ~0xC8/0xA8/0x4B) rather than
        // Tok::GoldBright (pure 0xFF/0xD7/0x00). GoldBright was reading
        // too saturated and too bright when used as a constant body color
        // — it's fine for selection highlights and hover accents where
        // the brightness is the affordance, but as a steady title color
        // it dominated the row. Gold drops saturation from ~100% to
        // ~53% and brightness from 100% to ~78%, still reads as warm
        // gold against the stone background and still picks up the
        // user's selected accent (Tok::Gold tracks the Colour dropdown
        // via ApplyColorChange).
        SolidBrush titleBr(Tok::Gold);
        SolidBrush dimBr(Tok::TextParchment);

        const wchar_t* title = !mod.title.empty() ? mod.title.c_str()
                                                  : mod.folder.c_str();
        // Title rect height 50 (was 40) — SF(38) Exocet has a line-
        // height/em ratio around 1.25, which at g_scale 1.275–1.9125
        // means the rendered line height exceeds a 40-tall rect and
        // the descenders clip. 50 gives 12 logical px of headroom over
        // SF(38)'s em, comfortably absorbing the line gap at all scales.
        // (Author/version rect below shifts down 4 logical to absorb.)
        g.DrawString(title, -1, g_fHeroName ? g_fHeroName : g_fBtn,
                     RectF((REAL)(ix + ML::ROW_PAD - FRAME_INSET),
                           (REAL)(iy + 8),
                           (REAL)(iw - ML::ROW_PAD * 2 + FRAME_INSET * 2),
                           50.0f),
                     &sfLeft, &titleBr);

        wstring authorLine = mod.author.empty() ? L"" : (L"by " + mod.author);
        // Author/version rect lowered by 4 logical px to clear the
        // title's new 50-tall rect (title now ends at iy + 58).
        g.DrawString(authorLine.c_str(), -1, g_fBtn,
                     RectF((REAL)(ix + ML::ROW_PAD - FRAME_INSET),
                           (REAL)(iy + ih - 26),
                           (REAL)(iw / 2 - ML::ROW_PAD + FRAME_INSET),
                           20.0f),
                     &sfLeft, &dimBr);
        if (!mod.version.empty()) {
            wstring versionLine = L"v " + mod.version;
            g.DrawString(versionLine.c_str(), -1, g_fBtn,
                         RectF((REAL)(ix + iw / 2),
                               (REAL)(iy + ih - 26),
                               (REAL)(iw / 2 - ML::ROW_PAD + FRAME_INSET),
                               20.0f),
                         &sfLeft, &dimBr);
        }
    }

    // ── State tints (gold overlay inside the interior) ──────────────────
    // Drawn on top of the banner art so the row reads as "warmed up".
    if (selected) {
        SolidBrush tint(GPA(50, 0xE8, 0xC2, 0x5E));
        g.FillRectangle(&tint, ix, iy, iw, ih);
    } else if (hover) {
        SolidBrush tint(GPA(25, 0xE8, 0xC2, 0x5E));
        g.FillRectangle(&tint, ix, iy, iw, ih);
    }

    // ── Frame overlay (the gold border) ─────────────────────────────────
    // Stretched to the row's actual dimensions. If the asset is missing
    // we fall back to a programmatic bronze rectangle so the row is still
    // visible.
    Gdiplus::Bitmap* frame = AssetImage(L"frame_modbanner.png");
    if (frame) {
        InterpolationMode prev = g.GetInterpolationMode();
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(frame, rx, ry, rw, rh);
        g.SetInterpolationMode(prev);
    } else {
        Pen border(selected ? Tok::GoldBright
                            : (hover ? Tok::Gold : Tok::Bronze),
                   selected ? 2.0f : 1.0f);
        g.DrawRectangle(&border, rx, ry, rw - 1, rh - 1);
    }

    // ── Update badge (gold ↑ in the top-right corner) ───────────────────
    if (upd && upd->available) {
        int bx = rx + rw - ML::BADGE_SIZE - 6;
        int by = ry + 6;
        SolidBrush badgeBg(Tok::GoldBright);
        g.FillEllipse(&badgeBg, bx, by, ML::BADGE_SIZE, ML::BADGE_SIZE);
        Pen badgeBorder(Tok::Gold, 1.5f);
        g.DrawEllipse(&badgeBorder, bx, by, ML::BADGE_SIZE - 1, ML::BADGE_SIZE - 1);
        StringFormat sfC;
        sfC.SetAlignment(StringAlignmentCenter);
        sfC.SetLineAlignment(StringAlignmentCenter);
        SolidBrush dark(Tok::BgDeep);
        g.DrawString(L"\u2191", -1, g_fBtn,
                     RectF((REAL)bx, (REAL)by,
                           (REAL)ML::BADGE_SIZE, (REAL)ML::BADGE_SIZE),
                     &sfC, &dark);
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  CUSTOM SCROLLBAR — geometry, thumb draw, paint
// ═══════════════════════════════════════════════════════════════════════
//  All scrollbar pixels live in a right-edge gutter inside the list window,
//  so a single window proc owns both row and scrollbar input. Geometry is
//  derived purely from the client rect + content height, so the paint pass
//  and the mouse handlers agree without shared mutable state.

struct SbGeom {
    bool present    = false;  // gutter is drawn (always, when there are mods)
    bool scrollable = false;  // content overflows → thumb can move
    RECT area  = {};          // full gutter
    RECT up    = {};          // up-arrow cap
    RECT down  = {};          // down-arrow cap
    RECT track = {};          // groove between the arrows
    RECT thumb = {};          // grip
    int  maxScroll = 0;       // == max(0, totalH - viewportH)
    int  trackTop  = 0, trackH = 0;
};

// Compute every scrollbar rect from the live client rect and mod count.
// `scrollY` is read (and treated as already-clamped by the caller) only to
// position the thumb.
static SbGeom MLScrollbarGeom(MLState* st, const RECT& rc) {
    using namespace ML;
    SbGeom s;
    int W = rc.right, H = rc.bottom;
    int n = (int)g_mods.size();
    if (n <= 0 || W <= 0 || H <= 0) return s;

    int totalH = n * LO::ROW_H + (n - 1) * ROW_GAP;
    s.maxScroll = max(0, totalH - H);

    int upH   = SB_UP_H_FB,   downH = SB_DOWN_H_FB;
    if (Gdiplus::Bitmap* a = AssetImage(L"scroll_up.png"))   upH   = (int)a->GetHeight();
    if (Gdiplus::Bitmap* a = AssetImage(L"scroll_down.png")) downH = (int)a->GetHeight();

    s.present = true;
    s.area.right  = W - SB_PAD_R;
    s.area.left   = s.area.right - SB_W;
    s.area.top    = 0;
    s.area.bottom = H;

    s.up   = { s.area.left, 0,         s.area.right, upH };
    s.down = { s.area.left, H - downH, s.area.right, H };

    s.trackTop = upH;
    int trackBot = H - downH;
    s.trackH = max(0, trackBot - s.trackTop);
    s.track  = { s.area.left, s.trackTop, s.area.right, trackBot };

    // Thumb: proportional height with a floor, positioned by scroll fraction.
    s.scrollable = (s.maxScroll > 0) && (s.trackH > SB_MIN_THUMB);
    int thumbH;
    if (!s.scrollable) {
        thumbH = s.trackH;                       // fills the track when nothing to scroll
    } else {
        thumbH = (int)((long long)s.trackH * H / totalH);
        thumbH = max(SB_MIN_THUMB, min(thumbH, s.trackH));
    }
    int thumbTop = s.trackTop;
    if (s.scrollable) {
        int travel = s.trackH - thumbH;
        thumbTop = s.trackTop + (int)((long long)st->scrollY * travel / s.maxScroll);
        thumbTop = max(s.trackTop, min(thumbTop, s.trackTop + travel));
    }
    int thumbX = s.area.left + (SB_W - SB_THUMB_W) / 2;
    s.thumb = { thumbX, thumbTop, thumbX + SB_THUMB_W, thumbTop + thumbH };
    return s;
}

// Vertical 3-slice for the thumb: keep `cap` px of art at top and bottom,
// stretch the middle. Drawn at the asset's native width (no horizontal
// distortion). Falls back to a plain stretch if the thumb is shorter than
// the two caps combined.
static void MLDrawThumb(Graphics& g, Gdiplus::Bitmap* b,
                        int x, int y, int w, int h, int cap) {
    if (!b) return;
    int sw = (int)b->GetWidth(), sh = (int)b->GetHeight();
    if (h >= sh && h > cap * 2 && sh > cap * 2) {
        g.DrawImage(b, Rect(x, y, w, cap), 0, 0, sw, cap, UnitPixel);
        g.DrawImage(b, Rect(x, y + cap, w, h - cap * 2),
                    0, cap, sw, sh - cap * 2, UnitPixel);
        g.DrawImage(b, Rect(x, y + h - cap, w, cap), 0, sh - cap, sw, cap, UnitPixel);
    } else {
        g.DrawImage(b, Rect(x, y, w, h), 0, 0, sw, sh, UnitPixel);
    }
}

// Paint the gutter: stretched track, arrow caps, thumb, with a subtle
// translucent highlight on the hovered / pressed element (single-asset
// pattern — no per-state art).
static void MLPaintScrollbar(Graphics& g, MLState* st, const RECT& rc) {
    SbGeom s = MLScrollbarGeom(st, rc);
    if (!s.present) return;

    auto W = [](const RECT& r) -> int { return (int)(r.right - r.left); };
    auto H = [](const RECT& r) -> int { return (int)(r.bottom - r.top); };

    // Track groove (stretched vertically across the whole gutter).
    if (Gdiplus::Bitmap* tk = AssetImage(L"scrollbar_track.png")) {
        g.DrawImage(tk, Rect((INT)s.area.left, (INT)s.area.top, W(s.area), H(s.area)),
                    0, 0, (INT)tk->GetWidth(), (INT)tk->GetHeight(), UnitPixel);
    } else {
        SolidBrush groove(GPA(150, 0x10, 0x0A, 0x06));
        g.FillRectangle(&groove, (INT)s.area.left, (INT)s.area.top, W(s.area), H(s.area));
        Pen edge(Tok::BronzeDim, 1.0f);
        g.DrawRectangle(&edge, (INT)s.area.left, (INT)s.area.top, W(s.area) - 1, H(s.area) - 1);
    }

    // Thumb (only meaningful when there's something to scroll, but we draw
    // it always — full-track when not scrollable — to match the mockup).
    if (Gdiplus::Bitmap* th = AssetImage(L"scroll.png")) {
        MLDrawThumb(g, th, (INT)s.thumb.left, (INT)s.thumb.top,
                    W(s.thumb), H(s.thumb), ML::SB_THUMB_CAP);
    } else {
        LinearGradientBrush grip(
            RectF((REAL)s.thumb.left, (REAL)s.thumb.top,
                  (REAL)W(s.thumb), (REAL)max(1, H(s.thumb))),
            Tok::BronzeBright, Tok::BronzeDim, LinearGradientModeHorizontal);
        g.FillRectangle(&grip, (INT)s.thumb.left, (INT)s.thumb.top, W(s.thumb), H(s.thumb));
        Pen rim(Tok::Gold, 1.0f);
        g.DrawRectangle(&rim, (INT)s.thumb.left, (INT)s.thumb.top, W(s.thumb) - 1, H(s.thumb) - 1);
    }

    // Arrow caps.
    if (Gdiplus::Bitmap* up = AssetImage(L"scroll_up.png"))
        g.DrawImage(up, (INT)s.up.left, (INT)s.up.top, (INT)up->GetWidth(), (INT)up->GetHeight());
    if (Gdiplus::Bitmap* dn = AssetImage(L"scroll_down.png"))
        g.DrawImage(dn, (INT)s.down.left, (INT)s.down.top, (INT)dn->GetWidth(), (INT)dn->GetHeight());

    // Hover / press highlight.
    auto glow = [&](const RECT& r, int alpha) {
        SolidBrush hi(GPA(alpha, 0xFF, 0xE0, 0x90));
        g.FillRectangle(&hi, (INT)r.left, (INT)r.top, W(r), H(r));
    };
    if (st->sbPressed == SbPart::Up   || st->sbHover == SbPart::Up)
        glow(s.up,   st->sbPressed == SbPart::Up   ? 22 : 36);
    if (st->sbPressed == SbPart::Down || st->sbHover == SbPart::Down)
        glow(s.down, st->sbPressed == SbPart::Down ? 22 : 36);
    if (st->sbDragging || st->sbHover == SbPart::Thumb)
        glow(s.thumb, st->sbDragging ? 30 : 24);
}

static void MLPaint(HWND hw, HDC hdc, MLState* st, const RECT& dirty) {
    RECT rc; GetClientRect(hw, &rc);
    // Convert the physical client size to LOGICAL pixels so all paint code
    // below (row positions, scrollbar geometry, hover/selection rects) can
    // be written in the same logical units as Layout/LO::* constants. The
    // Graphics gets a ScaleTransform that maps logical → physical at output.
    int W = U(rc.right), H = U(rc.bottom);

    // Size the back buffer to the dirty rect only — much cheaper than
    // allocating one big enough for the entire list when a single row
    // changed (e.g. on hover or selection). The dirty rect stays in
    // PHYSICAL pixels because the MemDC bitmap is at physical resolution;
    // GDI+'s ScaleTransform composes with the MemDC's device-origin offset
    // so logical paint coords still land on the right physical region.
    int dx = dirty.left, dy = dirty.top;
    int dw = dirty.right  - dirty.left;
    int dh = dirty.bottom - dirty.top;
    if (dw <= 0 || dh <= 0) return;

    MemDC m(hdc, dx, dy, dw, dh);
    Graphics g(m.dc);
    g.ScaleTransform((REAL)g_scale, (REAL)g_scale);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // Backdrop: paint the parent's stone showing through. We composite
    // the stone texture in our own coords (relative to our HWND), offset
    // by our position within the parent. This makes the list visually
    // "sit on" the stone instead of having an opaque black rectangle.
    //
    // Two coordinate systems are in play: MapWindowPoints and the parent's
    // client rect return PHYSICAL pixels (Win32 doesn't know about
    // g_scale), but the destination of DrawImage is now LOGICAL (the
    // Graphics has a ScaleTransform). The source-coordinate fraction
    // (dx + origin.x) / pc.right is scale-invariant — it's the same ratio
    // in physical and logical units — so we can leave that math alone and
    // only convert the destination rect.
    int lDx = U(dx), lDy = U(dy), lDw = U(dw), lDh = U(dh);
    POINT origin = { 0, 0 };
    MapWindowPoints(hw, GetParent(hw), &origin, 1);
    if (Gdiplus::Bitmap* bgStone = AssetImage(L"bg_stone.png")) {
        RECT pc; GetClientRect(GetParent(hw), &pc);
        // Paint the parent's stone region that falls under our window —
        // we shift the image by our negative position so its pixels line
        // up with the parent's.
        g.DrawImage(bgStone,
            Rect(lDx, lDy, lDw, lDh),
            (REAL)(dx + origin.x) * (REAL)bgStone->GetWidth()  / (REAL)pc.right,
            (REAL)(dy + origin.y) * (REAL)bgStone->GetHeight() / (REAL)pc.bottom,
            (REAL)dw * (REAL)bgStone->GetWidth()  / (REAL)pc.right,
            (REAL)dh * (REAL)bgStone->GetHeight() / (REAL)pc.bottom,
            UnitPixel);
    } else {
        SolidBrush bgBr(Tok::BgDeep);
        g.FillRectangle(&bgBr, lDx, lDy, lDw, lDh);
    }

    if (g_mods.empty()) {
        StringFormat sfC;
        sfC.SetAlignment(StringAlignmentCenter);
        sfC.SetLineAlignment(StringAlignmentCenter);
        SolidBrush dimBr(Tok::TextDim);
        g.DrawString(L"No mods found.", -1, g_fBtn,
                     RectF(0, 0, (REAL)W, (REAL)H), &sfC, &dimBr);
        return;
    }

    // Update content height + clamp scroll
    int n = (int)g_mods.size();
    st->totalH = n * LO::ROW_H + (n - 1) * ML::ROW_GAP;
    MLClampScroll(st, H);

    // Compute visible row range
    int firstVis = max(0, st->scrollY / (LO::ROW_H + ML::ROW_GAP));
    int lastVis  = min(n - 1,
                       (st->scrollY + H) / (LO::ROW_H + ML::ROW_GAP));

    // Dirty rect comes from BeginPaint in PHYSICAL pixels; convert to
    // LOGICAL here so the per-row skip check below compares apples to
    // apples against the logical `ry` and `LO::ROW_H`. Without this,
    // any partial invalidate (hover transition, selection change,
    // scrollbar-area repaint) at scales where S(ry+ROW_H) > dirty.bottom
    // physical erases the bottom row's pixels but skips its repaint —
    // and the threshold for that is exactly g_scale ≈ 1.25, which is
    // why 70% (g_scale=1.05) worked and 85% (g_scale=1.275) broke.
    int ldTop = U(dirty.top);
    int ldBot = U(dirty.bottom);

    for (int i = firstVis; i <= lastVis; ++i) {
        int rx = ML::ROW_GAP;
        int ry = MLRowTop(i) - st->scrollY;
        // Reserve the right-edge gutter for the custom scrollbar.
        int rw = (W - ML::SB_PAD_R - ML::SB_W - ML::SB_ROW_GAP) - rx;

        // Skip rows that don't intersect the dirty rect (logical vs logical).
        if (ry + LO::ROW_H <= ldTop || ry >= ldBot) continue;

        bool selected = (i == g_selMod);
        bool hover    = (i == st->hoverRow) && !selected;

        const UpdateInfo* upd = nullptr;
        auto it = g_updateInfo.find(g_mods[i].folder);
        if (it != g_updateInfo.end()) upd = &it->second;

        MLPaintRow(g, g_mods[i], rx, ry, rw, selected, hover, upd);
    }

    // Custom scrollbar in the right gutter (drawn last, over the stone).
    // Construct a logical rect so the scrollbar geometry is in the same
    // space as the row paints above (the Graphics has a ScaleTransform).
    RECT lrc = { 0, 0, W, H };
    MLPaintScrollbar(g, st, lrc);
}

static void MLScrollBy(HWND hw, MLState* st, int dy) {
    RECT rc; GetClientRectL(hw, &rc);
    st->scrollY += dy;
    MLClampScroll(st, rc.bottom);
    InvalidateRect(hw, nullptr, FALSE);
}

static LRESULT CALLBACK ModListProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    MLState* st = GetMLState(hw);
    switch (msg) {

    case WM_NCCREATE: {
        st = new MLState;
        g_mlStates[hw] = st;
        return DefWindowProc(hw, msg, wp, lp);
    }

    case WM_NCDESTROY: {
        if (st) { delete st; g_mlStates.erase(hw); }
        return DefWindowProc(hw, msg, wp, lp);
    }

    case WM_ERASEBKGND: return TRUE;

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hw, &ps);
        if (st) MLPaint(hw, hdc, st, ps.rcPaint);
        EndPaint(hw, &ps);
        return 0;
    }

    case ML_REFRESH: {
        if (st) { st->scrollY = 0; st->hoverRow = -1; }
        InvalidateRect(hw, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!st) return 0;
        int x = U(GET_X_LPARAM(lp)), y = U(GET_Y_LPARAM(lp));
        RECT rc; GetClientRectL(hw, &rc);

        // While dragging the thumb, map mouse-Y → scrollY and bail.
        if (st->sbDragging) {
            SbGeom s = MLScrollbarGeom(st, rc);
            int thumbH = s.thumb.bottom - s.thumb.top;
            int travel = s.trackH - thumbH;
            if (travel > 0 && s.maxScroll > 0) {
                int newTop = y - st->sbGrabDY;
                newTop = max(s.trackTop, min(newTop, s.trackTop + travel));
                st->scrollY = (int)((long long)(newTop - s.trackTop) * s.maxScroll / travel);
                MLClampScroll(st, rc.bottom);
                InvalidateRect(hw, nullptr, FALSE);
            }
            return 0;
        }

        // Scrollbar hover.
        SbGeom s = MLScrollbarGeom(st, rc);
        SbPart newSb = SbPart::None;
        if (s.present) {
            POINT p = { x, y };
            if      (PtInRect(&s.thumb, p)) newSb = SbPart::Thumb;
            else if (PtInRect(&s.up,    p)) newSb = SbPart::Up;
            else if (PtInRect(&s.down,  p)) newSb = SbPart::Down;
            else if (PtInRect(&s.track, p)) newSb = SbPart::Track;
        }
        if (newSb != st->sbHover) {
            st->sbHover = newSb;
            InvalidateRectL(hw, &s.area, FALSE);
        }

        // Row hover — suppressed while the cursor is over the gutter.
        int newHover = (newSb == SbPart::None) ? MLRowAt(st, y, rc.bottom) : -1;
        if (newHover != st->hoverRow) {
            int prevHover = st->hoverRow;
            st->hoverRow = newHover;
            RECT r;
            if (prevHover >= 0 && MLRowClientRect(hw, prevHover, &r))
                InvalidateRectL(hw, &r, FALSE);
            if (newHover  >= 0 && MLRowClientRect(hw, newHover,  &r))
                InvalidateRectL(hw, &r, FALSE);
            // Hover transition — anything that was on its way to a
            // tooltip is now stale. Kill the pending timer and hide
            // the tip if it was already showing. If we moved onto a
            // new row, arm a fresh 2 s timer; if we moved off rows
            // entirely (newHover == -1), leave the timer dead.
            KillTimer(hw, IDT_HOVER_TIP);
            HideHoverTip();
            if (newHover >= 0) {
                SetTimer(hw, IDT_HOVER_TIP, 2000, nullptr);
            }
        }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hw, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_TIMER: {
        // The 2-second dwell timer expired. If the cursor is still on
        // a row, show the playtime tooltip for it. The timer is
        // one-shot — we KillTimer here and re-arm on the next hover
        // transition.
        if (wp == IDT_HOVER_TIP) {
            KillTimer(hw, IDT_HOVER_TIP);
            if (st && st->hoverRow >= 0
                && st->hoverRow < (int)g_mods.size()) {
                ShowHoverTip(st->hoverRow);
            }
            return 0;
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        if (!st) return 0;
        // Tear down any pending or visible tooltip — the cursor isn't
        // over the list at all anymore.
        KillTimer(hw, IDT_HOVER_TIP);
        HideHoverTip();
        if (st->hoverRow != -1) {
            int prevHover = st->hoverRow;
            st->hoverRow = -1;
            RECT r;
            if (MLRowClientRect(hw, prevHover, &r))
                InvalidateRectL(hw, &r, FALSE);
        }
        if (st->sbHover != SbPart::None) {
            st->sbHover = SbPart::None;
            RECT rc; GetClientRectL(hw, &rc);
            SbGeom s = MLScrollbarGeom(st, rc);
            if (s.present) InvalidateRectL(hw, &s.area, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        if (!st) return 0;
        // Any click dismisses the hover tooltip — user's done looking.
        KillTimer(hw, IDT_HOVER_TIP);
        HideHoverTip();
        int x = U(GET_X_LPARAM(lp)), y = U(GET_Y_LPARAM(lp));
        RECT rc; GetClientRectL(hw, &rc);

        // Scrollbar gutter takes priority over row selection.
        SbGeom s = MLScrollbarGeom(st, rc);
        if (s.present) {
            POINT p = { x, y };
            if (PtInRect(&s.thumb, p) && s.scrollable) {
                st->sbDragging = true;
                st->sbGrabDY   = y - s.thumb.top;
                SetCapture(hw);
                InvalidateRectL(hw, &s.area, FALSE);
                return 0;
            }
            if (PtInRect(&s.up, p)) {
                st->sbPressed = SbPart::Up;
                MLScrollBy(hw, st, -(LO::ROW_H + ML::ROW_GAP));
                return 0;
            }
            if (PtInRect(&s.down, p)) {
                st->sbPressed = SbPart::Down;
                MLScrollBy(hw, st, +(LO::ROW_H + ML::ROW_GAP));
                return 0;
            }
            if (PtInRect(&s.track, p)) {
                int page = max(LO::ROW_H + ML::ROW_GAP,
                               (int)rc.bottom - (LO::ROW_H + ML::ROW_GAP));
                MLScrollBy(hw, st, (y < s.thumb.top) ? -page : +page);
                return 0;
            }
        }

        int row = MLRowAt(st, y, rc.bottom);
        if (row >= 0 && row != g_selMod)
            SendMessage(GetParent(hw), ML_NOTIFY_SELECT, (WPARAM)row, 0);
        return 0;
    }

    case WM_LBUTTONUP: {
        if (!st) return 0;
        bool wasActive = st->sbDragging || (st->sbPressed != SbPart::None);
        if (st->sbDragging) { st->sbDragging = false; ReleaseCapture(); }
        st->sbPressed = SbPart::None;
        if (wasActive) {
            RECT rc; GetClientRectL(hw, &rc);
            SbGeom s = MLScrollbarGeom(st, rc);
            if (s.present) InvalidateRectL(hw, &s.area, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        // Double-click on a mod row → launch that mod through the same
        // path as the PLAY button. The first click of the pair fires the
        // normal WM_LBUTTONDOWN above and selects the row (if it wasn't
        // already selected); by the time we get here, g_selMod is
        // already pointing at the right mod. We still re-check + fix up
        // selection defensively in case the user double-clicks faster
        // than the LBUTTONDOWN can be processed.
        //
        // Scrollbar gutter excluded — double-clicks there are nonsense
        // for a launch action and would just confuse the user.
        if (!st) return 0;
        // Game is about to launch — tear down any visible tooltip.
        KillTimer(hw, IDT_HOVER_TIP);
        HideHoverTip();
        int x = U(GET_X_LPARAM(lp)), y = U(GET_Y_LPARAM(lp));
        RECT rc; GetClientRectL(hw, &rc);
        SbGeom s = MLScrollbarGeom(st, rc);
        if (s.present) {
            POINT p = { x, y };
            if (PtInRect(&s.thumb, p) || PtInRect(&s.up, p) ||
                PtInRect(&s.down,  p) || PtInRect(&s.track, p)) return 0;
        }
        int row = MLRowAt(st, y, rc.bottom);
        if (row < 0) return 0;

        HWND parent = GetParent(hw);
        if (row != g_selMod) {
            // Defensive — should already be selected by the first
            // click's LBUTTONDOWN, but cover the case where the click
            // pair races faster than message delivery.
            SendMessage(parent, ML_NOTIFY_SELECT, (WPARAM)row, 0);
        }
        // PostMessage rather than SendMessage so this handler returns
        // before the launch path enters its WaitForInputIdle block —
        // keeps the message pump clean for the LBUTTONUP that follows
        // the dblclick.
        PostMessage(parent, WM_COMMAND,
                    MAKEWPARAM(IDC_LAUNCH_BTN, BN_CLICKED), 0);
        return 0;
    }

    case WM_RBUTTONDOWN: {
        // Right-click selects the row before the context menu pops, so
        // the menu actions operate on the row the user just clicked
        // (not whatever was selected before). The actual menu is shown
        // from WM_CONTEXTMENU below — that's the right place to hook
        // it because Windows also fires WM_CONTEXTMENU for Shift+F10
        // and the right-click happens to dispatch through it too.
        if (!st) return 0;
        // Dismiss the hover tooltip — the menu is about to take over.
        KillTimer(hw, IDT_HOVER_TIP);
        HideHoverTip();
        int x = U(GET_X_LPARAM(lp)), y = U(GET_Y_LPARAM(lp));
        RECT rc; GetClientRectL(hw, &rc);
        SbGeom s = MLScrollbarGeom(st, rc);
        if (s.present) {
            POINT p = { x, y };
            if (PtInRect(&s.thumb, p) || PtInRect(&s.up, p) ||
                PtInRect(&s.down,  p) || PtInRect(&s.track, p)) return 0;
        }
        int row = MLRowAt(st, y, rc.bottom);
        if (row >= 0 && row != g_selMod) {
            SendMessage(GetParent(hw), ML_NOTIFY_SELECT, (WPARAM)row, 0);
        }
        return 0;
    }

    case WM_CONTEXTMENU: {
        // Show the mod row context menu. lp carries SCREEN coords for
        // mouse-invoked menus, or (-1, -1) when invoked from the
        // keyboard (Shift+F10). For the keyboard case we anchor at the
        // selected row center so the menu doesn't pop in the corner of
        // the screen.
        if (!st) return 0;
        if (g_selMod < 0 || g_selMod >= (int)g_mods.size()) return 0;

        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (pt.x == -1 && pt.y == -1) {
            RECT cr; GetClientRect(hw, &cr);
            pt.x = (cr.left + cr.right) / 2;
            pt.y = (cr.top  + cr.bottom) / 2;
            ClientToScreen(hw, &pt);
        }

        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING,
                    IDM_MOD_OPEN_FOLDER,  L"Open Folder in Explorer");
        AppendMenuW(menu, MF_STRING,
                    IDM_MOD_BACKUP_SAVES, L"Backup Saves");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING,
                    IDM_MOD_REZIP,        L"Re-zip Mod...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING,
                    IDM_MOD_UNINSTALL,    L"Uninstall...");

        // Route commands to MainProc (parent) so its WM_COMMAND
        // dispatcher handles them alongside other commands.
        TrackPopupMenu(menu,
                       TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                       pt.x, pt.y, 0, GetParent(hw), nullptr);
        DestroyMenu(menu);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (!st) return 0;
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        st->scrollY -= (delta / WHEEL_DELTA) * (LO::ROW_H + ML::ROW_GAP) / 2;
        RECT rc; GetClientRectL(hw, &rc);
        MLClampScroll(st, rc.bottom);
        InvalidateRect(hw, nullptr, FALSE);
        // Scrolling shuffles which row is under the cursor — the
        // tooltip's content would now be stale, and the geometry it
        // was positioned against has moved. Easiest fix: tear it down.
        // The hover transition logic in WM_MOUSEMOVE re-arms a fresh
        // timer if the new row under cursor warrants it.
        KillTimer(hw, IDT_HOVER_TIP);
        HideHoverTip();
        return 0;
    }
    }
    return DefWindowProc(hw, msg, wp, lp);
}

void RegisterModListClass(HINSTANCE hInst) {
    WNDCLASS wc = {};
    wc.style         = CS_DBLCLKS;   // enable WM_LBUTTONDBLCLK delivery
    wc.lpfnWndProc   = ModListProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = MOD_LIST_CLASS;
    RegisterClass(&wc);
}
