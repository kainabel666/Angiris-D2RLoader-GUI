// ═══════════════════════════════════════════════════════════════════════
//  loader_options_modal.cpp — Basic Options modal
// ═══════════════════════════════════════════════════════════════════════
//
//  Themed popup that exposes the six Basic settings D2RLoader reads at
//  launch. Rows are a mix of two-state toggles (booleans) and int
//  dropdowns. Each change writes immediately to D2RLoader.toml through
//  the SaveTomlBool / SaveTomlInt helpers exported by Angiris.cpp.
//
//  Paint approach mirrors the plugin manager popup — stone bg + ornate
//  frame + owner-drawn Close button — but the row painting is inline
//  here (no listbox) because six fixed rows don't warrant a scrollable
//  control.

#include "loader_options_modal.h"
#include "core.h"          // g_hInst, g_dpiScale
#include "config.h"        // g_cfg (for LoaderTomlPath via extern below)
#include "scaling.h"       // S(), SF()
#include "colors.h"        // Tok::Gold, Tok::BgPanel, etc.
#include "fonts.h"         // g_fBtn, g_fNavSm, g_fModName
#include "assets.h"        // AssetImage, DrawButton9Slice
#include "buttons.h"       // MkStdBtn, PaintOwnerDrawButton, ButtonKind
#include "ui_state.h"      // g_loaderOpts

#include <cstdlib>         // _wtoi

// Toml write helpers live in Angiris.cpp (which owns g_cfg). Declaring
// them extern here keeps this file free of a shared header.
extern void SaveTomlBool(const wchar_t* section, const wchar_t* key, bool v);
extern void SaveTomlInt (const wchar_t* section, const wchar_t* key, int  v);

using namespace Gdiplus;

// ─────────────────────────────────────────────────────────────────────
//  Layout constants (logical pixels)
// ─────────────────────────────────────────────────────────────────────

constexpr int BO_W                 = 400;   // was 440 — trimmed unused width
constexpr int BO_TITLE_H           = 32;    // was 40
constexpr int BO_TITLE_TOP_PAD     = 8;     // was 12
constexpr int BO_TITLE_BOT_PAD     = 4;     // was 8
constexpr int BO_ROW_H             = 32;
constexpr int BO_ROW_H_TALL        = 44;    // was 48 — rows with `helper` text
constexpr int BO_ROW_LABEL_INSET_L = 20;
constexpr int BO_ROW_VALUE_INSET_R = 20;    // was 40 — controls sit closer to right edge
constexpr int BO_ROW_VALUE_BOX_W   = 70;
constexpr int BO_VALUE_BOX_H       = 28;    // fixed; vertically centered
constexpr int BO_SLIDER_W          = 53;
constexpr int BO_SLIDER_H          = 23;
constexpr int BO_ROW_TO_BTN_GAP    = 12;    // was 20
constexpr int BO_BTN_W             = 140;
constexpr int BO_BTN_H             = 50;
constexpr int BO_BTN_BOTTOM_PAD    = 12;    // was 16

// Item dimensions inside the int-dropdown popup menu (logical pixels).
constexpr int BO_MENU_ITEM_W = 80;
constexpr int BO_MENU_ITEM_H = 28;

// Fits both Basic (6 rows) and Developer (10 rows). One EDIT slot per
// row index; only IntTextBox rows populate their slot.
constexpr int BO_MAX_ROWS = 16;

// ─────────────────────────────────────────────────────────────────────
//  Row descriptor
// ─────────────────────────────────────────────────────────────────────

enum class BoKind { Toggle, IntDropdown, IntTextBox };

struct BoRow {
    BoKind         kind;
    const wchar_t* label;
    bool*          boolTarget;        // Toggle only
    int*           intTarget;         // IntDropdown / IntTextBox only
    int            minValue;          // Int* only, inclusive
    int            maxValue;          // Int* only, inclusive
    const wchar_t* tomlSection;
    const wchar_t* tomlKey;
    // Cascade: -1 = independent. Otherwise = index of a Toggle master
    // row in this same list; when the master's boolTarget is false, this
    // row is greyed out and clicks are ignored.
    int            cascadedFrom;
    // Visual: true = label starts a bit further to the right, so nested
    // groups (like the log-detail toggles under Enable Logging) read as
    // subordinate.
    bool           indent;
    // Optional helper text drawn under the label in dim gold. Rows with
    // a helper are rendered taller (BO_ROW_H_TALL) so the two-line block
    // has breathing room.
    const wchar_t* helper;
};

