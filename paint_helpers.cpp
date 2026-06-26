// ═══════════════════════════════════════════════════════════════════════
//  paint_helpers.cpp — see paint_helpers.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "paint_helpers.h"
#include "scaling.h"     // SF, S
#include "colors.h"      // Tok::* (Color tokens)
#include "assets.h"      // AssetImage, DrawAssetAt
#include "layout.h"      // LO::ORNAMENT_TOP_Y, LO::CORNER_ACCENT_INSET_*
#include "fonts.h"       // not currently used but kept for parity with other paint TUs

void FillSolid(HDC hdc, int x, int y, int w, int h, COLORREF c) {
    RECT r = { x, y, x + w, y + h };
    HBRUSH br = CreateSolidBrush(c);
    FillRect(hdc, &r, br);
    DeleteObject(br);
}

// Draw text with shadow + glow (spec §3 — gold text glow pattern)
void DrawGoldText(Graphics& g, const wstring& s, Font* fnt,
                         RectF rect, StringFormat* sf,
                         Color main, bool glow) {
    if (glow) {
        // Atmospheric drop
        SolidBrush shadow(GPA(180, 0, 0, 0));
        RectF sr = rect; sr.Y += 2; sr.X += 0;
        g.DrawString(s.c_str(), -1, fnt, sr, sf, &shadow);
    }
    SolidBrush mb(main);
    g.DrawString(s.c_str(), -1, fnt, rect, sf, &mb);
}

// ── Custom checkbox glyph (rune-X style) ─────────────────────────────
//
// Drawn directly into Graphics. State:
//   checked + isLocked → checkbox_locked.png  (asset's yellow-X variant)
//   checked            → checkbox_checked.png (asset's X-mark)
//   unchecked          → checkbox.png         (asset's empty box)
//
// Falls back to a programmatic gold-and-X rendering when the assets
// aren't available, so the launcher still functions visually with no art.
void DrawFlagCheckbox(Graphics& g, REAL x, REAL y, bool checked,
                             bool hover, bool isLocked) {
    constexpr int CB_SIZE = 27;     // matches asset native size (27×28)
    REAL S  = (REAL)CB_SIZE;
    REAL Sh = 28.0f;                // height per asset

    // Asset path
    const wchar_t* assetName = checked
        ? (isLocked ? L"checkbox_locked.png" : L"checkbox_checked.png")
        : L"checkbox.png";
    Gdiplus::Bitmap* bm = AssetImage(assetName);
    if (bm) {
        InterpolationMode prev = g.GetInterpolationMode();
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(bm, (int)x, (int)y, (int)S, (int)Sh);
        g.SetInterpolationMode(prev);
        // Hover highlight: a faint gold outline overlay on top of the asset
        if (hover) {
            Pen out(GPA(140, 0xE8, 0xC2, 0x5E), 1.5f);
            g.DrawRectangle(&out, x, y, S - 1, Sh - 1);
        }
        return;
    }

    // ── Programmatic fallback (original behavior) ───────────────────────
    if (checked) {
        SolidBrush base(Tok::GoldDeep);
        g.FillRectangle(&base, x, y, S, S);
        LinearGradientBrush hl(PointF(x, y), PointF(x, y + S),
            GP(0xC8, 0xA8, 0x4B), GP(0x6B, 0x4F, 0x1A));
        g.FillRectangle(&hl, x + 1, y + 1, S - 2, S - 2);
        Pen out(hover ? Tok::GoldBright : Tok::Gold, 1.5f);
        g.DrawRectangle(&out, x, y, S - 1, S - 1);
        Pen glyph(GP(0x2A, 0x1A, 0x0A), 2.0f);
        glyph.SetStartCap(LineCapRound);
        glyph.SetEndCap(LineCapRound);
        REAL pad = 4.0f;
        g.DrawLine(&glyph, x + pad,       y + pad,       x + S - pad, y + S - pad);
        g.DrawLine(&glyph, x + S - pad,   y + pad,       x + pad,     y + S - pad);
    } else {
        SolidBrush bg(Tok::BgPanel2);
        g.FillRectangle(&bg, x + 1, y + 1, S - 2, S - 2);
        Pen out(hover ? Tok::Gold : Tok::GoldDim, 1.0f);
        g.DrawRectangle(&out, x, y, S - 1, S - 1);
    }
}

