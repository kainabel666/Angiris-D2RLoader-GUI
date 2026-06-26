// ═══════════════════════════════════════════════════════════════════════
//  buttons.cpp — see buttons.h for the public interface
// ═══════════════════════════════════════════════════════════════════════
//
//  Implements the owner-drawn button infrastructure:
//
//    • ButtonState map keyed by HWND (kind, hover, tracking, dirty)
//    • Hover subclass that flips state on WM_MOUSEMOVE/LEAVE
//    • Per-kind transform + asset lookup tables
//    • Button factory MkStdBtn + RegisterButton helper
//    • Paint routine PaintOwnerDrawButton called from WM_DRAWITEM
//
//  Both MainProc and the dialog procs route their WM_DRAWITEM
//  through PaintOwnerDrawButton so all buttons share the same
//  visual treatment regardless of which window they live on.
//
// ═══════════════════════════════════════════════════════════════════════

#include "buttons.h"
#include "core.h"        // g_hInst, g_bottomExpanded
#include "scaling.h"     // S, SF, U
#include "colors.h"      // Tok::Gold, etc.
#include "assets.h"      // AssetImage, DrawButton9Slice
#include "fonts.h"       // g_fNav, g_fBtn, etc.
#include "layout.h"      // LO::BTN_OVERFLOW_PAD
#include "paint_helpers.h"  // FillSolid (backdrop fallback), OPDrawBtnFrame (no-asset fallback)

// Per-kind visual response to hover / click. Applied as a GDI+ transform
// around the button's center so the asset AND label move/scale together.
struct ButtonStateTransform {
    float scaleHover;     // multiplier when mouse is over (and not pressed)
    float scaleClick;     // multiplier when pressed
    float offsetXClick;   // pixels to shift right when pressed (down-right tactile)
    float offsetYClick;
};

// Per-kind transform table. Tuned so each kind's animation feels right at
// its native size — bigger buttons grow/shrink less to keep movement subtle,
// smaller buttons can use bolder scale factors.
static ButtonStateTransform StateTransformFor(ButtonKind k) {
    switch (k) {
    case ButtonKind::Nav:         return { 1.06f, 0.92f, 2.0f, 2.0f };
    case ButtonKind::NavSm:       return { 1.06f, 0.92f, 2.0f, 2.0f };
    case ButtonKind::Refresh:     return { 1.00f, 0.94f, 1.0f, 1.0f };   // no hover-grow; click-shrink only
    case ButtonKind::NexusUpdate:    return { 1.05f, 0.93f, 1.0f, 1.0f };
    case ButtonKind::ModLink:        return { 1.00f, 0.93f, 1.0f, 1.0f };   // legacy
    case ButtonKind::ModLinkDocs:    return { 1.00f, 0.93f, 1.0f, 1.0f };   // no hover-grow; click-shrink
    case ButtonKind::ModLinkDiscord: return { 1.00f, 0.93f, 1.0f, 1.0f };
    case ButtonKind::ModLinkWebsite: return { 1.00f, 0.93f, 1.0f, 1.0f };
    case ButtonKind::Play:        return { 1.03f, 0.95f, 2.0f, 2.0f };
    case ButtonKind::Ellipse:     return { 1.08f, 0.90f, 1.0f, 1.0f };
    case ButtonKind::Arrow:       return { 1.06f, 0.92f, 0.0f, 2.0f };
    case ButtonKind::Plugins:     return { 1.00f, 0.93f, 1.0f, 1.0f };   // no hover-grow; click-shrink only
    }
    return { 1.0f, 1.0f, 0.0f, 0.0f };
}