// Basic Options — 6 rows exposing [d2rcore.*] + [d2rloader] show_tcpip_button.
// Material Limit is a text box (0-255 range is too wide for a menu, and
// users typically want a specific number rather than picking from a list).
static BoRow g_boRowsBasic[] = {
    { BoKind::Toggle,      L"Show Sockets",
      &g_loaderOpts.showGroundSockets, nullptr, 0, 0,
      L"d2rcore.items",  L"show_ground_sockets",  -1, false, nullptr },
    { BoKind::Toggle,      L"Show Item Level",
      &g_loaderOpts.displayItemLevels, nullptr, 0, 0,
      L"d2rcore.items",  L"display_item_levels",  -1, false, nullptr },
    { BoKind::Toggle,      L"Respec Skill/Stats",
      &g_loaderOpts.enableRespec,      nullptr, 0, 0,
      L"d2rcore.player", L"enable_respec",        -1, false, nullptr },
    { BoKind::IntDropdown, L"Stash Tabs",
      nullptr, &g_loaderOpts.addSharedTabs,     0, 16,
      L"d2rcore.stash",  L"add_shared_tabs",      -1, false, nullptr },
    { BoKind::IntTextBox,  L"Material Limit",
      nullptr, &g_loaderOpts.setMaterialsLimit, 0, 255,
      L"d2rcore.stash",  L"set_materials_limit",  -1, false,
      L"Default = 99, Max = 255" },
    { BoKind::Toggle,      L"Show TCP/IP Button",
      &g_loaderOpts.showTcpipButton,   nullptr, 0, 0,
      L"d2rloader",      L"show_tcpip_button",    -1, false, nullptr },
};

// Developer Options — 3 top-level toggles then 7 cascaded log-detail
// toggles that grey out when Enable Logging (index 2, the master) is off.
static BoRow g_boRowsDev[] = {
    { BoKind::Toggle, L"Enable Console",
      &g_loaderOpts.enableConsole,    nullptr, 0, 0,
      L"d2rloader.developer", L"enable_console",     -1, false, nullptr },
    { BoKind::Toggle, L"Assert Dialog Message",
      &g_loaderOpts.assertDialogMode, nullptr, 0, 0,
      L"d2rloader.developer", L"assert_dialog_mode", -1, false, nullptr },
    { BoKind::Toggle, L"Enable Logging",
      &g_loaderOpts.logsEnabled,      nullptr, 0, 0,
      L"d2rloader.developer.logs", L"enabled",       -1, false, nullptr },
    { BoKind::Toggle, L"JSON Resources",
      &g_loaderOpts.logJsonResources, nullptr, 0, 0,
      L"d2rloader.developer.logs", L"json_resources", 2, true,  nullptr },
    { BoKind::Toggle, L"Widget Panel Creation",
      &g_loaderOpts.logWidgetPanels,  nullptr, 0, 0,
      L"d2rloader.developer.logs", L"widget_panels",  2, true,  nullptr },
    { BoKind::Toggle, L"Excel File Loaded",
      &g_loaderOpts.logExcelFiles,    nullptr, 0, 0,
      L"d2rloader.developer.logs", L"excel_files",    2, true,  nullptr },
    { BoKind::Toggle, L"True and Open Type Fonts",
      &g_loaderOpts.logFonts,         nullptr, 0, 0,
      L"d2rloader.developer.logs", L"fonts",          2, true,  nullptr },
    { BoKind::Toggle, L"UI Sprites Creation",
      &g_loaderOpts.logSprites,       nullptr, 0, 0,
      L"d2rloader.developer.logs", L"sprites",        2, true,  nullptr },
    { BoKind::Toggle, L"Chat Messages",
      &g_loaderOpts.logChatMessages,  nullptr, 0, 0,
      L"d2rloader.developer.logs", L"chat_messages",  2, true,  nullptr },
    { BoKind::Toggle, L"Models Creation",
      &g_loaderOpts.logModels,        nullptr, 0, 0,
      L"d2rloader.developer.logs", L"models",         2, true,  nullptr },
};

// ─────────────────────────────────────────────────────────────────────
//  State (file-static)
// ─────────────────────────────────────────────────────────────────────

static HWND g_boHwnd     = nullptr;
static HWND g_boCloseBtn = nullptr;
static bool g_boClassReg = false;

// One EDIT HWND per row (only populated for IntTextBox kind). Created
// with the modal, destroyed with it; hosted directly on the modal so
// keyboard focus lands there when the user tabs or clicks in.
static HWND g_boRowEdits[BO_MAX_ROWS] = { nullptr };

// EDIT control IDs. Base 100 leaves 1 for the Close button and lets
// EN_KILLFOCUS route the row index back to us via LOWORD(wparam).
constexpr int BO_EDIT_ID_BASE = 100;

// Which row list is bound to the currently-open modal. Swapped in by
// ShowBasicOptionsModal / ShowDeveloperOptionsModal before the pump
// starts; cleared on WM_DESTROY. Paint + hit-test code reads these.
static const BoRow*  g_activeRows     = nullptr;
static int           g_activeRowCount = 0;
static const wchar_t* g_activeTitle   = L"";

// Popup-menu state for the active int-dropdown click. Only the row
// index (into the active row list) needs to survive across
// TrackPopupMenu since the WM_DRAWITEM / WM_MEASUREITEM callbacks fire
// during the modal blocking call. -1 = no menu open.
static int  g_boOpenMenuRow = -1;