void OPDrawBtnFrame(Graphics& g, int x, int y, int w, int h, bool hot) {
    SolidBrush bg(Tok::BgPanel);
    g.FillRectangle(&bg, x + 1, y + 1, w - 2, h - 2);
    Pen border(hot ? Tok::GoldBright : Tok::Bronze, 1.0f);
    g.DrawRectangle(&border, x, y, w - 1, h - 1);
}

void PaintTopOrnament(Graphics& g, int W) {
    const int topY = LO::ORNAMENT_TOP_Y;

    // ── Asset path: drop the real PNG in at native size, centered ────────
    if (Gdiplus::Bitmap* orn = AssetImage(L"ornament_gem.png")) {
        int aw = (int)orn->GetWidth();
        DrawAssetAt(g, orn, (W - aw) / 2, topY);
        return;
    }

    // ── Programmatic fallback ────────────────────────────────────────────
    // Nominal footprint roughly matching the asset, centered on W/2.
    const REAL ow = 150.0f, oh = 86.0f;
    const REAL ox = (W - ow) * 0.5f;
    const REAL oy = (REAL)topY;
    const REAL cx = ox + ow * 0.5f;
    const REAL cy = oy + oh * 0.5f;

    SmoothingMode prevSmooth = g.GetSmoothingMode();
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    // Bronze setting: a wide horizontal lozenge behind the gem.
    {
        GraphicsPath lozenge;
        PointF pts[4] = {
            PointF(cx,            oy + 6.0f),       // top
            PointF(ox + ow - 6,   cy),              // right
            PointF(cx,            oy + oh - 6),     // bottom
            PointF(ox + 6,        cy),              // left
        };
        lozenge.AddPolygon(pts, 4);
        LinearGradientBrush bronze(
            RectF(ox, oy, ow, oh),
            Tok::BronzeBright, Tok::BronzeDim, LinearGradientModeVertical);
        g.FillPath(&bronze, &lozenge);
        Pen goldEdge(Tok::Gold, 2.0f);
        g.DrawPath(&goldEdge, &lozenge);
    }

    // Filigree wings: short tapering strokes flanking the setting.
    {
        Pen wing(Tok::BronzeBright, 3.0f);
        wing.SetStartCap(LineCapRound);
        wing.SetEndCap(LineCapRound);
        g.DrawLine(&wing, ox + 6,      cy, ox - 30,        cy - 10);
        g.DrawLine(&wing, ox + ow - 6, cy, ox + ow + 30,   cy - 10);
    }

    // Sapphire: a smaller diamond with a vertical blue gradient, gold rim,
    // and a specular highlight near the top-left facet.
    {
        const REAL gw = 30.0f, gh = 40.0f;
        GraphicsPath gem;
        PointF gp[4] = {
            PointF(cx,             cy - gh * 0.5f),
            PointF(cx + gw * 0.5f, cy),
            PointF(cx,             cy + gh * 0.5f),
            PointF(cx - gw * 0.5f, cy),
        };
        gem.AddPolygon(gp, 4);
        LinearGradientBrush blue(
            RectF(cx - gw, cy - gh, gw * 2, gh * 2),
            GP(0x6C, 0xA8, 0xFF), GP(0x10, 0x2A, 0x78),
            LinearGradientModeVertical);
        g.FillPath(&blue, &gem);
        Pen rim(Tok::GoldBright, 1.5f);
        g.DrawPath(&rim, &gem);

        GraphicsPath spec;
        PointF sp[3] = {
            PointF(cx,              cy - gh * 0.5f + 3),
            PointF(cx - gw * 0.28f, cy - 2),
            PointF(cx,              cy - 2),
        };
        spec.AddPolygon(sp, 3);
        SolidBrush hi(GPA(150, 0xFF, 0xFF, 0xFF));
        g.FillPath(&hi, &spec);
    }

    g.SetSmoothingMode(prevSmooth);
}