// Asset filename for a given kind (single asset per kind — no state suffix).
static const wchar_t* AssetNameFor(ButtonKind k) {
    switch (k) {
    case ButtonKind::Nav:         return L"btn_nav.png";
    case ButtonKind::NavSm:       return L"btn_nav.png";
    case ButtonKind::Refresh:     return L"btn_refresh.png";
    case ButtonKind::NexusUpdate:    return L"btn_nexus_update.png";
    case ButtonKind::ModLink:        return L"btn_nexus_update.png";   // legacy
    case ButtonKind::ModLinkDocs:    return L"btn_docs.png";
    case ButtonKind::ModLinkDiscord: return L"btn_discord.png";
    case ButtonKind::ModLinkWebsite: return L"btn_website.png";
    case ButtonKind::Play:        return L"btn_play.png";
    case ButtonKind::Ellipse:     return L"btn_ellipse.png";
    case ButtonKind::Arrow:       return L"btn_expand_arrow.png";
    case ButtonKind::Plugins:     return L"btn_nexus_update.png";   // reuse the Nexus/Update frame
    }
    return nullptr;
}

struct ButtonState {
    ButtonKind kind     = ButtonKind::Nav;
    bool       hover    = false;     // mouse currently over this button
    bool       tracking = false;     // we've called TrackMouseEvent already
    bool       dirty    = false;     // refresh-style "pending changes" highlight
};
static map<HWND, ButtonState> g_btnStates;

// Subclass proc for owner-drawn buttons. Tracks mouse hover state so the
// shared WM_DRAWITEM handler can swap idle↔hover assets cleanly. Without
// this, Win32 button controls swallow mouse messages internally and we'd
// have to poll cursor position on every paint (which doesn't trigger
// repaints on hover-in/hover-out).
static LRESULT CALLBACK BtnHoverSubclass(HWND hw, UINT msg,
                                        WPARAM wp, LPARAM lp,
                                        UINT_PTR /*id*/, DWORD_PTR /*ref*/) {
    // Suppress the system's default WM_ERASEBKGND for owner-draw buttons.
    // Without this, the BUTTON class fills the client area with
    // COLOR_BTNFACE (a light gray, looks near-white over our dark stone)
    // BEFORE WM_DRAWITEM fires, which paints a brief flash whenever a
    // hidden button becomes visible (expansion panel opens, mod-link
    // buttons appear for a freshly selected mod, etc.). WM_DRAWITEM
    // repaints the entire rect anyway, so the erase is redundant.
    if (msg == WM_ERASEBKGND) return 1;

    auto it = g_btnStates.find(hw);
    if (it != g_btnStates.end()) {
        ButtonState& st = it->second;
        // Skip the hover-driven invalidate when the kind has no hover
        // visual change (scaleHover == 1.0). Otherwise WM_MOUSEMOVE /
        // WM_MOUSELEAVE schedule a full WM_PAINT just to redraw an
        // identical frame, and on heavy-asset buttons that paint shows
        // up as visible jitter. Click-driven repaints still happen
        // through the normal BS_OWNERDRAW pathway (Windows sends
        // WM_DRAWITEM with ODS_SELECTED), so the shrink-on-click
        // behavior is unaffected.
        bool hoverHasVisual = (StateTransformFor(st.kind).scaleHover != 1.0f);
        if (msg == WM_MOUSEMOVE) {
            if (!st.hover) {
                st.hover = true;
                if (hoverHasVisual) InvalidateRect(hw, nullptr, FALSE);
            }
            if (!st.tracking) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hw, 0 };
                TrackMouseEvent(&tme);
                st.tracking = true;
            }
        }
        else if (msg == WM_MOUSELEAVE) {
            st.hover = false;
            st.tracking = false;
            if (hoverHasVisual) InvalidateRect(hw, nullptr, FALSE);
        }
        else if (msg == WM_NCDESTROY) {
            g_btnStates.erase(it);
            RemoveWindowSubclass(hw, BtnHoverSubclass, 1);
        }
    }
    return DefSubclassProc(hw, msg, wp, lp);
}