// True if row `i` in the active list is currently inert because its
// master toggle (BoRow::cascadedFrom) is off. Disabled rows paint dim
// and ignore clicks.
static bool RowIsDisabled(int i) {
    if (!g_activeRows) return false;
    if (i < 0 || i >= g_activeRowCount) return false;
    const BoRow& r = g_activeRows[i];
    if (r.cascadedFrom < 0) return false;
    if (r.cascadedFrom >= g_activeRowCount) return false;
    const BoRow& master = g_activeRows[r.cascadedFrom];
    return !(master.boolTarget && *master.boolTarget);
}

// Invalidate just the affected rows so WM_PAINT's HDC clip rect limits
// pixel updates to the touched area. Defined after RowPhysRect (below)
// because it depends on it.
static void InvalidateRowRange(HWND hw, int firstRow, int lastRow);

// ─────────────────────────────────────────────────────────────────────
//  Row-Y computation
// ─────────────────────────────────────────────────────────────────────

// Returns the logical height of row `i`. Rows with `helper` text are
// taller so the two-line label/helper block has breathing room.
static int RowLogicalHeight(int i) {
    if (!g_activeRows) return BO_ROW_H;
    if (i < 0 || i >= g_activeRowCount) return BO_ROW_H;
    return g_activeRows[i].helper ? BO_ROW_H_TALL : BO_ROW_H;
}

// Returns the logical Y coordinate of the top edge of row `i`.
// Iterates prior rows (each may have a different height) rather than
// multiplying by BO_ROW_H, so tall rows shift subsequent rows down.
static int RowLogicalTop(int i) {
    int y = BO_TITLE_TOP_PAD + BO_TITLE_H + BO_TITLE_BOT_PAD;
    for (int j = 0; j < i; ++j) y += RowLogicalHeight(j);
    return y;
}

// Returns the physical (dpi-scaled) rect for row `i`, in modal client
// coordinates. Rows span the full width minus the panel padding.
static RECT RowPhysRect(int i, int physW) {
    int y  = (int)(RowLogicalTop(i)    * g_dpiScale);
    int h  = (int)(RowLogicalHeight(i) * g_dpiScale);
    int lx = (int)(BO_ROW_LABEL_INSET_L * g_dpiScale);
    int rx = physW - (int)(BO_ROW_LABEL_INSET_L * g_dpiScale);
    return { lx, y, rx, y + h };
}

// Value-box rect inside a row — the 70×28 bronze chrome that holds the
// integer value (dropdown chevron or editable EDIT), fixed height so
// tall rows don't stretch it.
static RECT ValueBoxPhysRect(const RECT& row) {
    int boxW   = (int)(BO_ROW_VALUE_BOX_W * g_dpiScale);
    int boxH   = (int)(BO_VALUE_BOX_H     * g_dpiScale);
    int insetR = (int)(BO_ROW_VALUE_INSET_R * g_dpiScale);
    int bx = row.right - boxW - insetR;
    int by = row.top + ((row.bottom - row.top) - boxH) / 2;
    return { bx, by, bx + boxW, by + boxH };
}

// Definition for the forward-declared helper above. For a toggle
// click, invalidate just that row; if the toggle is a cascade master,
// the caller passes a wider range so dependent rows repaint dim/live.
static void InvalidateRowRange(HWND hw, int firstRow, int lastRow) {
    if (!g_activeRows) return;
    if (firstRow < 0) firstRow = 0;
    if (lastRow >= g_activeRowCount) lastRow = g_activeRowCount - 1;
    if (firstRow > lastRow) return;
    RECT clientRc; GetClientRect(hw, &clientRc);
    RECT first = RowPhysRect(firstRow, clientRc.right);
    RECT last  = RowPhysRect(lastRow,  clientRc.right);
    RECT rc = { first.left, first.top, last.right, last.bottom };
    InvalidateRect(hw, &rc, FALSE);
}

static RECT SliderPhysRect(const RECT& row) {
    RECT vb = ValueBoxPhysRect(row);
    int sw = (int)(BO_SLIDER_W * g_dpiScale);
    int sh = (int)(BO_SLIDER_H * g_dpiScale);
    int sx = vb.left + ((vb.right - vb.left) - sw) / 2;
    int sy = row.top + ((row.bottom - row.top) - sh) / 2;
    return { sx, sy, sx + sw, sy + sh };
}

// ─────────────────────────────────────────────────────────────────────
//  Paint
// ─────────────────────────────────────────────────────────────────────