void PaintCornerAccents(Graphics& g, int W) {
    using namespace LO;
    int frameH = 1024;
    if (Gdiplus::Bitmap* fm = AssetImage(L"frame_main.png"))
        frameH = (int)fm->GetHeight();

    // Anchor to the inner edge of frame_main.png's border filigree so each
    // bracket nestles into the content-opening corner (right inside the
    // border, not on the absolute window corner). CORNER_ACCENT_INSET_* is an
    // additional nudge from that edge: positive pushes further into the
    // content, negative pulls back toward / onto the filigree.
    //
    // MeasureFrameInset keys off scan-line density. The top/bottom borders are
    // solid horizontal bands and measure correctly, but the left/right borders
    // are sparser vertical filigree that falls under the density threshold, so
    // it returns ~0 for left/right. The border is ~uniform thickness, so fall
    // back to the (reliable) vertical measurement when horizontal degenerates.
    FrameInset fi = MeasureFrameInset(L"frame_main.png");
    int hL = (fi.left  >= 8) ? fi.left  : fi.top;
    int hR = (fi.right >= 8) ? fi.right : fi.bottom;
    int li = hL        + CORNER_ACCENT_INSET_X;   // left   inner edge
    int ri = hR        + CORNER_ACCENT_INSET_X;   // right  inner edge
    int ti = fi.top    + CORNER_ACCENT_INSET_Y;   // top    inner edge
    int bi = fi.bottom + CORNER_ACCENT_INSET_Y;   // bottom inner edge

    struct Corner { const wchar_t* name; bool right; bool bottom; };
    const Corner corners[4] = {
        { L"corner_tl.png", false, false },
        { L"corner_tr.png", true,  false },
        { L"corner_bl.png", false, true  },
        { L"corner_br.png", true,  true  },
    };

    SmoothingMode prevSmooth = g.GetSmoothingMode();
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    for (const Corner& c : corners) {
        Gdiplus::Bitmap* bm = AssetImage(c.name);
        if (bm) {
            int bw = (int)bm->GetWidth();
            int bh = (int)bm->GetHeight();
            int x = c.right  ? (W - bw - ri)      : li;
            int y = c.bottom ? (frameH - bh - bi) : ti;
            DrawAssetAt(g, bm, x, y);
        } else {
            // Minimal fallback: a short gold L hugging the corner, with a
            // bronze diagonal accent — reads as a bracket without pretending
            // to reproduce the filigree.
            const REAL len = 44.0f, off = 8.0f;
            REAL px = c.right  ? (REAL)(W - ri - off)      : (REAL)(li + off);
            REAL py = c.bottom ? (REAL)(frameH - bi - off) : (REAL)(ti + off);
            REAL dx = c.right  ? -1.0f : 1.0f;
            REAL dy = c.bottom ? -1.0f : 1.0f;
            Pen gold(Tok::Gold, 2.0f);
            gold.SetStartCap(LineCapRound);
            gold.SetEndCap(LineCapRound);
            g.DrawLine(&gold, px, py, px + dx * len, py);
            g.DrawLine(&gold, px, py, px, py + dy * len);
            Pen bronze(Tok::BronzeBright, 1.5f);
            g.DrawLine(&bronze, px + dx * 4, py + dy * 4,
                       px + dx * (len * 0.5f), py + dy * (len * 0.5f));
        }
    }

    g.SetSmoothingMode(prevSmooth);
}