// Register an HWND as an owner-drawn button of the given kind. Called once
// per button at creation time. WM_DRAWITEM dispatches via this map; the
// subclass added here keeps the hover state in sync.
void RegisterButton(HWND hw, ButtonKind kind) {
    if (!hw) return;
    g_btnStates[hw] = ButtonState{ kind, false, false };
    SetWindowSubclass(hw, BtnHoverSubclass, 1, 0);
}

// Small helper for standard Win32 buttons (used by left rail nav + the
// Mod Description's Discord/Docs/Website link buttons).
// Creates a push-button as BS_OWNERDRAW so all buttons paint through the
// shared WM_DRAWITEM handler. The optional `kind` controls which asset
// family will paint it (Nav / Refresh / NexusUpdate / Play / Arrow). The
// label text rendering still happens in WM_DRAWITEM, except for kinds
// whose art has the label baked in (Refresh).
HWND MkStdBtn(HWND parent, const wchar_t* lbl, int id,
                     int x, int y, int w, int h, bool visible,
                     ButtonKind kind) {
    DWORD style = WS_CHILD | BS_PUSHBUTTON | BS_OWNERDRAW;
    if (visible) style |= WS_VISIBLE;
    HWND hw = CreateWindow(L"BUTTON", lbl, style,
        x, y, w, h, parent, (HMENU)(UINT_PTR)id, g_hInst, nullptr);
    RegisterButton(hw, kind);
    return hw;
}