// Paint one row's label + control. Runs inside the modal's WM_PAINT
// after the stone + frame + title backdrop has already been laid down.
// `disabled` = true dims the label and paints a semi-transparent stone
// overlay over the toggle/dropdown so the row reads as inert.
static void PaintRow(Graphics& g, const RECT& row, const BoRow& r,
                     bool disabled) {
    SolidBrush labelBr(disabled ? Tok::BronzeDim : Tok::TextParchment);
    SolidBrush valueBr(disabled ? Tok::BronzeDim : Tok::Gold);
    SolidBrush helperBr(disabled ? Tok::BronzeDim : Tok::TextParchment);

    // Label + optional helper text. Two-line rows split the label area
    // vertically: label on top, helper on the bottom in dim gold.
    StringFormat sfLbl;
    sfLbl.SetAlignment(StringAlignmentNear);
    sfLbl.SetLineAlignment(StringAlignmentCenter);
    sfLbl.SetFormatFlags(sfLbl.GetFormatFlags() | StringFormatFlagsNoWrap);

    RECT vb = ValueBoxPhysRect(row);
    int  labelXBase = row.left + (int)(8 * g_dpiScale);
    int  labelX = labelXBase + (r.indent ? (int)(24 * g_dpiScale) : 0);
    int  labelW = vb.left - (int)(6 * g_dpiScale) - labelX;
    if (labelW < 0) labelW = 0;

    int rowH  = row.bottom - row.top;
    int lblH  = r.helper ? rowH / 2 : rowH;
    int lblY  = row.top;
    int helpY = row.top + lblH;
    int helpH = rowH - lblH;

    if (r.label) {
        // Label: prefer g_fModName (Exocet 18px) so labels read at a
        // comfortable size relative to the value controls. Falls back to
        // g_fBtn (13px) if the larger font failed to load.
        Gdiplus::Font* labelFont = g_fModName ? g_fModName : g_fBtn;
        if (labelFont) {
            g.DrawString(r.label, -1, labelFont,
                         RectF((REAL)labelX, (REAL)lblY,
                               (REAL)labelW, (REAL)lblH),
                         &sfLbl, &labelBr);
        }
    }
    if (r.helper) {
        // Helper: small (11px), dim-gold. Sub-label size keeps the note
        // clearly secondary to the label above.
        Gdiplus::Font* helperFont = g_fSubLbl ? g_fSubLbl : g_fBtn;
        if (helperFont) {
            g.DrawString(r.helper, -1, helperFont,
                         RectF((REAL)labelX, (REAL)helpY,
                               (REAL)labelW, (REAL)helpH),
                         &sfLbl, &helperBr);
        }
    }

    if (r.kind == BoKind::Toggle) {
        // Same asset family as the main window's Show Sockets toggle
        // (btn_toggle1 = false / btn_toggle3 = true). Fallback: pill
        // with a marker at the active end.
        bool on = r.boolTarget && *r.boolTarget;
        RECT sr = SliderPhysRect(row);
        const wchar_t* assetName = on ? L"btn_toggle3.png" : L"btn_toggle1.png";
        if (Gdiplus::Bitmap* asset = AssetImage(assetName)) {
            InterpolationMode prev = g.GetInterpolationMode();
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.DrawImage(asset, (INT)sr.left, (INT)sr.top,
                        (INT)(sr.right - sr.left),
                        (INT)(sr.bottom - sr.top));
            g.SetInterpolationMode(prev);
        } else {
            int trackY = sr.top + (sr.bottom - sr.top) / 2 - 1;
            Pen track(Tok::BronzeBright, 2.0f);
            g.DrawLine(&track,
                       sr.left + (int)(4 * g_dpiScale), trackY,
                       sr.right - (int)(4 * g_dpiScale), trackY);
            int markerW = (sr.bottom - sr.top) - (int)(4 * g_dpiScale);
            int slot0X  = sr.left + (int)(4 * g_dpiScale);
            int slot2X  = sr.right - (int)(4 * g_dpiScale) - markerW;
            int markerX = on ? slot2X : slot0X;
            SolidBrush markerFill(Tok::GoldBright);
            g.FillEllipse(&markerFill, markerX,
                          sr.top + (int)(2 * g_dpiScale),
                          markerW, markerW);
        }
        if (disabled) {
            SolidBrush dim(Color(140, 28, 24, 20));
            g.FillRectangle(&dim, (INT)sr.left, (INT)sr.top,
                            (INT)(sr.right - sr.left),
                            (INT)(sr.bottom - sr.top));
        }
        return;
    }

    // ── IntDropdown / IntTextBox chrome ─────────────────────────────────
    // Both use text_box.png as the bronze chrome. Dropdown paints the
    // value + chevron on top; TextBox skips both — a themed EDIT child
    // sits inside the chrome and paints its own contents.
    if (Gdiplus::Bitmap* tb = AssetImage(L"text_box.png")) {
        InterpolationMode prev = g.GetInterpolationMode();
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(tb, (INT)vb.left, (INT)vb.top,
                    (INT)(vb.right - vb.left),
                    (INT)(vb.bottom - vb.top));
        g.SetInterpolationMode(prev);
    } else {
        Pen border(Tok::Bronze, 1.0f);
        g.DrawRectangle(&border,
                        (INT)vb.left, (INT)vb.top,
                        (INT)((vb.right - vb.left) - 1),
                        (INT)((vb.bottom - vb.top) - 1));
    }

    if (r.kind == BoKind::IntTextBox) {
        // The EDIT paints its own value; nothing more to draw here.
        // (Also skip the disabled overlay — IntTextBox rows have no
        // cascade in the current design, so this is dead code, but the
        // early return keeps the value-paint block below unambiguous.)
        return;
    }

    // ── IntDropdown value + chevron ─────────────────────────────────
    int val = r.intTarget ? *r.intTarget : 0;
    wchar_t buf[16]; swprintf(buf, 16, L"%d", val);
    StringFormat sfC;
    sfC.SetAlignment(StringAlignmentCenter);
    sfC.SetLineAlignment(StringAlignmentCenter);
    int chevronPad = (int)(22 * g_dpiScale);
    if (g_fBtn) {
        g.DrawString(buf, -1, g_fBtn,
                     RectF((REAL)vb.left, (REAL)vb.top,
                           (REAL)((vb.right - vb.left) - chevronPad),
                           (REAL)(vb.bottom - vb.top)),
                     &sfC, &valueBr);
    }

    if (Gdiplus::Bitmap* ch = AssetImage(L"dropdown_chevron.png")) {
        int chW = (int)ch->GetWidth();
        int chH = (int)ch->GetHeight();
        int targetH = (vb.bottom - vb.top) - (int)(4 * g_dpiScale);
        int targetW = (chH > 0) ? chW * targetH / chH : chW;
        int cx = vb.right - targetW - (int)(4 * g_dpiScale);
        int cy = vb.top + ((vb.bottom - vb.top) - targetH) / 2;
        InterpolationMode prev = g.GetInterpolationMode();
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(ch, cx, cy, targetW, targetH);
        g.SetInterpolationMode(prev);
    } else {
        StringFormat sfR;
        sfR.SetAlignment(StringAlignmentCenter);
        sfR.SetLineAlignment(StringAlignmentCenter);
        if (g_fBtn) {
            g.DrawString(L"\u25BE", -1, g_fBtn,
                         RectF((REAL)(vb.right - (int)(16 * g_dpiScale)),
                               (REAL)vb.top, (REAL)(16 * g_dpiScale),
                               (REAL)(vb.bottom - vb.top)),
                         &sfR, &valueBr);
        }
    }
    if (disabled) {
        SolidBrush dim(Color(140, 28, 24, 20));
        g.FillRectangle(&dim, (INT)vb.left, (INT)vb.top,
                        (INT)(vb.right - vb.left),
                        (INT)(vb.bottom - vb.top));
    }
}

// Paint the popup menu's owner-drawn items when TrackPopupMenu asks
// during g_boOpenMenuRow != -1. Item body = value string centered in
// a stone-toned cell, matching the main window's owner-draw menus.
static void PaintMenuItem(DRAWITEMSTRUCT* d) {
    Graphics g(d->hDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    int rl = (int)d->rcItem.left;
    int rt = (int)d->rcItem.top;
    int rR = (int)d->rcItem.right;
    int rB = (int)d->rcItem.bottom;
    int rw = rR - rl;
    int rh = rB - rt;

    bool selected = (d->itemState & ODS_SELECTED) != 0;
    bool checked  = (d->itemState & ODS_CHECKED)  != 0;

    SolidBrush bg(selected ? Tok::BgPanel2 : Tok::BgPanel);
    g.FillRectangle(&bg, rl, rt, rw, rh);

    Pen sep(Tok::BronzeDim, 1.0f);
    g.DrawLine(&sep, rl, rB - 1, rR, rB - 1);

    if (selected) {
        Pen glow(Tok::Gold, 1.0f);
        g.DrawRectangle(&glow, rl + 1, rt + 1, rw - 3, rh - 3);
    }

    if (checked) {
        int dd = S(6);
        SolidBrush dot(Tok::GoldBright);
        g.FillEllipse(&dot, rl + S(8),
                      rt + (rh - dd) / 2, dd, dd);
    }

    // Menu item ID = value + 1 (we shifted by 1 in the insert loop so
    // that TrackPopupMenu can return 0 as "user cancelled").
    int value = (int)d->itemID - 1;
    wchar_t buf[16]; swprintf(buf, 16, L"%d", value);
    StringFormat sfC;
    sfC.SetAlignment(StringAlignmentCenter);
    sfC.SetLineAlignment(StringAlignmentCenter);
    SolidBrush txt(selected ? Tok::GoldBright : Tok::Gold);
    if (g_fBtn) {
        g.DrawString(buf, -1, g_fBtn,
                     RectF((REAL)rl, (REAL)rt, (REAL)rw, (REAL)rh),
                     &sfC, &txt);
    }
}

// ─────────────────────────────────────────────────────────────────────
//  Popup menu (int dropdowns)
// ─────────────────────────────────────────────────────────────────────

// Open a themed int-value popup menu anchored to the row's value box.
// Blocks until the user picks a value or dismisses; on pick, updates
// the target int, writes the toml, and invalidates the modal for a
// repaint. No-op if the row is disabled by cascade.
static void OpenIntMenu(int rowIdx) {
    if (!g_activeRows) return;
    if (rowIdx < 0 || rowIdx >= g_activeRowCount) return;
    if (RowIsDisabled(rowIdx)) return;
    const BoRow& r = g_activeRows[rowIdx];
    if (r.kind != BoKind::IntDropdown || !r.intTarget) return;

    g_boOpenMenuRow = rowIdx;

    HMENU menu = CreatePopupMenu();
    int cur = *r.intTarget;
    for (int v = r.minValue; v <= r.maxValue; ++v) {
        MENUITEMINFOW mii = { sizeof(mii) };
        mii.fMask  = MIIM_FTYPE | MIIM_ID | MIIM_STATE;
        mii.fType  = MFT_OWNERDRAW;
        mii.fState = (v == cur) ? MFS_CHECKED : MFS_UNCHECKED;
        mii.wID    = (UINT)(v + 1);   // +1 so 0 can mean "cancelled"
        InsertMenuItemW(menu, (UINT)(v - r.minValue), TRUE, &mii);
    }

    // Anchor at the bottom-left of the row's value box, in physical
    // screen coordinates.
    RECT clientRc; GetClientRect(g_boHwnd, &clientRc);
    RECT row = RowPhysRect(rowIdx, clientRc.right);
    RECT vb  = ValueBoxPhysRect(row);
    POINT pt = { vb.left, vb.bottom };
    ClientToScreen(g_boHwnd, &pt);

    int chosen = TrackPopupMenu(menu,
                                TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN,
                                pt.x, pt.y, 0, g_boHwnd, nullptr);
    DestroyMenu(menu);
    g_boOpenMenuRow = -1;

    if (chosen > 0) {
        int newVal = chosen - 1;
        if (newVal < r.minValue) newVal = r.minValue;
        if (newVal > r.maxValue) newVal = r.maxValue;
        // r is a const reference into g_activeRows — the mutable int
        // target lives outside the row struct, so we're free to write
        // it (and the toml key) directly.
        *r.intTarget = newVal;
        SaveTomlInt(r.tomlSection, r.tomlKey, newVal);
        InvalidateRowRange(g_boHwnd, rowIdx, rowIdx);
    }
}

// ─────────────────────────────────────────────────────────────────────
//  WndProc
// ─────────────────────────────────────────────────────────────────────

static LRESULT CALLBACK BasicOptionsProc(HWND hw, UINT msg,
                                         WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        int W = rc.right, H = rc.bottom;

        // Double-buffered so asset blits don't flicker on drag.
        HDC memDC     = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);
        {
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

            // Stone bg — sampled at (40,40) so the texture cadence
            // matches the plugin manager popup + main window.
            if (Gdiplus::Bitmap* stone = AssetImage(L"bg_stone.png")) {
                int sw = (int)stone->GetWidth();
                int sh = (int)stone->GetHeight();
                int cropW = (sw < W) ? sw : W;
                int cropH = (sh < H) ? sh : H;
                Rect dst(0, 0, W, H);
                g.DrawImage(stone, dst, 40, 40, cropW, cropH, UnitPixel);
            } else {
                SolidBrush bg(Color(28, 24, 20));
                g.FillRectangle(&bg, 0, 0, W, H);
            }

            // Frame chrome — 9-slice frame_modbanner (corner 24).
            if (Gdiplus::Bitmap* frame = AssetImage(L"frame_modbanner.png")) {
                DrawButton9Slice(g, frame, 0, 0, W, H, 24);
            } else {
                Pen fallback(Tok::Bronze, 1.0f);
                g.DrawRectangle(&fallback, 1, 1, W - 3, H - 3);
            }

            // Title strip — centered at the top.
            SolidBrush titleBr(Tok::Gold);
            StringFormat sfT;
            sfT.SetAlignment(StringAlignmentCenter);
            sfT.SetLineAlignment(StringAlignmentCenter);
            Gdiplus::Font* titleFont = g_fModName ? g_fModName : g_fNavSm;
            if (titleFont) {
                g.DrawString(g_activeTitle, -1, titleFont,
                    RectF((REAL)rc.left, (REAL)S(BO_TITLE_TOP_PAD),
                          (REAL)(rc.right - rc.left),
                          (REAL)S(BO_TITLE_H)),
                    &sfT, &titleBr);
            }

            // Rows — label + control per row. Cascade-dimmed rows
            // paint dim label + overlay-muted control.
            for (int i = 0; i < g_activeRowCount; ++i) {
                RECT row = RowPhysRect(i, W);
                PaintRow(g, row, g_activeRows[i], RowIsDisabled(i));
            }
        }

        // BitBlt only the invalidated region — for targeted row
        // updates this keeps the copy small and avoids overpainting
        // pixels outside the paint clip.
        int px = ps.rcPaint.left;
        int py = ps.rcPaint.top;
        int pw = ps.rcPaint.right - ps.rcPaint.left;
        int ph = ps.rcPaint.bottom - ps.rcPaint.top;
        BitBlt(hdc, px, py, pw, ph, memDC, px, py, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT rc; GetClientRect(hw, &rc);
        for (int i = 0; i < g_activeRowCount; ++i) {
            RECT row = RowPhysRect(i, rc.right);
            if (pt.y < row.top || pt.y >= row.bottom) continue;
            if (pt.x < row.left || pt.x >= row.right) continue;

            if (RowIsDisabled(i)) return 0;    // cascade-inert

            const BoRow& r = g_activeRows[i];
            if (r.kind == BoKind::Toggle) {
                // Row-wide hit target — clicking anywhere in the row
                // toggles the boolean. Forgiving on small slider art.
                if (r.boolTarget) {
                    *r.boolTarget = !*r.boolTarget;
                    SaveTomlBool(r.tomlSection, r.tomlKey, *r.boolTarget);
                    // If this row is a cascade master, dependent rows
                    // may need to flip between dim/live — find the
                    // trailing dependent index and invalidate the whole
                    // affected range. Otherwise, just this row.
                    int lastRow = i;
                    for (int j = i + 1; j < g_activeRowCount; ++j) {
                        if (g_activeRows[j].cascadedFrom == i) lastRow = j;
                    }
                    InvalidateRowRange(hw, i, lastRow);
                    UpdateWindow(hw);
                }
            } else if (r.kind == BoKind::IntDropdown) {
                // Only the value-box area opens the popup, so clicking
                // the label doesn't spuriously open menus.
                RECT vb = ValueBoxPhysRect(row);
                if (pt.x >= vb.left && pt.x < vb.right
                    && pt.y >= vb.top && pt.y < vb.bottom) {
                    OpenIntMenu(i);
                }
            }
            // IntTextBox: the EDIT child catches its own clicks; a
            // click on the label area does nothing (matches the
            // dropdown case — no spurious focus grab).
            break;
        }
        return 0;
    }

    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* m = (MEASUREITEMSTRUCT*)lp;
        if (m->CtlType == ODT_MENU && g_boOpenMenuRow >= 0) {
            m->itemWidth  = S(BO_MENU_ITEM_W);
            m->itemHeight = S(BO_MENU_ITEM_H);
            return TRUE;
        }
        return FALSE;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;
        if (d->CtlType == ODT_MENU && g_boOpenMenuRow >= 0) {
            PaintMenuItem(d);
            return TRUE;
        }
        // Close button — MkStdBtn/PaintOwnerDrawButton pipeline.
        if (PaintOwnerDrawButton(d)) return TRUE;
        return 0;
    }

    case WM_COMMAND: {
        WORD id   = LOWORD(wp);
        WORD code = HIWORD(wp);
        // IntTextBox EDIT lost focus (user tabbed/clicked away or the
        // modal closed). Read the string, clamp to the row's range,
        // and write to the toml + intTarget. The EDIT is refreshed
        // with the clamped value so out-of-range typing snaps back.
        if (code == EN_KILLFOCUS
            && id >= BO_EDIT_ID_BASE
            && id <  BO_EDIT_ID_BASE + BO_MAX_ROWS
            && g_activeRows) {
            int rowIdx = id - BO_EDIT_ID_BASE;
            if (rowIdx >= g_activeRowCount) break;
            const BoRow& r = g_activeRows[rowIdx];
            if (r.kind != BoKind::IntTextBox || !r.intTarget) break;
            HWND ed = g_boRowEdits[rowIdx];
            if (!ed) break;
            wchar_t buf[16] = {};
            GetWindowTextW(ed, buf, 16);
            int val = _wtoi(buf);
            if (val < r.minValue) val = r.minValue;
            if (val > r.maxValue) val = r.maxValue;
            *r.intTarget = val;
            SaveTomlInt(r.tomlSection, r.tomlKey, val);
            // Refresh EDIT with the clamped value (silently no-op if
            // val already equals the parsed text).
            wchar_t back[16]; swprintf(back, 16, L"%d", val);
            SetWindowTextW(ed, back);
            return 0;
        }
        if (code == BN_CLICKED && id == 1) {
            DestroyWindow(hw);
            return 0;
        }
        break;
    }

    case WM_CTLCOLOREDIT: {
        // Theme the IntTextBox EDITs — dark bg + gold text on a black
        // brush so the value box reads like a shadowed well matching
        // the rename modal's text field.
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, RGB(0xE0, 0xC0, 0x70));  // warm gold
        SetBkColor(hdc, RGB(0, 0, 0));
        return (LRESULT)GetStockObject(BLACK_BRUSH);
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            DestroyWindow(hw);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hw);
        return 0;

    case WM_DESTROY: {
        HWND parent = GetWindow(hw, GW_OWNER);
        if (parent) {
            EnableWindow(parent, TRUE);
            SetForegroundWindow(parent);
        }
        g_boHwnd     = nullptr;
        g_boCloseBtn = nullptr;
        // EDIT children are destroyed automatically with the parent;
        // just null the array so a subsequent open starts clean.
        for (int i = 0; i < BO_MAX_ROWS; ++i) g_boRowEdits[i] = nullptr;
        g_activeRows     = nullptr;
        g_activeRowCount = 0;
        g_activeTitle    = L"";
        return 0;
    }
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