bool PaintOwnerDrawButton(DRAWITEMSTRUCT* d) {
    if (d->CtlType != ODT_BUTTON) return false;
    HWND ctl = d->hwndItem;
    auto it = g_btnStates.find(ctl);
    if (it == g_btnStates.end()) return false;
    const ButtonState& st = it->second;

    int W = d->rcItem.right  - d->rcItem.left;
    int H = d->rcItem.bottom - d->rcItem.top;
    bool pressed  = (d->itemState & ODS_SELECTED) != 0;
    bool disabled = (d->itemState & ODS_DISABLED) != 0;
    bool hover    = st.hover;
    // Refresh button has a special "stale" highlight when g_modsDirty
    // — keep the existing behavior.
    bool dirty    = st.dirty;   // set by host via SetButtonDirty

    // The HWND is enlarged by BTN_OVERFLOW_PAD on each side (set up in
    // Layout()) so hover scale-ups have room to render. The VISIBLE art
    // occupies a centered rect within the HWND; everything (backdrop
    // composite, asset draw, label draw, transform center) is anchored
    // to this art rect, not the full HWND rect.
    //
    // EXCEPTIONS — these kinds have hover-grow disabled and their HWND
    // is sized exactly to the visible art, so the art rect IS the HWND
    // rect. Insetting by P here would draw the art at (HWND − 2·P),
    // which squashes the asset (e.g. 138×52 → 114×28 for Refresh).
    int P = LO::BTN_OVERFLOW_PAD;
    if (st.kind == ButtonKind::Ellipse        ||
        st.kind == ButtonKind::Arrow          ||
        st.kind == ButtonKind::Refresh        ||
        st.kind == ButtonKind::ModLink        ||
        st.kind == ButtonKind::ModLinkDocs    ||
        st.kind == ButtonKind::ModLinkDiscord ||
        st.kind == ButtonKind::ModLinkWebsite)
        P = 0;
    int artX = d->rcItem.left + P;
    int artY = d->rcItem.top  + P;
    int artW = W - 2 * P;
    int artH = H - 2 * P;

    // Legacy expansion-panel detection. NavSm was the kind originally
    // used for the bottom expansion panel buttons; the active code now
    // uses NexusUpdate for those, so this flag is effectively always
    // false. Kept so the NavSm path still works if a future build
    // re-registers any buttons as NavSm (e.g. for 9-slice compression
    // or the smaller g_fNavSm label font).
    bool inExpansion = (st.kind == ButtonKind::NavSm);

    // ── Asset path: choose the asset name for this kind/state ───────
    // ── Asset lookup ────────────────────────────────────────────────
    // One asset per kind; the state (idle/hover/click) is reproduced
    // via a GDI+ transform applied at paint time so both the art and
    // the label move/scale together.
    const wchar_t* assetName = AssetNameFor(st.kind);
    Gdiplus::Bitmap* assetBM = assetName ? AssetImage(assetName) : nullptr;

    // ModLink* fallback: when the per-button square asset (btn_docs,
    // btn_discord, btn_website) is missing, route to the shared
    // btn_nexus_update.png and 9-slice it to the button's square
    // dimensions. The single-letter label (D/X/W) then renders on
    // top because the primary asset didn't draw (skipLabel logic
    // below leaves the label visible in this case).
    bool modLinkFallback = false;
    if (!assetBM && (st.kind == ButtonKind::ModLinkDocs
              || st.kind == ButtonKind::ModLinkDiscord
              || st.kind == ButtonKind::ModLinkWebsite)) {
        assetBM = AssetImage(L"btn_nexus_update.png");
        modLinkFallback = (assetBM != nullptr);
    }

    // ── Backdrop ────────────────────────────────────────────────────
    // When the styled asset is present, composite the parent window's
    // background (stone + frame) into the button rect first so the
    // asset's transparent edges show what's behind. When the asset
    // is missing, fall back to a solid fill so OPDrawBtnFrame reads.
    Graphics g(d->hDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    if (assetBM) {
        // Composite parent's stone into our rect, mapped by where we
        // sit in the parent. Buttons in the expansion area (y > 1024
        // logical) are backed by bg_expand.png; everything else by
        // bg_stone.png.
        //
        // Two coordinate systems are in play. MapWindowPoints returns
        // PHYSICAL pixels (Win32 doesn't see g_scale), but the parent
        // paints bg_stone/bg_expand under a ScaleTransform so their
        // native pixels correspond to LOGICAL parent coords. To pick
        // the right source pixels we convert the parent position to
        // logical via U(), and we ask for U(W) × U(H) source pixels
        // (which then get stretched back up to W × H physical when
        // drawn onto our physical-pixel button surface — that stretch
        // factor is g_scale, matching the parent's visual scaling).
        POINT origin = { 0, 0 };
        MapWindowPoints(ctl, GetParent(ctl), &origin, 1);
        int parentX = origin.x + d->rcItem.left;
        int parentY = origin.y + d->rcItem.top;
        int lParentX = U(parentX);
        int lParentY = U(parentY);
        int lW = U(W);
        int lH = U(H);

        int collapsedH = 1024;
        if (Gdiplus::Bitmap* fm = AssetImage(L"frame_main.png"))
        collapsedH = (int)fm->GetHeight();

        const wchar_t* bgName = (lParentY >= collapsedH)
        ? L"bg_expand.png"
        : L"bg_stone.png";
        int srcOffsetY = (lParentY >= collapsedH)
        ? (lParentY - collapsedH)     // bg_expand starts at y=collapsedH (logical)
        : lParentY;

        if (Gdiplus::Bitmap* bg = AssetImage(bgName)) {
        g.DrawImage(bg,
            Rect(d->rcItem.left, d->rcItem.top, W, H),
            (REAL)lParentX, (REAL)srcOffsetY, (REAL)lW, (REAL)lH,
            UnitPixel);
        } else {
        // No background asset available — solid fill fallback
        FillSolid(d->hDC, d->rcItem.left, d->rcItem.top, W, H,
                  Tok::crBgDeep);
        }
        // Note: we deliberately skip painting frame_main.png on top
        // of the stone here. The button rects sit inside the frame's
        // interior so the frame filigree wouldn't intersect them
        // anyway in the typical case.
    } else {
        // Programmatic fallback path — solid fill so OPDrawBtnFrame
        // is clearly visible.
        FillSolid(d->hDC, d->rcItem.left, d->rcItem.top, W, H,
              Tok::crBgDeep);
    }

    // ── Asset + label, drawn under a shared state transform ─────────
    // Both the asset image AND its label text scale/offset together,
    // so the button reads as one piece (instead of the art moving
    // independently from the text). The backdrop composite above
    // is already done at the full HWND rect — it stays unaffected
    // by the transform, so the stone fills the entire HWND cleanly
    // even when the foreground shrinks on click.
    //
    // Everything below operates on the ART rect (centered inset), so
    // the visible button stays in the right position regardless of
    // the overflow padding.
    bool drewAsset = false;
    ButtonStateTransform xf = StateTransformFor(st.kind);
    bool applyTransform = (hover || pressed) && assetBM;

    Gdiplus::Matrix prevMatrix;
    if (applyTransform) {
        g.GetTransform(&prevMatrix);
        REAL cx = (REAL)(artX + artW * 0.5f);
        REAL cy = (REAL)(artY + artH * 0.5f);
        REAL scale = pressed ? xf.scaleClick : xf.scaleHover;
        REAL dx    = pressed ? xf.offsetXClick : 0.0f;
        REAL dy    = pressed ? xf.offsetYClick : 0.0f;
        // Translate to center, scale, translate back — offset by the
        // press shift so the whole button moves down-right when pressed.
        g.TranslateTransform(cx + dx, cy + dy);
        g.ScaleTransform(scale, scale);
        g.TranslateTransform(-cx, -cy);
    }

    // Draw the asset image (or the programmatic fallback frame).
    if (assetBM) {
        // Nav buttons in the BOTTOM EXPANSION PANEL (inExpansion,
        // computed earlier) render shorter (58px vs 76px native) to
        // fit inside the 300px expansion frame. Straight DrawImage
        // would squash the gem corners, so we route them through
        // DrawButton9Slice — the corners stay pixel-perfect and only
        // the middle compresses.

        // Arrow asset is rotated 180° when the bottom panel is expanded
        // so the chevron direction matches "click to collapse" vs "click
        // to expand". The rotation composes with the state transform.
        if (st.kind == ButtonKind::Arrow && g_bottomExpanded) {
        Gdiplus::Matrix beforeRotate;
        g.GetTransform(&beforeRotate);
        REAL cx = (REAL)(artX + artW * 0.5f);
        REAL cy = (REAL)(artY + artH * 0.5f);
        g.TranslateTransform(cx, cy);
        g.RotateTransform(180.0f);
        g.TranslateTransform(-cx, -cy);
        InterpolationMode prevIM = g.GetInterpolationMode();
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(assetBM, artX, artY, artW, artH);
        g.SetInterpolationMode(prevIM);
        g.SetTransform(&beforeRotate);
        } else if (inExpansion) {
        // 9-slice with 14px corners (matches the gem corner art).
        DrawButton9Slice(g, assetBM, artX, artY, artW, artH, 14);
        } else if (modLinkFallback) {
        // Square 85×85 button reusing the 254×54 nexus/update art.
        // The art's corner ornaments are ~14px; 9-slicing with that
        // inset keeps them pixel-perfect while the middle stretches
        // both horizontally and vertically to fill the square.
        DrawButton9Slice(g, assetBM, artX, artY, artW, artH, 14);
        } else {
        InterpolationMode prevIM = g.GetInterpolationMode();
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(assetBM, artX, artY, artW, artH);
        g.SetInterpolationMode(prevIM);
        }
        drewAsset = true;
    } else {
        // Programmatic fallback (current look). Doesn't get the
        // hover/click transform — the state is conveyed by the
        // OPDrawBtnFrame "highlight" parameter instead.
        if (applyTransform) g.SetTransform(&prevMatrix);
        applyTransform = false;
        bool highlight = hover || pressed || dirty;
        OPDrawBtnFrame(g, artX, artY, artW, artH, highlight);
    }

    // ── Label text ──────────────────────────────────────────────────
    // Some kinds have their label baked into the art (Refresh's text,
    // Ellipse's "...", the chevron direction in Arrow). Skip live text
    // rendering only when the asset actually rendered — if the asset
    // is missing we still draw the fallback caption (▼/▲, "Refresh",
    // "...") so the button stays identifiable.
    //
    // The three ModLink* kinds (Docs/Discord/Website) work the
    // opposite way: their per-button assets carry the icon directly
    // and the HWND's text is just a single-letter ID for the
    // fallback path. So we suppress the label when the per-button
    // asset drew, and SHOW it when the fallback 9-sliced
    // btn_nexus_update was used instead.
    bool isModLinkKind = (st.kind == ButtonKind::ModLinkDocs
                   || st.kind == ButtonKind::ModLinkDiscord
                   || st.kind == ButtonKind::ModLinkWebsite);
    bool skipLabel = drewAsset && (st.kind == ButtonKind::Refresh
                            || st.kind == ButtonKind::Ellipse
                            || st.kind == ButtonKind::Arrow
                            || (isModLinkKind && !modLinkFallback));
    if (!skipLabel) {
        wchar_t label[128] = {};
        GetWindowText(ctl, label, 127);

        StringFormat sfC;
        sfC.SetAlignment(StringAlignmentCenter);
        sfC.SetLineAlignment(StringAlignmentCenter);
        bool highlight = hover || pressed || dirty;
        SolidBrush lbl(disabled ? Tok::TextDim
                            : (highlight ? Tok::GoldBright : Tok::Gold));
        Font* labelFont = g_fBtn;
        if      (st.kind == ButtonKind::Play)        labelFont = g_fBtnLaunch;
        else if (inExpansion)                        labelFont = g_fNavSm;
        else if (st.kind == ButtonKind::Nav)         labelFont = g_fNav;
        else if (st.kind == ButtonKind::NexusUpdate) labelFont = g_fNavSm;   // +40% over g_fBtn
        else if (isModLinkKind)                      labelFont = g_fBtnLaunch;   // single-letter fallback — go big

        // Auto-shrink: if the label is wider than the button's interior,
        // step down to a smaller font. This handles long expansion-panel
        // labels (e.g. "AFJ Pro Text Editor") in 310px-wide Nav buttons
        // without manual per-kind sizing.
        const REAL labelPad = 24.0f;       // horizontal padding inside button
        RectF measureRect;
        g.MeasureString(label, -1, labelFont,
                    RectF(0, 0, 4096, 4096), &sfC, &measureRect);
        if (measureRect.Width > (REAL)artW - labelPad) {
        // Try a smaller font: Nav 26 → 20, Play 20 → 16, else 13 → no change
        if (labelFont == g_fNav)              labelFont = g_fBtnLaunch;  // 20px
        else if (labelFont == g_fBtnLaunch)   labelFont = g_fBtn;        // 13px
        }
        // Second pass — if still too wide, last resort to g_fBtn.
        g.MeasureString(label, -1, labelFont,
                    RectF(0, 0, 4096, 4096), &sfC, &measureRect);
        if (measureRect.Width > (REAL)artW - labelPad && labelFont != g_fBtn) {
        labelFont = g_fBtn;
        }

        // Label renders centered in the art rect (not the full HWND
        // rect), under the same transform as the asset.
        g.DrawString(label, -1, labelFont,
                 RectF((REAL)artX, (REAL)artY,
                       (REAL)artW, (REAL)artH),
                 &sfC, &lbl);
    }

    // Restore the prior transform (if we set one).
    if (applyTransform) g.SetTransform(&prevMatrix);
    return true;
}

void SetButtonDirty(HWND hw, bool dirty) {
    auto it = g_btnStates.find(hw);
    if (it == g_btnStates.end()) return;
    if (it->second.dirty == dirty) return;       // no-op if unchanged
    it->second.dirty = dirty;
    InvalidateRect(hw, nullptr, FALSE);
}