// ─────────────────────────────────────────────────────────────────────
//  Shared spawner + public entry points
// ─────────────────────────────────────────────────────────────────────

// Modal height derived from row list so Basic (6 rows, one tall) and
// Developer (10 rows, all standard) share the same layout constants
// and just size differently.
static int ComputeModalHeightLogical(const BoRow* rows, int rowCount) {
    int y = BO_TITLE_TOP_PAD + BO_TITLE_H + BO_TITLE_BOT_PAD;
    for (int j = 0; j < rowCount; ++j) {
        y += rows[j].helper ? BO_ROW_H_TALL : BO_ROW_H;
    }
    return y + BO_ROW_TO_BTN_GAP + BO_BTN_H + BO_BTN_BOTTOM_PAD;
}

static void ShowLoaderOptionsModal(HWND parent, const wchar_t* title,
                                   const BoRow* rows, int rowCount) {
    if (g_boHwnd) return;   // already open (either flavor)
    if (rowCount > BO_MAX_ROWS) rowCount = BO_MAX_ROWS;

    if (!g_boClassReg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = BasicOptionsProc;
        wc.hInstance     = g_hInst;
        wc.lpszClassName = L"AngirisLoaderOptionsModal";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        g_boClassReg = true;
    }

    // Bind the active list before creating the window (WM_PAINT can
    // fire during CreateWindow/ShowWindow, so the paint code needs
    // these already valid).
    g_activeRows     = rows;
    g_activeRowCount = rowCount;
    g_activeTitle    = title;
    for (int i = 0; i < BO_MAX_ROWS; ++i) g_boRowEdits[i] = nullptr;

    RECT pr;
    GetWindowRect(parent, &pr);
    int physW = (int)(BO_W * g_dpiScale);
    int physH = (int)(ComputeModalHeightLogical(rows, rowCount) * g_dpiScale);
    int x = pr.left + ((pr.right  - pr.left) - physW) / 2;
    int y = pr.top  + ((pr.bottom - pr.top ) - physH) / 2;

    g_boHwnd = CreateWindowExW(
        WS_EX_TOPMOST,     // no DLGMODALFRAME — our own frame_modbanner
                           // is the visible border; the system-drawn
                           // 3D edge from DLGMODALFRAME was showing as
                           // a bright white ring around the popup.
        L"AngirisLoaderOptionsModal",
        title,
        // WS_CLIPCHILDREN: parent paints excluded from where children
        // sit, so the Close button + IntTextBox EDITs don't flicker
        // during targeted row invalidations.
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, physW, physH,
        parent, nullptr, g_hInst, nullptr);
    if (!g_boHwnd) {
        g_activeRows     = nullptr;
        g_activeRowCount = 0;
        g_activeTitle    = L"";
        return;
    }

    // Close button — centered horizontally, bottom-anchored so the
    // button grows upward if we ever bump BO_BTN_H.
    int physBtnW = (int)(BO_BTN_W * g_dpiScale);
    int physBtnH = (int)(BO_BTN_H * g_dpiScale);
    int btnX = (physW - physBtnW) / 2;
    int btnY = physH - (int)(BO_BTN_BOTTOM_PAD * g_dpiScale) - physBtnH;
    g_boCloseBtn = MkStdBtn(g_boHwnd, L"Close", 1,
                            btnX, btnY, physBtnW, physBtnH,
                            true, ButtonKind::Plugins);

    // For each IntTextBox row, spawn a themed EDIT positioned inside
    // the value box. ID = BO_EDIT_ID_BASE + row index so EN_KILLFOCUS
    // routes back to the right row.
    for (int i = 0; i < rowCount; ++i) {
        if (rows[i].kind != BoKind::IntTextBox) continue;
        RECT rowRc = RowPhysRect(i, physW);
        RECT vb    = ValueBoxPhysRect(rowRc);
        int inset = (int)(4 * g_dpiScale);
        int ex = vb.left + inset;
        int ey = vb.top + inset;
        int ew = (vb.right - vb.left) - 2 * inset;
        int eh = (vb.bottom - vb.top) - 2 * inset;
        int cur = rows[i].intTarget ? *rows[i].intTarget : 0;
        wchar_t buf[16]; swprintf(buf, 16, L"%d", cur);
        g_boRowEdits[i] = CreateWindowExW(0,
            L"EDIT", buf,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_NUMBER,
            ex, ey, ew, eh,
            g_boHwnd, (HMENU)(UINT_PTR)(BO_EDIT_ID_BASE + i),
            g_hInst, nullptr);
        if (g_boRowEdits[i]) {
            // Cap the input at 3 chars (max int value 255 needs 3).
            SendMessage(g_boRowEdits[i], EM_SETLIMITTEXT, 3, 0);
        }
    }

    EnableWindow(parent, FALSE);
    ShowWindow(g_boHwnd, SW_SHOW);
    UpdateWindow(g_boHwnd);
    SetActiveWindow(g_boHwnd);

    MSG msg;
    while (g_boHwnd) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == 0 || got == -1) {
            if (got == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (g_boHwnd && IsDialogMessageW(g_boHwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void ShowBasicOptionsModal(HWND parent) {
    ShowLoaderOptionsModal(parent, L"Basic Options",
        g_boRowsBasic,
        (int)(sizeof(g_boRowsBasic) / sizeof(g_boRowsBasic[0])));
}

void ShowDeveloperOptionsModal(HWND parent) {
    ShowLoaderOptionsModal(parent, L"Developer Options",
        g_boRowsDev,
        (int)(sizeof(g_boRowsDev) / sizeof(g_boRowsDev[0])));
}
