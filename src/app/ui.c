/*
 * TurkishDPI - main window and tray icon.
 *
 * Hand-drawn dark UI: plain Win32 + GDI+ (flat API, anti-aliased shapes) and
 * GDI ClearType text. No framework, no web view: the window costs a few
 * hundred KB while open and nothing at all while hidden.
 *
 * Layout (docs/design/SCREENS.md, "Layout A"): one fixed 400x520 window with
 * three pages - Durum (status), Ayrıntılar (details), Ayarlar (settings).
 * Everything clickable is a native BUTTON with BS_OWNERDRAW, so Tab/Space/
 * Enter work through IsDialogMessageW and screen readers read the button
 * text. The parent only paints static content (texts, cards, lists).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "syslib.h"
#include "app.h"
#include "presets.h"
#include "version.h"

/* ---------- minimal GDI+ flat API ---------- */
typedef struct { UINT32 ver; void *cb; BOOL no_thread; BOOL no_codecs; } GpStartupInput;
typedef void GpGraphics, GpBrush, GpPen, GpPath;
typedef struct { float X, Y; } GpPointF;
#define GPAPI __stdcall
/*
 * Resolved at run time from System32 (gdiplus_load) instead of being a
 * static import: on Windows 7/8 gdiplus.dll is not a KnownDLL, so this
 * elevated exe would otherwise load a gdiplus.dll planted next to it.
 */
typedef int (GPAPI *GdiplusStartup_t)(ULONG_PTR *, const GpStartupInput *, void *);
static GdiplusStartup_t p_GdiplusStartup;
#define GdiplusStartup p_GdiplusStartup
typedef int (GPAPI *GdipCreateFromHDC_t)(HDC, GpGraphics **);
static GdipCreateFromHDC_t p_GdipCreateFromHDC;
#define GdipCreateFromHDC p_GdipCreateFromHDC
typedef int (GPAPI *GdipDeleteGraphics_t)(GpGraphics *);
static GdipDeleteGraphics_t p_GdipDeleteGraphics;
#define GdipDeleteGraphics p_GdipDeleteGraphics
typedef int (GPAPI *GdipSetSmoothingMode_t)(GpGraphics *, int);
static GdipSetSmoothingMode_t p_GdipSetSmoothingMode;
#define GdipSetSmoothingMode p_GdipSetSmoothingMode
typedef int (GPAPI *GdipCreateSolidFill_t)(DWORD, GpBrush **);
static GdipCreateSolidFill_t p_GdipCreateSolidFill;
#define GdipCreateSolidFill p_GdipCreateSolidFill
typedef int (GPAPI *GdipDeleteBrush_t)(GpBrush *);
static GdipDeleteBrush_t p_GdipDeleteBrush;
#define GdipDeleteBrush p_GdipDeleteBrush
typedef int (GPAPI *GdipCreatePen1_t)(DWORD, float, int, GpPen **);
static GdipCreatePen1_t p_GdipCreatePen1;
#define GdipCreatePen1 p_GdipCreatePen1
typedef int (GPAPI *GdipSetPenStartCap_t)(GpPen *, int);
static GdipSetPenStartCap_t p_GdipSetPenStartCap;
#define GdipSetPenStartCap p_GdipSetPenStartCap
typedef int (GPAPI *GdipSetPenEndCap_t)(GpPen *, int);
static GdipSetPenEndCap_t p_GdipSetPenEndCap;
#define GdipSetPenEndCap p_GdipSetPenEndCap
typedef int (GPAPI *GdipSetPenLineJoin_t)(GpPen *, int);
static GdipSetPenLineJoin_t p_GdipSetPenLineJoin;
#define GdipSetPenLineJoin p_GdipSetPenLineJoin
typedef int (GPAPI *GdipDeletePen_t)(GpPen *);
static GdipDeletePen_t p_GdipDeletePen;
#define GdipDeletePen p_GdipDeletePen
typedef int (GPAPI *GdipFillEllipse_t)(GpGraphics *, GpBrush *, float, float, float, float);
static GdipFillEllipse_t p_GdipFillEllipse;
#define GdipFillEllipse p_GdipFillEllipse
typedef int (GPAPI *GdipDrawEllipse_t)(GpGraphics *, GpPen *, float, float, float, float);
static GdipDrawEllipse_t p_GdipDrawEllipse;
#define GdipDrawEllipse p_GdipDrawEllipse
typedef int (GPAPI *GdipDrawArc_t)(GpGraphics *, GpPen *, float, float, float, float, float, float);
static GdipDrawArc_t p_GdipDrawArc;
#define GdipDrawArc p_GdipDrawArc
typedef int (GPAPI *GdipDrawLine_t)(GpGraphics *, GpPen *, float, float, float, float);
static GdipDrawLine_t p_GdipDrawLine;
#define GdipDrawLine p_GdipDrawLine
typedef int (GPAPI *GdipDrawLines_t)(GpGraphics *, GpPen *, const GpPointF *, int);
static GdipDrawLines_t p_GdipDrawLines;
#define GdipDrawLines p_GdipDrawLines
typedef int (GPAPI *GdipFillPolygon_t)(GpGraphics *, GpBrush *, const GpPointF *, int, int);
static GdipFillPolygon_t p_GdipFillPolygon;
#define GdipFillPolygon p_GdipFillPolygon
typedef int (GPAPI *GdipCreatePath_t)(int, GpPath **);
static GdipCreatePath_t p_GdipCreatePath;
#define GdipCreatePath p_GdipCreatePath
typedef int (GPAPI *GdipAddPathArc_t)(GpPath *, float, float, float, float, float, float);
static GdipAddPathArc_t p_GdipAddPathArc;
#define GdipAddPathArc p_GdipAddPathArc
typedef int (GPAPI *GdipClosePathFigure_t)(GpPath *);
static GdipClosePathFigure_t p_GdipClosePathFigure;
#define GdipClosePathFigure p_GdipClosePathFigure
typedef int (GPAPI *GdipFillPath_t)(GpGraphics *, GpBrush *, GpPath *);
static GdipFillPath_t p_GdipFillPath;
#define GdipFillPath p_GdipFillPath
typedef int (GPAPI *GdipDrawPath_t)(GpGraphics *, GpPen *, GpPath *);
static GdipDrawPath_t p_GdipDrawPath;
#define GdipDrawPath p_GdipDrawPath
typedef int (GPAPI *GdipDeletePath_t)(GpPath *);
static GdipDeletePath_t p_GdipDeletePath;
#define GdipDeletePath p_GdipDeletePath

static int gdiplus_load(void) {
    HMODULE h = load_system_dll(L"gdiplus.dll");
    if (!h) return 0;
    p_GdiplusStartup = (GdiplusStartup_t)(void *)GetProcAddress(h, "GdiplusStartup");
    p_GdipCreateFromHDC = (GdipCreateFromHDC_t)(void *)GetProcAddress(h, "GdipCreateFromHDC");
    p_GdipDeleteGraphics = (GdipDeleteGraphics_t)(void *)GetProcAddress(h, "GdipDeleteGraphics");
    p_GdipSetSmoothingMode = (GdipSetSmoothingMode_t)(void *)GetProcAddress(h, "GdipSetSmoothingMode");
    p_GdipCreateSolidFill = (GdipCreateSolidFill_t)(void *)GetProcAddress(h, "GdipCreateSolidFill");
    p_GdipDeleteBrush = (GdipDeleteBrush_t)(void *)GetProcAddress(h, "GdipDeleteBrush");
    p_GdipCreatePen1 = (GdipCreatePen1_t)(void *)GetProcAddress(h, "GdipCreatePen1");
    p_GdipSetPenStartCap = (GdipSetPenStartCap_t)(void *)GetProcAddress(h, "GdipSetPenStartCap");
    p_GdipSetPenEndCap = (GdipSetPenEndCap_t)(void *)GetProcAddress(h, "GdipSetPenEndCap");
    p_GdipSetPenLineJoin = (GdipSetPenLineJoin_t)(void *)GetProcAddress(h, "GdipSetPenLineJoin");
    p_GdipDeletePen = (GdipDeletePen_t)(void *)GetProcAddress(h, "GdipDeletePen");
    p_GdipFillEllipse = (GdipFillEllipse_t)(void *)GetProcAddress(h, "GdipFillEllipse");
    p_GdipDrawEllipse = (GdipDrawEllipse_t)(void *)GetProcAddress(h, "GdipDrawEllipse");
    p_GdipDrawArc = (GdipDrawArc_t)(void *)GetProcAddress(h, "GdipDrawArc");
    p_GdipDrawLine = (GdipDrawLine_t)(void *)GetProcAddress(h, "GdipDrawLine");
    p_GdipDrawLines = (GdipDrawLines_t)(void *)GetProcAddress(h, "GdipDrawLines");
    p_GdipFillPolygon = (GdipFillPolygon_t)(void *)GetProcAddress(h, "GdipFillPolygon");
    p_GdipCreatePath = (GdipCreatePath_t)(void *)GetProcAddress(h, "GdipCreatePath");
    p_GdipAddPathArc = (GdipAddPathArc_t)(void *)GetProcAddress(h, "GdipAddPathArc");
    p_GdipClosePathFigure = (GdipClosePathFigure_t)(void *)GetProcAddress(h, "GdipClosePathFigure");
    p_GdipFillPath = (GdipFillPath_t)(void *)GetProcAddress(h, "GdipFillPath");
    p_GdipDrawPath = (GdipDrawPath_t)(void *)GetProcAddress(h, "GdipDrawPath");
    p_GdipDeletePath = (GdipDeletePath_t)(void *)GetProcAddress(h, "GdipDeletePath");
    return p_GdiplusStartup && p_GdipCreateFromHDC && p_GdipDeleteGraphics && p_GdipSetSmoothingMode && p_GdipCreateSolidFill && p_GdipDeleteBrush && p_GdipCreatePen1 && p_GdipSetPenStartCap && p_GdipSetPenEndCap && p_GdipSetPenLineJoin && p_GdipDeletePen && p_GdipFillEllipse && p_GdipDrawEllipse && p_GdipDrawArc && p_GdipDrawLine && p_GdipDrawLines && p_GdipFillPolygon && p_GdipCreatePath && p_GdipAddPathArc && p_GdipClosePathFigure && p_GdipFillPath && p_GdipDrawPath && p_GdipDeletePath;
}


/* ---------- palette (docs/design/DESIGN-SYSTEM.md §4) ---------- */
typedef struct {
    COLORREF bg, surface, hover, sunken, border, text, text2, text3, focus;
    COLORREF accent, accent_hover, on_accent, toggle_on;
    COLORREF ok, warn, busy, idle, error;
} palette_t;

static const palette_t PAL_DARK = {
    RGB(0x11, 0x13, 0x1A), RGB(0x1A, 0x1E, 0x29), RGB(0x23, 0x28, 0x36), RGB(0x15, 0x18, 0x21),
    RGB(0x2A, 0x30, 0x40), RGB(0xEC, 0xEE, 0xF3), RGB(0x9A, 0xA3, 0xB5), RGB(0x83, 0x8B, 0x9D),
    RGB(0x60, 0xA5, 0xFA),
    RGB(0xC8, 0x1E, 0x2E), RGB(0xD9, 0x2B, 0x3B), RGB(0xFF, 0xFF, 0xFF), RGB(0x3B, 0x82, 0xF6),
    RGB(0x22, 0xC5, 0x5E), RGB(0xF5, 0xB0, 0x14), RGB(0x60, 0xA5, 0xFA), RGB(0x94, 0xA3, 0xB8),
    RGB(0xF8, 0x71, 0x71),
};
static const palette_t *P = &PAL_DARK;

static DWORD AR(COLORREF c, int a) {
    return ((DWORD)a << 24) | ((DWORD)GetRValue(c) << 16) | ((DWORD)GetGValue(c) << 8) | GetBValue(c);
}

static COLORREF mix(COLORREF a, COLORREF b, int pct) {   /* pct of b over a */
    return RGB((GetRValue(a) * (100 - pct) + GetRValue(b) * pct) / 100,
               (GetGValue(a) * (100 - pct) + GetGValue(b) * pct) / 100,
               (GetBValue(a) * (100 - pct) + GetBValue(b) * pct) / 100);
}

/* ---------- window state ---------- */
static HWND g_wnd, g_edit;
static HINSTANCE g_inst;
static UINT g_taskbar_created;
static UINT g_show_msg;              /* TURKISHDPI_SHOW_MSG: a second start */
static ULONG_PTR g_gdip;
static float g_scale = 1.0f;
static HFONT f_display, f_title, f_body, f_caption, f_label, f_button;
static HBRUSH g_sunken_brush;
static HICON g_logo, g_tray_icons[6];
static int g_anim = 0, g_hot = 0, g_spinning = 0, g_layout_n = -1;
static NOTIFYICONDATAW g_nid;
static WNDPROC g_edit_proc, g_btn_proc;
int g_payment_domains = 100;

/* --ui-preview: UI only, mock state, no privileged or persistent action */
int g_ui_preview = 0;
static float g_force_scale = 0.0f;   /* preview --scale; 0 = window DPI */
static int g_preview_shot = 0;       /* render off-screen, capture, exit */
static int g_preview_focus = 0;      /* --focus: draw this control as focused */
static int g_preview_press = 0;      /* --press: draw this control as pressed */
static void preview_action(const wchar_t *what);

enum { PG_STATUS, PG_DETAILS, PG_SETTINGS, PG_ALL = -1 };
static int g_page = PG_STATUS;

enum { IDM_AUTO = 1000, IDM_AUTOSTART, IDM_PAYMENT, IDM_LOG, IDM_INI, IDM_GITHUB,
       IDM_EXIT, IDM_SHOW, IDM_PAUSE, IDM_RESCAN, IDM_UNINSTALL, IDM_AVHELP, IDM_PROXY,
       IDM_UPDATE, IDM_PRESET = 1100 };

#define BASE_W 400
#define BASE_H 520

static int S(int v) { return (int)(v * g_scale + 0.5f); }
static float SF(float v) { return v * g_scale; }

/* ---------- controls ---------- */
enum { K_TAB, K_RING, K_PRIMARY, K_SECONDARY, K_SMALL, K_LINK, K_TOGGLE, K_BANNER, K_DANGER,
       K_PAYLINE };
enum { ID_TAB0 = 200, ID_TAB1, ID_TAB2, ID_POWER, ID_ACTION, ID_BANNER, ID_PAYLINE,
       ID_CHANGE, ID_RESCAN, ID_TEST,
       ID_T_AUTO, ID_T_START, ID_T_PAY, ID_L_LOG, ID_L_INI, ID_L_AV, ID_L_GH,
       ID_UNINSTALL, ID_EXIT };

typedef struct {
    int id, page, kind;
    int x, y, w, h;          /* logical px; site-test row is moved by layout() */
    HWND hwnd;
} ctl_t;

static ctl_t g_ctl[] = {
    /* hit areas >= 32 px tall; tabs/small buttons draw a smaller shape */
    { ID_TAB0,      PG_ALL,      K_TAB,       150,  12,  66, 32, NULL },
    { ID_TAB1,      PG_ALL,      K_TAB,       216,  12,  88, 32, NULL },
    { ID_TAB2,      PG_ALL,      K_TAB,       304,  12,  70, 32, NULL },
    { ID_BANNER,    PG_STATUS,   K_BANNER,     24,  60, 352, 36, NULL },
    { ID_POWER,     PG_STATUS,   K_RING,      128, 104, 144, 144, NULL },
    { ID_ACTION,    PG_STATUS,   K_SECONDARY,  80, 440, 240, 36, NULL },
    { ID_PAYLINE,   PG_STATUS,   K_PAYLINE,    24, 484, 352, 32, NULL },
    { ID_CHANGE,    PG_DETAILS,  K_SMALL,     288,  58,  88, 32, NULL },
    { ID_RESCAN,    PG_DETAILS,  K_SMALL,     256, 188, 120, 32, NULL },
    { ID_TEST,      PG_DETAILS,  K_PRIMARY,   288, 300,  88, 32, NULL },
    { ID_T_AUTO,    PG_SETTINGS, K_TOGGLE,     24,  60, 352, 56, NULL },
    { ID_T_START,   PG_SETTINGS, K_TOGGLE,     24, 116, 352, 56, NULL },
    { ID_T_PAY,     PG_SETTINGS, K_TOGGLE,     24, 172, 352, 56, NULL },
    { ID_L_LOG,     PG_SETTINGS, K_LINK,       24, 248, 172, 32, NULL },
    { ID_L_INI,     PG_SETTINGS, K_LINK,      204, 248, 172, 32, NULL },
    { ID_L_AV,      PG_SETTINGS, K_LINK,       24, 282, 172, 32, NULL },
    { ID_L_GH,      PG_SETTINGS, K_LINK,      204, 282, 172, 32, NULL },
    { ID_UNINSTALL, PG_SETTINGS, K_DANGER,     24, 452, 180, 36, NULL },
    { ID_EXIT,      PG_SETTINGS, K_SECONDARY, 276, 452, 100, 36, NULL },
};
#define NCTL ((int)(sizeof(g_ctl) / sizeof(g_ctl[0])))

static ctl_t *ctl(int id) {
    for (int i = 0; i < NCTL; i++)
        if (g_ctl[i].id == id) return &g_ctl[i];
    return NULL;
}

static RECT ctl_rect(const ctl_t *c) {
    RECT r = { S(c->x), S(c->y), S(c->x + c->w), S(c->y + c->h) };
    return r;
}

/*
 * Screen-reader text. The pages are painted with GDI, so the status, the
 * details rows and the site-check result would be invisible to Narrator/NVDA.
 * Each block gets a STATIC child with the same text, placed over the painted
 * area and in reading order. Its window region is empty: it paints nothing
 * and takes no clicks (WS_CLIPCHILDREN keeps the parent's drawing), but its
 * bounds are on screen, so screen readers read it in place.
 */
enum { A_STATUS, A_DETAILS, A_TARGETS, A_SITE_LABEL, A_SITE_RESULT, A_SETTINGS, A_COUNT };
static HWND g_a11y[A_COUNT];
static WNDPROC g_static_proc;

/* Never draws (also not into PrintWindow), never takes the mouse. */
static LRESULT CALLBACK a11y_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PRINT: case WM_PRINTCLIENT: return 0;
    case WM_NCHITTEST: return HTTRANSPARENT;
    }
    return CallWindowProcW(g_static_proc, h, msg, wp, lp);
}

/* ---------- state helpers ---------- */
static void snapshot(ui_state_t *st) {
    EnterCriticalSection(&g_lock);
    *st = g_ui;
    LeaveCriticalSection(&g_lock);
}

static COLORREF state_color(state_kind_t k) {
    switch (k) {
    case ST_ACTIVE:   return P->ok;
    case ST_PARTIAL:  return P->warn;
    case ST_SCANNING:
    case ST_STARTING: return P->busy;
    case ST_STANDBY:
    case ST_PAUSED:
    case ST_OFFLINE:  return P->idle;
    default:          return P->error;
    }
}

/* tray icons: 1 ok, 2 busy, 3 idle, 4 error, 5 warn (0 = app icon) */
static int tray_icon_index(state_kind_t k) {
    switch (k) {
    case ST_ACTIVE:   return 1;
    case ST_SCANNING:
    case ST_STARTING: return 2;
    case ST_STANDBY:
    case ST_PAUSED:
    case ST_OFFLINE:  return 3;
    case ST_PARTIAL:  return 5;
    default:          return 4;
    }
}

static int driver_problem(const ui_state_t *st) {
    return st->kind == ST_ERROR && st->driver_err != 0;
}

/* Short, non-technical headline; the controller's own text stays in
 * Ayrıntılar and the tray tooltip. */
static const wchar_t *kind_headline(const ui_state_t *st) {
    switch (st->kind) {
    case ST_ACTIVE:   return L"Koruma açık";
    case ST_PARTIAL:  return L"Kısmen çalışıyor";
    case ST_STARTING:
    case ST_SCANNING: return L"Hazırlanıyor…";
    case ST_STANDBY:  return L"Engel yok";
    case ST_PAUSED:   return L"Duraklatıldı";
    case ST_OFFLINE:  return L"İnternet yok";
    default:
        if (st->driver_err == 193 || st->driver_err == 1006) return L"Dosyalar uygun değil";
        return driver_problem(st) ? L"Sürücü başlatılamadı" : L"Çalışamıyor";
    }
}

static const wchar_t *kind_sub(const ui_state_t *st) {
    switch (st->kind) {
    case ST_ACTIVE:   return L"Engelli siteler açılıyor.";
    case ST_PARTIAL:  return L"Bazı siteler açılmıyor olabilir.";
    case ST_STARTING:
    case ST_SCANNING: return st->learned == 0
                             ? L"Bu ağda ilk kez en iyi yöntem aranıyor. Bu bir dakika sürebilir."
                             : L"En uygun yöntem aranıyor.";
    case ST_STANDBY:  return L"Gerekirse kendiliğinden devreye girer.";
    case ST_PAUSED:   return L"Koruma kapalı.";
    case ST_OFFLINE:  return L"Bağlantı gelince kendiliğinden başlar.";
    default: {        /* errors: the controller's actionable text */
        static wchar_t msg[300];   /* UI thread only */
        if (driver_problem(st)) return st->detail;
        if (st->detail[0] && wcscmp(st->detail, st->status) != 0) {
            _snwprintf(msg, 299, L"%ls. %ls", st->status, st->detail);
            msg[299] = 0;
            return msg;
        }
        return st->status;
    }
    }
}

static const wchar_t *speed_word(int ms) {
    return ms < 250 ? L"iyi" : ms < 600 ? L"orta" : L"yavaş";
}

enum { A_NONE, A_SITE, A_RETRY, A_FIX, A_RESUME, A_DOWNLOAD };

static int action_for(const ui_state_t *st) {
    switch (st->kind) {
    case ST_ACTIVE:
    case ST_STANDBY: return A_SITE;
    case ST_PARTIAL: return A_RETRY;
    case ST_PAUSED:  return A_RESUME;
    case ST_ERROR:
        /* missing / blocked files: antivirus help; wrong or altered files:
         * the download page; another WinDivert loaded: needs a reboot, a
         * retry cannot help */
        if (st->driver_err == 2 || st->driver_err == 126 || st->driver_err == 1275 ||
            st->driver_err == 1920) return A_FIX;
        if (st->driver_err == 193 || st->driver_err == 1006) return A_DOWNLOAD;
        if (st->driver_err == 654 || st->driver_err == 50) return A_NONE;
        return A_RETRY;
    default:         return A_NONE;
    }
}

static const wchar_t *action_label(int a) {
    switch (a) {
    case A_SITE:   return L"Bir site açılmıyor mu?";
    case A_RETRY:  return L"Yeniden dene";
    case A_FIX:    return L"Nasıl düzeltilir?";
    case A_RESUME: return L"Korumayı aç";
    case A_DOWNLOAD: return L"İndirme sayfası";
    default:       return L"";
    }
}

/* notice banner: 0 none, 1 update, 2 proxy, 3 rival (priority 2 > 3 > 1) */
static int banner_kind(const ui_state_t *st, int *others) {
    int n = (st->proxy_broken ? 1 : 0) + (st->rival[0] ? 1 : 0) + (st->update_tag[0] ? 1 : 0);
    *others = n > 0 ? n - 1 : 0;
    return st->proxy_broken ? 2 : st->rival[0] ? 3 : st->update_tag[0] ? 1 : 0;
}

static void banner_text(const ui_state_t *st, int kind, wchar_t *buf, int n) {
    if (kind == 1 && st->update_state == 1)
        _snwprintf(buf, n - 1, L"Güncelleniyor: %ls…", st->update_tag);
    else if (kind == 1 && st->update_state == 2)
        _snwprintf(buf, n - 1, L"Güncelleme yapılamadı: %ls", st->update_tag);
    else if (kind == 1)
        _snwprintf(buf, n - 1, L"Yeni sürüm hazır: %ls", st->update_tag);
    else if (kind == 2)
        _snwprintf(buf, n - 1, L"%ls", L"Bozuk bir proxy ayarı bulundu");
    else if (kind == 3)
        _snwprintf(buf, n - 1, L"%ls çalışıyor; çakışmayı önlemek için kapatın", st->rival);
    else
        buf[0] = 0;
    buf[n - 1] = 0;
}

/* what the update banner's button does in each step */
static const wchar_t *update_action(const ui_state_t *st) {
    return st->update_state == 1 ? L"" : st->update_state == 2 ? L"Sayfa →" : L"Güncelle →";
}

static int ok_count(const ui_state_t *st) {
    int c = 0;
    for (int i = 0; i < st->ntargets && i < PROBE_MAX_HOSTS; i++)
        if (st->target_ok[i] == 1) c++;
    return c;
}

static int ui_autostart(void) {
    return g_ui_preview ? g_ui.autostart == 1 : autostart_enabled();
}

/* Details page: y of the "Site kontrolü" section, below the target rows. */
static int details_site_y(int ntargets) {
    int n = ntargets < 1 ? 1 : ntargets > PROBE_MAX_HOSTS ? PROBE_MAX_HOSTS : ntargets;
    return 220 + n * 20 + 12;
}

/* ---------- drawing primitives ---------- */
static GpGraphics *gp(HDC dc) {
    GpGraphics *g = NULL;
    GdipCreateFromHDC(dc, &g);
    GdipSetSmoothingMode(g, 4);
    return g;
}

static void round_rect_path(GpPath *p, float x, float y, float w, float h, float r) {
    float d = r * 2;
    if (d > h) d = h;
    if (d > w) d = w;
    GdipAddPathArc(p, x, y, d, d, 180, 90);
    GdipAddPathArc(p, x + w - d, y, d, d, 270, 90);
    GdipAddPathArc(p, x + w - d, y + h - d, d, d, 0, 90);
    GdipAddPathArc(p, x, y + h - d, d, d, 90, 90);
    GdipClosePathFigure(p);
}

/* border < 0: none */
static void fill_round(HDC dc, RECT r, float rad, COLORREF fill, long border, float bw) {
    GpGraphics *g = gp(dc);
    GpPath *p;
    GpBrush *b;
    float inset = border >= 0 ? bw / 2 : 0;
    GdipCreatePath(0, &p);
    round_rect_path(p, r.left + inset, r.top + inset, (r.right - r.left) - 2 * inset - 1,
                    (r.bottom - r.top) - 2 * inset - 1, SF(rad));
    GdipCreateSolidFill(AR(fill, 255), &b);
    GdipFillPath(g, b, p);
    GdipDeleteBrush(b);
    if (border >= 0) {
        GpPen *pen;
        GdipCreatePen1(AR((COLORREF)border, 255), bw, 2, &pen);
        GdipDrawPath(g, pen, p);
        GdipDeletePen(pen);
    }
    GdipDeletePath(p);
    GdipDeleteGraphics(g);
}

static void focus_ring(HDC dc, RECT r, float rad) {
    GpGraphics *g = gp(dc);
    GpPath *p;
    GpPen *pen;
    float w = SF(2);
    GdipCreatePath(0, &p);
    round_rect_path(p, r.left + w / 2, r.top + w / 2, (r.right - r.left) - w - 1,
                    (r.bottom - r.top) - w - 1, SF(rad));
    GdipCreatePen1(AR(P->focus, 255), w, 2, &pen);
    GdipDrawPath(g, pen, p);
    GdipDeletePen(pen);
    GdipDeletePath(p);
    GdipDeleteGraphics(g);
}

enum { GL_CHECK, GL_BANG, GL_PAUSE, GL_PLAY, GL_CROSS, GL_DASH, GL_SHIELD, GL_NOWIFI, GL_UP };

/* Line glyph centred on (cx, cy); s = glyph size, t = stroke width. */
static void glyph(HDC dc, int kind, float cx, float cy, float s, float t, COLORREF col) {
    GpGraphics *g = gp(dc);
    GpPen *pen;
    GpBrush *b;
    GdipCreatePen1(AR(col, 255), t, 2, &pen);
    GdipSetPenStartCap(pen, 2);
    GdipSetPenEndCap(pen, 2);
    GdipSetPenLineJoin(pen, 2);
    switch (kind) {
    case GL_CHECK: {
        GpPointF pts[3] = { { cx - s * 0.45f, cy + s * 0.02f }, { cx - s * 0.12f, cy + s * 0.33f },
                            { cx + s * 0.45f, cy - s * 0.32f } };
        GdipDrawLines(g, pen, pts, 3);
        break;
    }
    case GL_BANG:
        GdipDrawLine(g, pen, cx, cy - s * 0.45f, cx, cy + s * 0.12f);
        GdipCreateSolidFill(AR(col, 255), &b);
        GdipFillEllipse(g, b, cx - t * 0.7f, cy + s * 0.42f - t * 0.7f, t * 1.4f, t * 1.4f);
        GdipDeleteBrush(b);
        break;
    case GL_PAUSE:
        GdipDrawLine(g, pen, cx - s * 0.2f, cy - s * 0.4f, cx - s * 0.2f, cy + s * 0.4f);
        GdipDrawLine(g, pen, cx + s * 0.2f, cy - s * 0.4f, cx + s * 0.2f, cy + s * 0.4f);
        break;
    case GL_PLAY: {
        GpPointF pts[3] = { { cx - s * 0.3f, cy - s * 0.42f }, { cx + s * 0.42f, cy },
                            { cx - s * 0.3f, cy + s * 0.42f } };
        GdipCreateSolidFill(AR(col, 255), &b);
        GdipFillPolygon(g, b, pts, 3, 0);
        GdipDeleteBrush(b);
        break;
    }
    case GL_CROSS:
        GdipDrawLine(g, pen, cx - s * 0.36f, cy - s * 0.36f, cx + s * 0.36f, cy + s * 0.36f);
        GdipDrawLine(g, pen, cx + s * 0.36f, cy - s * 0.36f, cx - s * 0.36f, cy + s * 0.36f);
        break;
    case GL_DASH:
        GdipDrawLine(g, pen, cx - s * 0.3f, cy, cx + s * 0.3f, cy);
        break;
    case GL_SHIELD: {
        GpPointF pts[6] = { { cx, cy - s * 0.5f }, { cx + s * 0.42f, cy - s * 0.32f },
                            { cx + s * 0.36f, cy + s * 0.12f }, { cx, cy + s * 0.5f },
                            { cx - s * 0.36f, cy + s * 0.12f }, { cx - s * 0.42f, cy - s * 0.32f } };
        GpPointF closed[7];
        memcpy(closed, pts, sizeof(pts));
        closed[6] = pts[0];
        GdipDrawLines(g, pen, closed, 7);
        break;
    }
    case GL_NOWIFI:
        GdipDrawArc(g, pen, cx - s * 0.5f, cy - s * 0.3f, s, s, 225, 90);
        GdipDrawArc(g, pen, cx - s * 0.28f, cy - s * 0.04f, s * 0.56f, s * 0.56f, 225, 90);
        GdipCreateSolidFill(AR(col, 255), &b);
        GdipFillEllipse(g, b, cx - t * 0.7f, cy + s * 0.36f - t * 0.7f, t * 1.4f, t * 1.4f);
        GdipDeleteBrush(b);
        GdipDrawLine(g, pen, cx - s * 0.45f, cy - s * 0.45f, cx + s * 0.45f, cy + s * 0.45f);
        break;
    case GL_UP:
        GdipDrawLine(g, pen, cx, cy + s * 0.4f, cx, cy - s * 0.4f);
        GdipDrawLine(g, pen, cx - s * 0.32f, cy - s * 0.08f, cx, cy - s * 0.4f);
        GdipDrawLine(g, pen, cx + s * 0.32f, cy - s * 0.08f, cx, cy - s * 0.4f);
        break;
    }
    GdipDeletePen(pen);
    GdipDeleteGraphics(g);
}

static int state_glyph(state_kind_t k) {
    switch (k) {
    case ST_ACTIVE:  return GL_CHECK;
    case ST_PARTIAL: return GL_BANG;
    case ST_STANDBY: return GL_SHIELD;
    case ST_PAUSED:  return GL_PAUSE;
    case ST_OFFLINE: return GL_NOWIFI;
    case ST_ERROR:   return GL_CROSS;
    default:         return -1;   /* scanning: spinner only */
    }
}

static void text(HDC dc, HFONT f, COLORREF c, RECT r, const wchar_t *s, UINT flags) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s, -1, &r, flags | DT_NOPREFIX);
}

/* Centered, wrapped text with balanced lines (like CSS text-wrap: balance):
 * the narrowest width that keeps the same line count, so a short last
 * word is never left alone on its own line. */
static void text_balanced(HDC dc, HFONT f, COLORREF c, RECT r, const wchar_t *s) {
    const UINT fl = DT_CENTER | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX;
    int full = r.right - r.left, lo, hi, h0;
    RECT m = { 0, 0, full, 0 };
    SelectObject(dc, f);
    DrawTextW(dc, s, -1, &m, fl | DT_CALCRECT);
    h0 = m.bottom;
    lo = full / 2;
    hi = full;
    if (h0 > 0) {
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        if (h0 > tm.tmHeight) {               /* wraps: search the narrowest width */
            while (hi - lo > 2) {
                int mid = (lo + hi) / 2;
                RECT t = { 0, 0, mid, 0 };
                DrawTextW(dc, s, -1, &t, fl | DT_CALCRECT);
                if (t.bottom > h0) lo = mid; else hi = mid;
            }
            r.left += (full - hi) / 2;
            r.right = r.left + hi;
        }
    }
    text(dc, f, c, r, s, fl | DT_END_ELLIPSIS);
}

static RECT rc(int x, int y, int w, int h) {
    RECT r = { S(x), S(y), S(x + w), S(y + h) };
    return r;
}

static void hline(HDC dc, int x, int y, int w) {
    RECT r = { S(x), S(y), S(x + w), S(y) + (S(1) > 0 ? S(1) : 1) };
    HBRUSH b = CreateSolidBrush(P->border);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

/* ---------- fonts ---------- */
static int CALLBACK font_exists_cb(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lp) {
    (void)lf; (void)tm; (void)type;
    *(int *)lp = 1;
    return 0;
}

static const wchar_t *font_face(void) {
    static int checked = 0, variable = 0;
    if (!checked) {
        LOGFONTW lf;
        HDC dc = GetDC(NULL);
        ZeroMemory(&lf, sizeof(lf));
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy(lf.lfFaceName, L"Segoe UI Variable Text");
        EnumFontFamiliesExW(dc, &lf, font_exists_cb, (LPARAM)&variable, 0);
        ReleaseDC(NULL, dc);
        checked = 1;
    }
    return variable ? L"Segoe UI Variable Text" : L"Segoe UI";
}

static HFONT mkfont(int px, int weight) {
    return CreateFontW(-S(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, font_face());
}

static void fonts_create(void) {
    HFONT *all[] = { &f_display, &f_title, &f_body, &f_caption, &f_label, &f_button };
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++)
        if (*all[i]) DeleteObject(*all[i]);
    f_display = mkfont(24, FW_SEMIBOLD);
    f_title   = mkfont(16, FW_SEMIBOLD);
    f_body    = mkfont(14, FW_NORMAL);
    f_caption = mkfont(12, FW_NORMAL);
    f_label   = mkfont(12, FW_SEMIBOLD);
    f_button  = mkfont(14, FW_SEMIBOLD);   /* primary button: body size, semibold */
    if (g_edit) SendMessageW(g_edit, WM_SETFONT, (WPARAM)f_body, TRUE);
}

/* ---------- layout ---------- */
static RECT a11y_rect(int i, int sy) {
    switch (i) {
    case A_STATUS:      return rc(24, 258, 352, 166);
    case A_DETAILS:     return rc(24, 62, 256, 120);
    case A_TARGETS:     return rc(24, 192, 224, sy - 6 - 192 > 20 ? sy - 6 - 192 : 20);
    case A_SITE_LABEL:  return rc(24, sy, 256, 20);
    case A_SITE_RESULT: return rc(24, sy + 62, 352, 52);
    default:            return rc(24, 334, 352, 104);
    }
}

static int a11y_page(int i) {
    return i == A_STATUS ? PG_STATUS : i == A_SETTINGS ? PG_SETTINGS : PG_DETAILS;
}

static void a11y_set(int i, const wchar_t *text) {
    wchar_t cur[600];
    if (!g_a11y[i]) return;
    GetWindowTextW(g_a11y[i], cur, 600);
    if (wcscmp(cur, text)) SetWindowTextW(g_a11y[i], text);   /* raises a name-change event */
    ShowWindow(g_a11y[i], a11y_page(i) == g_page ? SW_SHOWNA : SW_HIDE);
}

/* The same words the paint_* functions draw, as sentences. */
static void a11y_update(const ui_state_t *st) {
    wchar_t b[600], line[200];
    size_t n;
    const wchar_t *sub = kind_sub(st);

    /* status page */
    _snwprintf(b, 599, L"Durum: %ls.%ls%ls", kind_headline(st), sub[0] ? L" " : L"", sub);
    b[599] = 0;
    if (st->kind == ST_ACTIVE || st->kind == ST_STANDBY || st->kind == ST_PARTIAL ||
        st->kind == ST_SCANNING || st->kind == ST_STARTING) {
        n = wcslen(b);
        _snwprintf(b + n, 599 - n, L" Ağ: %ls.", st->network[0] ? st->network : L"bilinmiyor");
        b[599] = 0;
        n = wcslen(b);
        if (st->kind == ST_SCANNING || st->kind == ST_STARTING) {
            if (st->scan_total > 0 && st->scan_pos > 0)
                _snwprintf(b + n, 599 - n, L" İlerleme: %d / %d.",
                           st->scan_pos > st->scan_total ? st->scan_total : st->scan_pos, st->scan_total);
            else
                _snwprintf(b + n, 599 - n, L" İlerleme: hazırlanıyor.");
        } else if (st->kind == ST_PARTIAL) {
            _snwprintf(b + n, 599 - n, L" Açılan siteler: %d / %d.", ok_count(st), st->ntargets);
        } else if (st->avg_ms > 0) {
            _snwprintf(b + n, 599 - n, L" Hız: %d ms, %ls.", st->avg_ms, speed_word(st->avg_ms));
        }
        b[599] = 0;
    }
    a11y_set(A_STATUS, b);

    /* details: rows */
    if (st->preset >= 0 && st->preset < PRESET_COUNT)
        _snwprintf(b, 599, L"Yöntem: %ls.", PRESETS[st->preset].name);
    else
        wcscpy(b, L"Yöntem: yok.");
    b[599] = 0;
    n = wcslen(b);
    if (st->preset >= 0 && st->reason[0])
        _snwprintf(b + n, 599 - n, L" Mod: %ls, %ls.", g_auto ? L"otomatik" : L"elle", st->reason);
    else
        _snwprintf(b + n, 599 - n, L" Mod: %ls.", g_auto ? L"otomatik" : L"elle seçildi");
    b[599] = 0;
    n = wcslen(b);
    if (st->preset >= 0 && st->dns >= 0 && st->dns < DNS_SERVER_COUNT) {
        wchar_t ip[32];
        MultiByteToWideChar(CP_ACP, 0, DNS_SERVERS[st->dns].v4, -1, ip, 32);
        ip[31] = 0;
        if (st->dns_ms > 0)
            _snwprintf(b + n, 599 - n, L" DNS: %ls%ls, %d ms.", st->hybrid ? L"hibrit, " : L"", ip, st->dns_ms);
        else
            _snwprintf(b + n, 599 - n, L" DNS: %ls%ls.", st->hybrid ? L"hibrit, " : L"", ip);
    } else {
        _snwprintf(b + n, 599 - n, L" DNS: yok.");
    }
    b[599] = 0;
    n = wcslen(b);
    if (st->avg_ms > 0)
        _snwprintf(b + n, 599 - n, L" Bağlantı: %d ms, TCP ve TLS ortalaması.", st->avg_ms);
    b[599] = 0;
    n = wcslen(b);
    _snwprintf(b + n, 599 - n, L" Öğrenilen: %d / %d yöntem bu ağda denendi.", st->learned, PRESET_COUNT);
    b[599] = 0;
    a11y_set(A_DETAILS, b);

    /* details: test sites */
    wcscpy(b, L"Test siteleri:");
    for (int i = 0; i < st->ntargets && i < PROBE_MAX_HOSTS; i++) {
        int ok = st->target_ok[i];
        if (ok < 0) _snwprintf(line, 199, L" %ls, denenmedi;", st->target[i]);
        else if (ok) _snwprintf(line, 199, L" %ls, açık, %d ms;", st->target[i], st->target_ms[i]);
        else _snwprintf(line, 199, L" %ls, engelli;", st->target[i]);
        line[199] = 0;
        if (wcslen(b) + wcslen(line) < 599) wcscat(b, line);
    }
    if (!st->ntargets) wcscat(b, L" yok.");
    a11y_set(A_TARGETS, b);

    a11y_set(A_SITE_LABEL, L"Site kontrolü, site adresi");
    _snwprintf(b, 599, L"%ls", st->site_result[0] ? st->site_result
               : L"Açılmayan bir siteyi yazın; TurkishDPI uygun yöntemi arar.");
    b[599] = 0;
    if (st->st_packets) {
        n = wcslen(b);
        _snwprintf(b + n, 599 - n, L" İstatistik: %lu paket, %lu HTTPS, %lu HTTP, %lu DNS, %lu ödeme atlandı.",
                   st->st_packets, st->st_https, st->st_http, st->st_dns, st->st_excluded);
        b[599] = 0;
    }
    a11y_set(A_SITE_RESULT, b);

    a11y_set(A_SETTINGS, L"TurkishDPI " TURKISHDPI_VERSION_W L", Apache-2.0, kendi motoru, Shroudly çekirdeğinden. "
             L"Kaldırma: TurkishDPI durdurulur; başlangıç görevi, ayarlar, öğrenilen bilgiler, günlük ve "
             L"sürücü bu bilgisayardan silinir.");
}

static void layout(void) {
    int sy = details_site_y(g_layout_n);
    ctl(ID_TEST)->y = sy + 24;
    for (int i = 0; i < NCTL; i++) {
        RECT r = ctl_rect(&g_ctl[i]);
        if (g_ctl[i].hwnd)
            MoveWindow(g_ctl[i].hwnd, r.left, r.top, r.right - r.left, r.bottom - r.top, FALSE);
    }
    for (int i = 0; i < A_COUNT; i++) {
        RECT r = a11y_rect(i, sy);
        if (g_a11y[i]) MoveWindow(g_a11y[i], r.left, r.top, r.right - r.left, r.bottom - r.top, FALSE);
    }
    if (g_edit) {
        RECT r = rc(36, sy + 24, 236, 32);
        int fh = S(20);
        MoveWindow(g_edit, r.left, r.top + (r.bottom - r.top - fh) / 2, r.right - r.left, fh, FALSE);
    }
    RedrawWindow(g_wnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

/* Texts, visibility and positions that depend on the state. Cheap; called
 * on every status update while the window is visible and on page change. */
static void update_controls(void) {
    ui_state_t st;
    wchar_t buf[200], cur[200];
    int others, bk, act;
    HWND focus = GetFocus();

    if (!g_wnd) return;
    snapshot(&st);
    if (st.ntargets != g_layout_n) { g_layout_n = st.ntargets; layout(); }
    bk = banner_kind(&st, &others);
    act = action_for(&st);

    for (int i = 0; i < NCTL; i++) {
        ctl_t *c = &g_ctl[i];
        int show = c->page == PG_ALL || c->page == g_page;
        buf[0] = 0;
        switch (c->id) {
        case ID_TAB0: case ID_TAB1: case ID_TAB2: {
            static const wchar_t *names[] = { L"Durum", L"Ayrıntılar", L"Ayarlar" };
            int p = c->id - ID_TAB0;
            _snwprintf(buf, 199, p == g_page ? L"%ls (seçili)" : L"%ls", names[p]);
            break;
        }
        case ID_POWER:
            wcscpy(buf, st.kind == ST_PAUSED ? L"Devam et" : L"Duraklat");
            break;
        case ID_ACTION:
            wcscpy(buf, action_label(act));
            c->kind = act == A_RESUME ? K_PRIMARY : K_SECONDARY;
            show = show && act != A_NONE;
            break;
        case ID_BANNER:
            banner_text(&st, bk, buf, 200);
            show = show && bk != 0;
            break;
        case ID_PAYLINE:
            if (g_payment_shown)
                _snwprintf(buf, 199, L"Ödeme koruması açık · %d+ banka ve ödeme sitesi hariç",
                           g_payment_domains);
            else
                wcscpy(buf, L"Ödeme koruması kapalı");
            break;
        case ID_CHANGE:    wcscpy(buf, L"Değiştir ▾"); break;
        case ID_RESCAN:    wcscpy(buf, L"⟳  Yeniden tara"); break;
        case ID_TEST:      wcscpy(buf, L"Kontrol et"); break;
        case ID_T_AUTO:
            wcscpy(buf, g_auto ? L"Otomatik yöntem seçimi: açık" : L"Otomatik yöntem seçimi: kapalı");
            break;
        case ID_T_START:
            wcscpy(buf, ui_autostart() ? L"Windows açılışında başlat: açık"
                                       : L"Windows açılışında başlat: kapalı");
            break;
        case ID_T_PAY:
            wcscpy(buf, g_payment_shown ? L"Ödeme koruması: açık" : L"Ödeme koruması: kapalı");
            break;
        case ID_L_LOG:     wcscpy(buf, L"Günlüğü aç"); break;
        case ID_L_INI:     wcscpy(buf, L"Gelişmiş ayarlar"); break;
        case ID_L_AV:      wcscpy(buf, L"Antivirüs yardımı"); break;
        case ID_L_GH:      wcscpy(buf, L"GitHub sayfası"); break;
        case ID_UNINSTALL: wcscpy(buf, L"Kaldır ve temizle…"); break;
        case ID_EXIT:      wcscpy(buf, L"Çıkış"); break;
        }
        buf[199] = 0;
        GetWindowTextW(c->hwnd, cur, 200);
        if (wcscmp(cur, buf)) SetWindowTextW(c->hwnd, buf);   /* also tells screen readers */
        if (!show && focus == c->hwnd) focus = NULL;
        ShowWindow(c->hwnd, show ? SW_SHOWNA : SW_HIDE);
    }
    ShowWindow(g_edit, g_page == PG_DETAILS ? SW_SHOWNA : SW_HIDE);
    a11y_update(&st);
    if (!focus && GetForegroundWindow() == g_wnd) SetFocus(ctl(ID_TAB0 + g_page)->hwnd);
    RedrawWindow(g_wnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

static void spinner_update(void) {
    BOOL anim = TRUE;
    ui_state_t st;
    int want;
    snapshot(&st);
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0);
    want = anim && !g_preview_shot && IsWindowVisible(g_wnd) && !IsIconic(g_wnd) && g_page == PG_STATUS &&
           (st.kind == ST_SCANNING || st.kind == ST_STARTING);
    if (want && !g_spinning) SetTimer(g_wnd, 1, 50, NULL);
    if (!want && g_spinning) KillTimer(g_wnd, 1);
    g_spinning = want;
}

static void show_page(int p) {
    g_page = p;
    update_controls();
    spinner_update();
}

/* ---------- painting: parent (static content) ---------- */
static void paint_header(HDC dc) {
    DrawIconEx(dc, S(24), S(16), g_logo, S(24), S(24), 0, NULL, DI_NORMAL);
    text(dc, f_title, P->text, rc(56, 14, 90, 28), L"TurkishDPI", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    fill_round(dc, rc(148, 12, 228, 32), 8, P->surface, -1, 0);
}

static void info_row(HDC dc, int y, const wchar_t *label, const wchar_t *value, COLORREF vcol, int vw) {
    text(dc, f_label, P->text3, rc(24, y, 92, 24), label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    text(dc, f_body, vcol, rc(116, y, vw, 24), value, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void paint_status(HDC dc, const ui_state_t *st) {
    const wchar_t *sub = kind_sub(st);
    int err = st->kind == ST_ERROR;
    wchar_t buf[160];

    text(dc, f_display, P->text, rc(24, 258, 352, 34), kind_headline(st),
         DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    /* errors carry an instruction: allow three lines and drop the card */
    text_balanced(dc, f_body, P->text2, rc(40, 294, 320, err ? 84 : 40), sub);

    if (st->kind == ST_ACTIVE || st->kind == ST_STANDBY || st->kind == ST_PARTIAL ||
        st->kind == ST_SCANNING || st->kind == ST_STARTING) {
        RECT card = rc(24, 344, 352, 80);
        fill_round(dc, card, 12, P->surface, (long)P->border, SF(1));
        text(dc, f_label, P->text3, rc(40, 360, 110, 24), L"Ağ", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        text(dc, f_body, P->text, rc(150, 360, 210, 24), st->network[0] ? st->network : L"—",
             DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (st->kind == ST_SCANNING || st->kind == ST_STARTING) {
            text(dc, f_label, P->text3, rc(40, 384, 110, 24), L"İlerleme",
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            if (st->scan_total > 0 && st->scan_pos > 0) {
                RECT track = rc(150, 393, 150, 6), fillr = track;
                int pos = st->scan_pos > st->scan_total ? st->scan_total : st->scan_pos;
                fill_round(dc, track, 3, P->border, -1, 0);
                fillr.right = fillr.left + (track.right - track.left) * pos / st->scan_total;
                if (fillr.right - fillr.left >= S(6)) fill_round(dc, fillr, 3, P->busy, -1, 0);
                _snwprintf(buf, 31, L"%d / %d", pos, st->scan_total);
                buf[31] = 0;
                text(dc, f_body, P->text, rc(304, 384, 56, 24), buf, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            } else {
                text(dc, f_body, P->text2, rc(150, 384, 210, 24), L"Hazırlanıyor",
                     DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }
        } else if (st->kind == ST_PARTIAL) {
            text(dc, f_label, P->text3, rc(40, 384, 110, 24), L"Açılan siteler",
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            _snwprintf(buf, 31, L"%d / %d", ok_count(st), st->ntargets);
            buf[31] = 0;
            text(dc, f_body, P->text, rc(150, 384, 210, 24), buf, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        } else {
            text(dc, f_label, P->text3, rc(40, 384, 110, 24), L"Hız", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            if (st->avg_ms > 0) {
                _snwprintf(buf, 63, L"%d ms · %ls", st->avg_ms, speed_word(st->avg_ms));
                buf[63] = 0;
            } else {
                wcscpy(buf, L"—");
            }
            text(dc, f_body, P->text, rc(150, 384, 210, 24), buf, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

static void paint_details(HDC dc, const ui_state_t *st) {
    wchar_t buf[200];
    int sy = details_site_y(st->ntargets);

    if (st->preset >= 0 && st->preset < PRESET_COUNT)
        info_row(dc, 62, L"Yöntem", PRESETS[st->preset].name, P->text, 164);
    else
        info_row(dc, 62, L"Yöntem", L"—", P->text2, 164);
    if (st->preset >= 0 && st->reason[0]) {
        _snwprintf(buf, 199, L"%ls · %ls", g_auto ? L"Otomatik" : L"Elle", st->reason);
        buf[199] = 0;
        info_row(dc, 86, L"Mod", buf, P->text, 260);
    } else {
        info_row(dc, 86, L"Mod", g_auto ? L"Otomatik" : L"Elle seçildi", P->text, 260);
    }
    if (st->preset >= 0 && st->dns >= 0 && st->dns < DNS_SERVER_COUNT) {
        wchar_t ip[32];
        MultiByteToWideChar(CP_ACP, 0, DNS_SERVERS[st->dns].v4, -1, ip, 32);
        ip[31] = 0;
        if (st->dns_ms > 0)
            _snwprintf(buf, 199, L"%ls%ls · %d ms", st->hybrid ? L"Hibrit · " : L"", ip, st->dns_ms);
        else
            _snwprintf(buf, 199, L"%ls%ls", st->hybrid ? L"Hibrit · " : L"", ip);
        buf[199] = 0;
        info_row(dc, 110, L"DNS", buf, P->text, 260);
    } else {
        info_row(dc, 110, L"DNS", L"—", P->text2, 260);
    }
    if (st->avg_ms > 0) {
        _snwprintf(buf, 199, L"%d ms (TCP+TLS ortalaması)", st->avg_ms);
        buf[199] = 0;
        info_row(dc, 134, L"Bağlantı", buf, P->text, 260);
    } else {
        info_row(dc, 134, L"Bağlantı", L"—", P->text2, 260);
    }
    _snwprintf(buf, 199, L"%d / %d yöntem bu ağda denendi", st->learned, PRESET_COUNT);
    buf[199] = 0;
    info_row(dc, 158, L"Öğrenilen", buf, P->text, 260);

    hline(dc, 24, 186, 352);
    text(dc, f_label, P->text3, rc(24, 192, 200, 24), L"Test siteleri", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    for (int i = 0; i < st->ntargets && i < PROBE_MAX_HOSTS; i++) {
        int y = 220 + i * 20;
        int ok = st->target_ok[i];
        COLORREF gc = ok < 0 ? P->text3 : ok ? P->ok : P->error;
        glyph(dc, ok < 0 ? GL_DASH : ok ? GL_CHECK : GL_CROSS, SF(31), SF(y + 10.0f), SF(10), SF(1.5f), gc);
        text(dc, f_body, P->text, rc(44, y, 240, 20), st->target[i],
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (ok < 0) wcscpy(buf, L"—");
        else if (ok) { _snwprintf(buf, 31, L"%d ms", st->target_ms[i]); buf[31] = 0; }
        else wcscpy(buf, L"engelli");
        text(dc, f_body, ok == 0 ? P->error : P->text2, rc(288, y, 88, 20), buf,
             DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    if (!st->ntargets)
        text(dc, f_body, P->text2, rc(24, 220, 352, 20), L"Test sitesi yok", DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    hline(dc, 24, sy - 6, 352);
    text(dc, f_label, P->text3, rc(24, sy, 352, 20), L"Site kontrolü", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    fill_round(dc, rc(24, sy + 24, 256, 32), 8, P->sunken,
               (long)(GetFocus() == g_edit ? P->focus : P->border), SF(1));
    text(dc, f_caption, P->text2, rc(24, sy + 62, 352, 34),
         st->site_result[0] ? st->site_result : L"Açılmayan bir siteyi yazın; TurkishDPI uygun yöntemi arar.",
         DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS | DT_EDITCONTROL);
    if (st->st_packets) {
        _snwprintf(buf, 199, L"%lu paket · %lu HTTPS · %lu HTTP · %lu DNS · %lu ödeme atlandı",
                   st->st_packets, st->st_https, st->st_http, st->st_dns, st->st_excluded);
        buf[199] = 0;
        text(dc, f_caption, P->text3, rc(24, 496, 352, 18), buf, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

static void paint_settings(HDC dc) {
    hline(dc, 24, 240, 352);
    hline(dc, 24, 324, 352);
    text(dc, f_caption, P->text3, rc(24, 334, 352, 20),
         L"TurkishDPI " TURKISHDPI_VERSION_W L" · Apache-2.0 · Shroudly çekirdeğinden",
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    hline(dc, 24, 364, 352);
    text(dc, f_label, P->text3, rc(24, 376, 352, 20), L"Kaldırma", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    text(dc, f_caption, P->text2, rc(24, 398, 352, 40),
         L"TurkishDPI durdurulur; başlangıç görevi, ayarlar, öğrenilen bilgiler, günlük ve "
         L"sürücü bu bilgisayardan silinir.", DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL);
}

static void paint(HDC dc, RECT client) {
    ui_state_t st;
    HBRUSH bg = CreateSolidBrush(P->bg);
    snapshot(&st);
    FillRect(dc, &client, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    paint_header(dc);
    if (g_page == PG_STATUS) paint_status(dc, &st);
    else if (g_page == PG_DETAILS) paint_details(dc, &st);
    else paint_settings(dc);
}

/* ---------- painting: owner-drawn buttons ---------- */
static const wchar_t *toggle_title(int id) {
    return id == ID_T_AUTO ? L"Otomatik yöntem seçimi"
         : id == ID_T_START ? L"Windows açılışında başlat" : L"Ödeme koruması";
}

static const wchar_t *toggle_desc(int id) {
    return id == ID_T_AUTO ? L"En iyi yöntemi kendisi bulur (önerilen)."
         : id == ID_T_START ? L"Oturum açınca tepside sessizce başlar."
         : L"Banka ve ödeme sitelerinin trafiğine hiç dokunulmaz.";
}

static int toggle_on(int id) {
    return id == ID_T_AUTO ? g_auto : id == ID_T_START ? ui_autostart() : g_payment_shown;
}

static void draw_ring(HDC dc, RECT r, const ui_state_t *st, int hot, int focus, int pressed) {
    COLORREF col = state_color(st->kind);
    float cx = (r.left + r.right) / 2.0f, cy = (r.top + r.bottom) / 2.0f;
    float k = pressed ? 0.96f : 1.0f;              /* press feedback */
    float rad = SF(60) * k, halo = SF(72) * k;
    GpGraphics *g = gp(dc);
    GpBrush *b;
    GpPen *pen;
    int spin = st->kind == ST_SCANNING || st->kind == ST_STARTING;
    int gl;

    GdipCreateSolidFill(AR(col, 26), &b);
    GdipFillEllipse(g, b, cx - halo, cy - halo, halo * 2, halo * 2);
    GdipDeleteBrush(b);
    GdipCreateSolidFill(AR(hot ? P->hover : P->surface, 255), &b);
    GdipFillEllipse(g, b, cx - rad, cy - rad, rad * 2, rad * 2);
    GdipDeleteBrush(b);
    GdipCreatePen1(AR(col, 255), SF(4), 2, &pen);
    GdipSetPenStartCap(pen, 2);
    GdipSetPenEndCap(pen, 2);
    if (spin) {
        GdipDrawArc(g, pen, cx - rad + SF(2), cy - rad + SF(2), (rad - SF(2)) * 2, (rad - SF(2)) * 2,
                    (float)g_anim, 100);
        GdipDrawArc(g, pen, cx - rad + SF(2), cy - rad + SF(2), (rad - SF(2)) * 2, (rad - SF(2)) * 2,
                    (float)g_anim + 180, 100);
    } else {
        GdipDrawEllipse(g, pen, cx - rad + SF(2), cy - rad + SF(2), (rad - SF(2)) * 2, (rad - SF(2)) * 2);
    }
    GdipDeletePen(pen);
    if (focus) {
        GdipCreatePen1(AR(P->focus, 255), SF(2), 2, &pen);
        GdipDrawEllipse(g, pen, cx - rad - SF(6), cy - rad - SF(6), (rad + SF(6)) * 2, (rad + SF(6)) * 2);
        GdipDeletePen(pen);
    }
    GdipDeleteGraphics(g);

    /* hover shows what a click does; otherwise the state glyph */
    gl = hot ? (st->kind == ST_PAUSED ? GL_PLAY : GL_PAUSE) : state_glyph(st->kind);
    if (gl >= 0) {
        glyph(dc, gl, cx, cy, SF(40) * k, SF(5), hot ? P->text : col);
    } else if (st->scan_total > 0 && st->scan_pos > 0) {
        wchar_t buf[16];
        RECT tr = { (LONG)(cx - SF(50)), (LONG)(cy - SF(14)), (LONG)(cx + SF(50)), (LONG)(cy + SF(14)) };
        _snwprintf(buf, 15, L"%d/%d", st->scan_pos, st->scan_total);
        buf[15] = 0;
        text(dc, f_title, P->text, tr, buf, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

static void draw_item(const DRAWITEMSTRUCT *d) {
    ctl_t *c = ctl((int)d->CtlID);
    RECT r = { 0, 0, d->rcItem.right - d->rcItem.left, d->rcItem.bottom - d->rcItem.top }, v;
    HDC mem;
    HBITMAP bmp, old;
    HBRUSH under;
    wchar_t label[200];
    int hot, pressed, focus;
    COLORREF under_col;

    if (!c) return;
    hot = g_hot == c->id;
    pressed = (d->itemState & ODS_SELECTED) != 0 || c->id == g_preview_press;
    focus = ((d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT)) ||
            c->id == g_preview_focus;
    GetWindowTextW(d->hwndItem, label, 200);

    mem = CreateCompatibleDC(d->hDC);
    bmp = CreateCompatibleBitmap(d->hDC, r.right, r.bottom);
    old = SelectObject(mem, bmp);
    under_col = c->kind == K_TAB ? P->surface : P->bg;
    under = CreateSolidBrush(under_col);
    FillRect(mem, &r, under);
    DeleteObject(under);
    SetBkMode(mem, TRANSPARENT);

    /* v = visible shape. Tabs and small buttons get a taller hit area than
     * they look (control rect >= 32 px); pressed buttons shrink to 96 %. */
    v = r;
    if (c->kind == K_TAB) InflateRect(&v, 0, -S(2));
    if (c->kind == K_SMALL) InflateRect(&v, 0, -S(4));
    if (pressed && c->kind != K_TOGGLE && c->kind != K_LINK && c->kind != K_PAYLINE &&
        c->kind != K_BANNER && c->kind != K_RING)
        InflateRect(&v, -(int)((v.right - v.left) * 0.02f + 0.5f), -(int)((v.bottom - v.top) * 0.02f + 0.5f));

    switch (c->kind) {
    case K_TAB: {
        static const wchar_t *names[] = { L"Durum", L"Ayrıntılar", L"Ayarlar" };
        int sel = c->id - ID_TAB0 == g_page;
        if (sel || hot) fill_round(mem, v, 6, sel ? P->hover : mix(P->surface, P->hover, 50), -1, 0);
        text(mem, sel ? f_label : f_body, sel ? P->text : P->text2, v, names[c->id - ID_TAB0],
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (focus) focus_ring(mem, v, 6);
        break;
    }
    case K_RING: {
        ui_state_t st;
        snapshot(&st);
        draw_ring(mem, r, &st, hot, focus, pressed);
        break;
    }
    case K_PRIMARY:
        fill_round(mem, v, 8, hot ? P->accent_hover : P->accent, -1, 0);
        text(mem, f_button, P->on_accent, v, label, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (focus) focus_ring(mem, v, 8);
        break;
    case K_SECONDARY:
    case K_SMALL:
    case K_DANGER:
        fill_round(mem, v, c->kind == K_SMALL ? 6 : 8, hot ? P->hover : P->surface, (long)P->border, SF(1));
        text(mem, c->kind == K_SMALL ? f_caption : f_body, c->kind == K_DANGER ? P->error : P->text,
             v, label, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (focus) focus_ring(mem, v, c->kind == K_SMALL ? 6 : 8);
        break;
    case K_LINK: {
        RECT tr = r;
        SIZE sz;
        tr.left += S(4);
        text(mem, f_body, P->focus, tr, label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(mem, f_body);
        GetTextExtentPoint32W(mem, label, (int)wcslen(label), &sz);
        if (hot) {
            RECT ul = { tr.left, (r.bottom + sz.cy) / 2, tr.left + sz.cx, (r.bottom + sz.cy) / 2 + S(1) };
            HBRUSH b = CreateSolidBrush(P->focus);
            FillRect(mem, &ul, b);
            DeleteObject(b);
        }
        if (focus) focus_ring(mem, r, 6);
        break;
    }
    case K_TOGGLE: {
        int on = toggle_on(c->id);
        RECT tr = { S(0), S(8), r.right - S(64), S(30) };
        RECT dr = { S(0), S(30), r.right - S(64), r.bottom - S(4) };
        RECT track = { r.right - S(48), (r.bottom - S(24)) / 2, r.right - S(4), (r.bottom - S(24)) / 2 + S(24) };
        float kx, ky = (track.top + track.bottom) / 2.0f, kr = SF(8);
        GpGraphics *g;
        GpBrush *b;
        if (hot) fill_round(mem, r, 8, mix(P->bg, P->surface, 60), -1, 0);
        tr.left = dr.left = S(8);
        text(mem, f_body, P->text, tr, toggle_title(c->id), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        text(mem, f_caption, P->text2, dr, toggle_desc(c->id), DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
        if (on) fill_round(mem, track, 12, P->toggle_on, -1, 0);
        else fill_round(mem, track, 12, P->bg, (long)P->text3, SF(1.5f));
        kx = on ? track.right - SF(12) : track.left + SF(12);
        g = gp(mem);
        GdipCreateSolidFill(AR(on ? RGB(0xFF, 0xFF, 0xFF) : P->text2, 255), &b);
        GdipFillEllipse(g, b, kx - kr, ky - kr, kr * 2, kr * 2);
        GdipDeleteBrush(b);
        GdipDeleteGraphics(g);
        if (focus) focus_ring(mem, r, 8);
        break;
    }
    case K_BANNER: {
        ui_state_t st;
        int others, bk;
        RECT tr = r;
        COLORREF col;
        snapshot(&st);
        bk = banner_kind(&st, &others);
        col = bk == 1 ? P->busy : P->warn;
        fill_round(mem, r, 8, mix(P->bg, col, hot ? 24 : 16), (long)mix(P->bg, col, 40), SF(1));
        glyph(mem, bk == 1 ? GL_UP : GL_BANG, SF(20), r.bottom / 2.0f, SF(14), SF(1.5f), col);
        tr.left = S(36);
        tr.right -= S(bk == 3 ? (others ? 40 : 12) : (others ? 96 : 64));
        text(mem, f_caption, P->text, tr, label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (bk != 3 || others) {
            wchar_t act[24];
            RECT ar = r;
            ar.right -= S(12);
            if (bk == 3) _snwprintf(act, 23, L"+%d", others);
            else if (others) _snwprintf(act, 23, L"+%d  %ls", others, bk == 1 ? update_action(&st) : L"Onar →");
            else _snwprintf(act, 23, L"%ls", bk == 1 ? update_action(&st) : L"Onar →");
            act[23] = 0;
            text(mem, f_label, bk == 3 ? P->text2 : P->focus, ar, act, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
        if (focus) focus_ring(mem, r, 8);
        break;
    }
    case K_PAYLINE: {
        RECT tr = r;
        COLORREF col = g_payment_shown ? P->ok : P->warn;
        glyph(mem, g_payment_shown ? GL_CHECK : GL_BANG, SF(8), r.bottom / 2.0f, SF(10), SF(1.5f), col);
        tr.left = S(20);
        text(mem, f_caption, g_payment_shown ? (hot ? P->text : P->text2) : P->warn, tr, label,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (focus) focus_ring(mem, r, 6);
        break;
    }
    }

    BitBlt(d->hDC, d->rcItem.left, d->rcItem.top, r.right, r.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

/* ---------- tray ---------- */
static void tray_update(void) {
    ui_state_t st;
    if (g_ui_preview) return;
    snapshot(&st);
    g_nid.uFlags = NIF_TIP | NIF_ICON | NIF_MESSAGE;
    g_nid.hIcon = g_tray_icons[tray_icon_index(st.kind)];
    if (st.network[0] && st.kind != ST_OFFLINE)
        _snwprintf(g_nid.szTip, 127, L"TurkishDPI · %ls · %ls", kind_headline(&st), st.network);
    else
        _snwprintf(g_nid.szTip, 127, L"TurkishDPI · %ls", kind_headline(&st));
    g_nid.szTip[127] = 0;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

/* 1 if the mouse pointer is on our tray icon (or the shell cannot tell). */
static int tray_click_genuine(void) {
    typedef HRESULT (WINAPI *rect_fn)(const NOTIFYICONIDENTIFIER *, RECT *);
    rect_fn f = (rect_fn)(void *)GetProcAddress(GetModuleHandleW(L"shell32.dll"), "Shell_NotifyIconGetRect");
    NOTIFYICONIDENTIFIER id;
    RECT r;
    POINT pt;
    if (!f) return 1;
    ZeroMemory(&id, sizeof(id));
    id.cbSize = sizeof(id);
    id.hWnd = g_wnd;
    id.uID = 1;
    if (FAILED(f(&id, &r)) || !GetCursorPos(&pt)) return 1;   /* e.g. icon hidden in the overflow */
    InflateRect(&r, 4, 4);
    return PtInRect(&r, pt);
}

/* NIM_ADD fails while Explorer is still starting (logon task) or busy; the
 * TaskbarCreated broadcast may already be over by then, so retry on a timer
 * (TIMER_TRAY, every 2 s for up to 2 minutes) instead of staying iconless. */
#define TIMER_TRAY 2
static int g_tray_tries;

static void tray_add(void) {
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_wnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = g_tray_icons[2];
    wcscpy(g_nid.szTip, L"TurkishDPI");
    if (!Shell_NotifyIconW(NIM_ADD, &g_nid)) {
        /* already there (e.g. a second TaskbarCreated): update it instead */
        if (!Shell_NotifyIconW(NIM_MODIFY, &g_nid)) {
            if (g_tray_tries++ < 60) SetTimer(g_wnd, TIMER_TRAY, 2000, NULL);
            else KillTimer(g_wnd, TIMER_TRAY);
            return;
        }
    }
    KillTimer(g_wnd, TIMER_TRAY);
    g_tray_tries = 0;
    g_nid.uVersion = NOTIFYICON_VERSION;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
}

/* ---------- menus ---------- */
static void dark_menus(void) {
    /* Undocumented but stable since Win10 1903: uxtheme #135 SetPreferredAppMode. */
    typedef int (WINAPI *set_mode_fn)(int);
    HMODULE ux = load_system_dll(L"uxtheme.dll");
    if (ux) {
        set_mode_fn f = (set_mode_fn)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(135));
        if (f) f(2);
    }
}

static void popup(HMENU m, int x, int y) {
    SetForegroundWindow(g_wnd);
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, x, y, 0, g_wnd, NULL);
    PostMessageW(g_wnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

static void profile_menu(void) {
    HMENU m = CreatePopupMenu();
    RECT r = ctl_rect(ctl(ID_CHANGE));
    POINT p = { r.left, r.bottom + S(4) };
    AppendMenuW(m, MF_STRING | (g_auto ? MF_CHECKED : 0), IDM_AUTO, L"Otomatik (önerilen)");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    for (int i = 0; i < PRESET_COUNT; i++)
        AppendMenuW(m, MF_STRING | ((!g_auto && g_preset == i) ? MF_CHECKED : 0),
                    IDM_PRESET + i, PRESETS[i].name);
    ClientToScreen(g_wnd, &p);
    popup(m, p.x, p.y);
}

void ui_tray_menu(HWND owner) {
    HMENU m = CreatePopupMenu();
    POINT p;
    ui_state_t st;
    (void)owner;
    snapshot(&st);
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, kind_headline(&st));
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | MF_DEFAULT, IDM_SHOW, L"TurkishDPI'ı aç");
    AppendMenuW(m, MF_STRING, IDM_PAUSE, g_paused ? L"Devam et" : L"Duraklat");
    AppendMenuW(m, MF_STRING, IDM_RESCAN, L"Yeniden tara");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_EXIT, L"Çıkış");
    GetCursorPos(&p);
    SetForegroundWindow(g_wnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, p.x, p.y, 0, g_wnd, NULL);
    PostMessageW(g_wnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

/* ---------- input ---------- */
static void do_site_test(void) {
    wchar_t buf[256];
    GetWindowTextW(g_edit, buf, 256);
    if (!buf[0]) return;
    if (g_ui_preview) { preview_action(L"Site testi"); return; }
    post_site_test(buf);
}

static LRESULT CALLBACK edit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN) { do_site_test(); return 0; }
    if (msg == WM_CHAR && (wp == L'\r' || wp == L'\n')) return 0;
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) InvalidateRect(g_wnd, NULL, FALSE);  /* focus border */
    return CallWindowProcW(g_edit_proc, h, msg, wp, lp);
}

/* hover tracking and hand cursor for the owner-drawn buttons */
static LRESULT CALLBACK btn_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    int id = GetDlgCtrlID(h);
    switch (msg) {
    case WM_MOUSEMOVE:
        if (g_hot != id) {
            TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, h, 0 };
            HWND prev = g_hot ? ctl(g_hot)->hwnd : NULL;
            g_hot = id;
            TrackMouseEvent(&t);
            if (prev) InvalidateRect(prev, NULL, FALSE);
            InvalidateRect(h, NULL, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        if (g_hot == id) { g_hot = 0; InvalidateRect(h, NULL, FALSE); }
        break;
    case WM_SETCURSOR:
        SetCursor(LoadCursor(NULL, IDC_HAND));
        return TRUE;
    case WM_ERASEBKGND:
        return 1;
    }
    return CallWindowProcW(g_btn_proc, h, msg, wp, lp);
}

static void command(int id);

static void do_click(int id) {
    ui_state_t st;
    snapshot(&st);
    /* page switches and menus are UI only; everything else is an action */
    switch (id) {
    case ID_TAB0: case ID_TAB1: case ID_TAB2: show_page(id - ID_TAB0); return;
    case ID_PAYLINE: show_page(PG_SETTINGS); return;
    case ID_CHANGE:  profile_menu(); return;
    case ID_ACTION:
        if (action_for(&st) == A_SITE) {
            show_page(PG_DETAILS);
            SetFocus(g_edit);
            return;
        }
        break;
    }
    if (g_ui_preview) {
        /* flip local mock values so toggles can be reviewed; nothing is saved */
        if (id == ID_T_AUTO) g_auto = !g_auto;
        else if (id == ID_T_START) g_ui.autostart = !g_ui.autostart;
        else if (id == ID_T_PAY) g_payment_shown = !g_payment_shown;
        else if (id == ID_EXIT) { PostMessageW(g_wnd, WM_CLOSE, 0, 0); return; }
        else { wchar_t n[200]; GetWindowTextW(ctl(id)->hwnd, n, 200); preview_action(n); return; }
        update_controls();
        return;
    }
    switch (id) {
    case ID_POWER:  post_cmd(CMD_TOGGLE_PAUSE, 0); break;
    case ID_ACTION:
        switch (action_for(&st)) {
        case A_RETRY:  post_cmd(CMD_RESCAN, 0); break;
        case A_FIX:    command(IDM_AVHELP); break;
        case A_RESUME: post_cmd(CMD_TOGGLE_PAUSE, 0); break;
        case A_DOWNLOAD: command(IDM_UPDATE); break;
        }
        break;
    case ID_BANNER: {
        int others, bk = banner_kind(&st, &others);
        if (bk == 1 && st.update_state == 0) post_cmd(CMD_UPDATE, 0);   /* one-click update */
        else if (bk == 1 && st.update_state == 2) command(IDM_UPDATE);  /* failed: release page */
        else if (bk == 2) command(IDM_PROXY);
        break;
    }
    case ID_RESCAN:    post_cmd(CMD_RESCAN, 0); break;
    case ID_TEST:      do_site_test(); break;
    case ID_T_AUTO:
        /* off = stay on the current method (manual), on = automatic scan */
        if (g_auto) post_cmd(CMD_SELECT, g_preset); else post_cmd(CMD_AUTO, 0);
        break;
    case ID_T_START:   command(IDM_AUTOSTART); break;
    case ID_T_PAY:     command(IDM_PAYMENT); break;
    case ID_L_LOG:     command(IDM_LOG); break;
    case ID_L_INI:     command(IDM_INI); break;
    case ID_L_AV:      command(IDM_AVHELP); break;
    case ID_L_GH:      command(IDM_GITHUB); break;
    case ID_UNINSTALL: command(IDM_UNINSTALL); break;
    case ID_EXIT:      command(IDM_EXIT); break;
    }
}

static void command(int id) {
    if (g_ui_preview) {
        /* Single gate: no menu or banner action runs in preview, including
         * ones added later (ini writes, schtasks, uninstall, URLs, exit). */
        if (id == IDM_EXIT) { PostMessageW(g_wnd, WM_CLOSE, 0, 0); return; }
        if (id) preview_action(L"Menü komutu");
        return;
    }
    if (id >= IDM_PRESET && id < IDM_PRESET + PRESET_COUNT) {
        post_cmd(CMD_SELECT, id - IDM_PRESET);
        return;
    }
    switch (id) {
    case IDM_AUTO:      post_cmd(CMD_AUTO, 0); break;
    case IDM_AUTOSTART: post_cmd(CMD_AUTOSTART, !autostart_enabled()); break;
    case IDM_PAYMENT:
        if (g_payment_shown &&
            MessageBoxW(g_wnd, L"Ödeme koruması kapatılırsa banka ve ödeme sitelerinin trafiği de "
                               L"değiştirilir. Bu, 3D Secure ve ödeme hatalarına yol açabilir.\n\n"
                               L"Yine de kapatılsın mı?", L"TurkishDPI",
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            break;
        set_payment_protect(!g_payment_shown);
        update_controls();
        break;
    case IDM_LOG:       app_open_file(g_log); break;
    case IDM_INI:       app_open_file(g_ini); break;
    case IDM_GITHUB:    app_open_url(TURKISHDPI_URL); break;
    case IDM_EXIT:      app_exit(); break;
    case IDM_UPDATE:    app_open_url(TURKISHDPI_RELEASES_URL); break;
    case IDM_PROXY:
        if (MessageBoxW(g_wnd, L"Windows'un proxy ayarı bu bilgisayarda artık çalışmayan bir programı "
                               L"gösteriyor (eski bir DPI/proxy aracı). Bu yüzden bazı programlar internete "
                               L"çıkamaz.\n\nProxy kapatılsın mı? (Adres silinmez, isterseniz geri açabilirsiniz.)",
                        L"TurkishDPI", MB_YESNO | MB_ICONQUESTION) == IDYES)
            app_repair_proxy();
        break;
    case IDM_AVHELP:
        MessageBoxW(g_wnd,
            L"TurkishDPI, paketleri işlemek için imzalı WinDivert sürücüsünü kullanır. Bazı antivirüsler "
            L"bunu yanlışlıkla engelleyebilir.\n\n"
            L"1) Windows Güvenliği > Virüs ve tehdit koruması > Koruma geçmişi'nde TurkishDPI ile ilgili "
            L"bir kayıt varsa 'Geri yükle' deyin.\n"
            L"2) Gerekirse yalnızca şu klasörü dışlamalara ekleyin (İndirilenler gibi tüm "
            L"klasörleri değil):\n"
            L"     C:\\Program Files\\TurkishDPI (Windows ile başlat açıksa)\n"
            L"3) Kaspersky kullanıyorsanız kaldırmanız gerekebilir.\n\n"
            L"Tamam'a basınca Windows Güvenliği açılır.", L"Antivirüs yardımı", MB_ICONINFORMATION);
        app_open_url(L"windowsdefender://threat");
        break;
    case IDM_UNINSTALL:
        if (MessageBoxW(g_wnd, L"TurkishDPI durdurulacak; Windows başlangıç görevi, Program Files kopyası, "
                               L"ayarlar, öğrenilen bilgiler, günlük ve WinDivert sürücü hizmeti kaldırılacak.\n\n"
                               L"Not: GoodbyeDPI veya zapret gibi başka bir WinDivert aracı kullanıyorsanız "
                               L"onu yeniden başlatmanız gerekebilir.\n\nDevam edilsin mi?",
                        L"Kaldır ve temizle", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES)
            app_uninstall();
        break;
    case IDM_SHOW:      ui_show(); break;
    case IDM_PAUSE:     post_cmd(CMD_TOGGLE_PAUSE, 0); break;
    case IDM_RESCAN:    post_cmd(CMD_RESCAN, 0); break;
    }
}

/* ---------- window ---------- */
static void load_logo(void) {
    if (g_logo) DestroyIcon(g_logo);
    g_logo = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON, S(24), S(24), 0);
}

static void apply_dpi(UINT dpi) {
    g_scale = g_force_scale > 0 ? g_force_scale : dpi / 96.0f;
    fonts_create();
    load_logo();
    layout();
}

static UINT window_dpi(HWND h) {
    typedef UINT (WINAPI *dpi_fn)(HWND);
    dpi_fn f = (dpi_fn)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (f) return f(h);
    {
        HDC dc = GetDC(NULL);
        UINT d = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(NULL, dc);
        return d;
    }
}

static void fit_window(void) {
    typedef BOOL (WINAPI *adj_fn)(LPRECT, DWORD, BOOL, DWORD, UINT);
    RECT r = { 0, 0, S(BASE_W), S(BASE_H) };
    DWORD style = (DWORD)GetWindowLongW(g_wnd, GWL_STYLE);
    /* per-monitor DPI: the frame must be sized for this window's DPI,
     * not the system DPI (Windows 10 1607+; older systems fall back) */
    adj_fn adj = (adj_fn)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                "AdjustWindowRectExForDpi");
    if (!adj || !adj(&r, style, FALSE, 0, window_dpi(g_wnd)))
        AdjustWindowRect(&r, style, FALSE);
    SetWindowPos(g_wnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void dark_titlebar(HWND h) {
    typedef HRESULT (WINAPI *dwm_fn)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE m = load_system_dll(L"dwmapi.dll");
    if (m) {
        dwm_fn f = (dwm_fn)(void *)GetProcAddress(m, "DwmSetWindowAttribute");
        BOOL on = TRUE;
        COLORREF cap = P->bg;
        if (f) {
            f(h, 20, &on, sizeof(on));      /* DWMWA_USE_IMMERSIVE_DARK_MODE */
            f(h, 35, &cap, sizeof(cap));    /* DWMWA_CAPTION_COLOR (Win11)   */
        }
    }
}

static void create_controls(HWND h) {
    for (int i = 0; i < NCTL; i++) {
        RECT r = ctl_rect(&g_ctl[i]);
        g_ctl[i].hwnd = CreateWindowExW(0, L"BUTTON", L"",
                                        WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                                        r.left, r.top, r.right - r.left, r.bottom - r.top,
                                        h, (HMENU)(INT_PTR)g_ctl[i].id, g_inst, NULL);
        g_btn_proc = (WNDPROC)SetWindowLongPtrW(g_ctl[i].hwnd, GWLP_WNDPROC, (LONG_PTR)btn_proc);
    }
    g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                             0, 0, 10, 10, h, (HMENU)100, g_inst, NULL);
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)f_body, FALSE);
    SendMessageW(g_edit, EM_SETCUEBANNER, TRUE, (LPARAM)L"ör. discord.com");
    SendMessageW(g_edit, EM_SETLIMITTEXT, 200, 0);
    g_edit_proc = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
    /* tab order follows creation order; the edit belongs before "Kontrol et" */
    SetWindowPos(g_edit, ctl(ID_RESCAN)->hwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    /* screen-reader text, inserted into the reading (z-) order after the
     * control that precedes it on screen; the label right before the edit
     * also becomes the edit's accessible name */
    {
        static const int after[A_COUNT] = { ID_POWER, ID_TAB2, ID_RESCAN, 0, ID_TEST, ID_L_GH };
        for (int i = 0; i < A_COUNT; i++) {
            g_a11y[i] = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT | SS_NOPREFIX,
                                        0, 0, 10, 10, h, NULL, g_inst, NULL);
            SetWindowRgn(g_a11y[i], CreateRectRgn(0, 0, 0, 0), FALSE);
            g_static_proc = (WNDPROC)SetWindowLongPtrW(g_a11y[i], GWLP_WNDPROC, (LONG_PTR)a11y_proc);
        }
        for (int i = 0; i < A_COUNT; i++) {
            HWND prev = i == A_SITE_LABEL ? g_a11y[A_TARGETS] : ctl(after[i])->hwnd;
            SetWindowPos(g_a11y[i], prev, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        SetWindowPos(g_edit, g_a11y[A_SITE_LABEL], 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_taskbar_created && msg) { g_tray_tries = 0; tray_add(); tray_update(); return 0; }
    /* Posted by a second start of the (elevated) exe. Not in the UIPI
     * allow-list, so unprivileged programs cannot post it. Never registered
     * in --ui-preview (g_show_msg stays 0). */
    if (msg == g_show_msg && msg) { ui_show(); return 0; }
    switch (msg) {
    case WM_CREATE:
        g_wnd = h;
        g_scale = g_force_scale > 0 ? g_force_scale : window_dpi(h) / 96.0f;
        fonts_create();
        load_logo();
        create_controls(h);
        layout();
        update_controls();
        return 0;
    case WM_DPICHANGED: {
        RECT *r = (RECT *)lp;
        if (g_force_scale > 0) return 0;   /* preview pins the scale */
        apply_dpi(HIWORD(wp));
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        fit_window();
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT cr;
        HDC dc = BeginPaint(h, &ps), mem;
        HBITMAP bmp, old;
        GetClientRect(h, &cr);
        mem = CreateCompatibleDC(dc);
        bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
        old = SelectObject(mem, bmp);
        paint(mem, cr);
        BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_PRINTCLIENT: {             /* PrintWindow (preview screenshots) */
        RECT cr;
        GetClientRect(h, &cr);
        paint((HDC)wp, cr);
        return 0;
    }
    case WM_DRAWITEM:
        draw_item((const DRAWITEMSTRUCT *)lp);
        return TRUE;
    case WM_ACTIVATE: {
        /* like a dialog: remember the focused control, restore it (or the
         * current tab) on activation so the keyboard always has a target */
        static HWND last;
        if (LOWORD(wp) == WA_INACTIVE) {
            HWND f = GetFocus();
            if (f && GetParent(f) == h) last = f;
        } else if (!g_preview_shot) {
            HWND f = last && IsWindowVisible(last) ? last : ctl(ID_TAB0 + g_page)->hwnd;
            SetFocus(f);
        }
        return 0;
    }
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, P->text);
        SetBkColor((HDC)wp, P->sunken);
        return (LRESULT)g_sunken_brush;
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == IDOK) {                  /* Enter (IsDialogMessage) */
            HWND f = GetFocus();
            int fid = f ? GetDlgCtrlID(f) : 0;
            if (f == g_edit) do_site_test();
            else if (fid >= ID_TAB0 && fid <= ID_EXIT && GetParent(f) == h) do_click(fid);
        } else if (id >= ID_TAB0 && id <= ID_EXIT) {
            if (code == BN_CLICKED || code == BN_DOUBLECLICKED) do_click(id);
        } else if (id >= 1000) {
            command(id);
        }
        return 0;
    }
    case WM_SIZE:            /* minimize/restore: start or stop the spinner */
        spinner_update();
        return 0;
    case WM_TIMER:
        if (wp == TIMER_TRAY) { tray_add(); tray_update(); return 0; }
        g_anim = (g_anim + 9) % 360;
        InvalidateRect(ctl(ID_POWER)->hwnd, NULL, FALSE);
        return 0;
    case WM_TRAY: {
        /* WM_TRAY is let through UIPI, so any program could post it: act
         * only when the pointer really is on our icon, and never open a
         * second menu while one is showing. */
        static int in_menu = 0;
        if (!tray_click_genuine() || in_menu) return 0;
        if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK) ui_toggle();
        else if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
            in_menu = 1;
            ui_tray_menu(h);
            in_menu = 0;
        }
        return 0;
    }
    case WM_STATUS:
        tray_update();
        if (IsWindowVisible(h)) update_controls();
        spinner_update();
        return 0;
    case WM_UPDATE_DONE:   /* posted by our own controller thread only */
        if (!g_ui_preview) app_update_done((int)wp);
        return 0;
    case WM_CLOSE:
        if (g_ui_preview) { DestroyWindow(h); PostQuitMessage(0); return 0; }
        ShowWindow(h, SW_HIDE);
        spinner_update();
        SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC && !g_ui_preview) app_network_event();
        return TRUE;
    case WM_ENDSESSION:
        if (wp && !g_ui_preview) app_exit();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int ui_init(HINSTANCE inst) {
    WNDCLASSEXW wc;
    GpStartupInput gi = { 1, NULL, FALSE, TRUE };
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };

    g_inst = inst;
    InitCommonControlsEx(&icc);
    if (!gdiplus_load()) return 0;
    GdiplusStartup(&g_gdip, &gi, NULL);
    dark_menus();
    g_sunken_brush = CreateSolidBrush(P->sunken);
    for (int i = 0; i < 6; i++)
        g_tray_icons[i] = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(i == 0 ? 1 : 10 + i), IMAGE_ICON,
                                            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    if (!g_tray_icons[5]) g_tray_icons[5] = g_tray_icons[2];

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = g_tray_icons[0];
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    /* the preview must not be found by the real app's FindWindowW() */
    wc.lpszClassName = g_ui_preview ? L"TurkishDPI_preview" : L"TurkishDPI_main";
    if (!RegisterClassExW(&wc)) return 0;

    g_wnd = CreateWindowExW((g_preview_shot ? WS_EX_TOOLWINDOW : 0) | WS_EX_CONTROLPARENT,
                            wc.lpszClassName,
                            g_ui_preview ? L"TurkishDPI (önizleme)" : L"TurkishDPI",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, NULL, NULL, inst, NULL);
    if (!g_wnd) return 0;
    dark_titlebar(g_wnd);
    fit_window();
    if (g_preview_shot) {
        SetWindowPos(g_wnd, NULL, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        /* centre on the work area */
        RECT wr, wa;
        GetWindowRect(g_wnd, &wr);
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        SetWindowPos(g_wnd, NULL, wa.left + ((wa.right - wa.left) - (wr.right - wr.left)) / 2,
                     wa.top + ((wa.bottom - wa.top) - (wr.bottom - wr.top)) / 2, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (g_ui_preview) return 1;   /* no tray icon, no UIPI changes */
    g_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    g_show_msg = RegisterWindowMessageW(TURKISHDPI_SHOW_MSG);   /* deliberately not allowed below */
    {
        /* We run elevated: UIPI drops messages from the (non-elevated)
         * Explorer unless allowed, so the icon would never come back after
         * Explorer restarts and tray clicks could be lost. Only these two are
         * let through: WM_COMMAND must NOT be, or any unprivileged program
         * could post menu commands (exit, autostart, profile...) to us. Our
         * own menus send WM_COMMAND from inside this process, which UIPI
         * never blocks. */
        typedef BOOL (WINAPI *filter_fn)(HWND, UINT, DWORD, void *);
        filter_fn f = (filter_fn)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                        "ChangeWindowMessageFilterEx");
        if (f) {
            f(g_wnd, g_taskbar_created, 1 /* MSGFLT_ALLOW */, NULL);
            f(g_wnd, WM_TRAY, 1, NULL);
        }
    }
    tray_add();
    tray_update();
    return 1;
}

void ui_show(void) {
    ShowWindow(g_wnd, SW_SHOW);
    if (IsIconic(g_wnd)) ShowWindow(g_wnd, SW_RESTORE);
    SetForegroundWindow(g_wnd);
    PostMessageW(g_wnd, WM_STATUS, 0, 0);
}

void ui_toggle(void) {
    if (IsWindowVisible(g_wnd) && !IsIconic(g_wnd) && GetForegroundWindow() == g_wnd)
        PostMessageW(g_wnd, WM_CLOSE, 0, 0);
    else
        ui_show();
}

void ui_refresh(void) {
    if (g_wnd) PostMessageW(g_wnd, WM_STATUS, 0, 0);
}

HWND ui_hwnd(void) {
    return g_wnd;
}

/* ---------- --ui-preview ---------- */
/* Developer tool: shows the window with mock state so the design can be
 * reviewed and screenshotted without administrator rights. Nothing here
 * touches the data folder, the registry, the network, the engine or the
 * tray; every action is stopped by the gates in do_click(), do_site_test()
 * and command(). */
static void preview_action(const wchar_t *what) {
    EnterCriticalSection(&g_lock);
    _snwprintf(g_ui.site_result, 159, L"Önizleme: \"%ls\" çalıştırılmadı.", what);
    g_ui.site_result[159] = 0;
    LeaveCriticalSection(&g_lock);
    update_controls();
}

static void pv_state(state_kind_t k, const wchar_t *status, const wchar_t *detail) {
    g_ui.kind = k;
    _snwprintf(g_ui.status, 127, L"%ls", status);
    g_ui.status[127] = 0;
    _snwprintf(g_ui.detail, 159, L"%ls", detail);
    g_ui.detail[159] = 0;
}

static void pv_targets(int n, const wchar_t *const *names, const int *ok, const int *ms) {
    int sum = 0, cnt = 0;
    g_ui.ntargets = n;
    for (int i = 0; i < n; i++) {
        _snwprintf(g_ui.target[i], 63, L"%ls", names[i]);
        g_ui.target[i][63] = 0;
        g_ui.target_ok[i] = ok ? ok[i] : -1;
        g_ui.target_ms[i] = ms ? ms[i] : -1;
        if (ok && ok[i] == 1 && ms && ms[i] > 0) { sum += ms[i]; cnt++; }
    }
    g_ui.avg_ms = cnt ? sum / cnt : -1;
}

/* Returns 0 for an unknown scenario. All values are mock data. */
static int preview_fill(const char *name) {
    static const wchar_t *t3[] = { L"discord.com", L"www.roblox.com", L"www.wattpad.com" };
    static const wchar_t *t8[] = { L"discord.com", L"www.roblox.com", L"www.wattpad.com",
                                   L"pastebin.com", L"imgur.com", L"cdn.discordapp.com",
                                   L"bir-cok-uzun-alt-alan-adi.ornek-site.com.tr", L"www.example.org" };
    static const int ok3[] = { 1, 1, 1 }, ms3[] = { 148, 162, 139 };
    static const int okp[] = { 1, 0, 1 }, msp[] = { 151, -1, 143 };
    static const int ok8[] = { 1, 1, 1, 1, 0, 1, 1, -1 };
    static const int ms8[] = { 148, 162, 139, 201, -1, 187, 1234, -1 };
    static const int unk[] = { -1, -1, -1 }, none[] = { 0, 0, 0 };

    ZeroMemory(&g_ui, sizeof(g_ui));
    g_ui.preset = 4;
    g_ui.dns = 4;
    g_ui.dns_ms = 42;
    g_ui.hybrid = 1;
    g_ui.n_block_ips = 1;
    g_ui.autostart = 1;
    g_ui.learned = 3;
    g_ui.scan_total = 10;
    wcscpy(g_ui.network, L"Ev Wi-Fi");
    g_auto = 1; g_preset = 4; g_paused = 0; g_payment_shown = 1;
    pv_targets(3, t3, ok3, ms3);
    g_ui.st_packets = 18432; g_ui.st_https = 812; g_ui.st_http = 37;
    g_ui.st_dns = 655; g_ui.st_excluded = 6;
    pv_state(ST_ACTIVE, L"Koruma aktif", L"SNI Parçalama + Sahte SNI (DNS)");

    if (!strcmp(name, "active")) {
        /* defaults above */
    } else if (!strcmp(name, "active-long")) {
        wcscpy(g_ui.network, L"Çok Uzun Bir Kablosuz Ağ Adı (5 GHz) – Misafir");
        pv_targets(8, t8, ok8, ms8);
        g_ui.st_packets = 4294967295UL; g_ui.st_https = 123456789UL;
        g_ui.st_dns = 98765432UL; g_ui.st_excluded = 1234567UL;
        pv_state(ST_ACTIVE, L"Koruma aktif", L"Agresif SNI (DNS, 2x sahte paket)");
        g_ui.preset = 5; g_preset = 5;
        _snwprintf(g_ui.site_result, 159, L"%ls",
                   L"bir-cok-uzun-alt-alan-adi.ornek-site.com.tr açılıyor (1234 ms). Mevcut profil "
                   L"bu siteyi de açıyor, değişiklik gerekmedi.");
        g_ui.site_result[159] = 0;
    } else if (!strcmp(name, "partial")) {
        pv_targets(3, t3, okp, msp);
        pv_state(ST_PARTIAL, L"Kısmen aktif", L"Türkiye Standart (-5, TTL 5, DNS)");
        g_ui.preset = 0; g_preset = 0;
    } else if (!strcmp(name, "scanning") || !strcmp(name, "first-run")) {
        pv_targets(3, t3, unk, NULL);
        g_ui.preset = -1;
        g_ui.scan_pos = 3;
        g_ui.st_packets = 0;
        if (name[0] == 'f') { g_ui.learned = 0; g_ui.dns_ms = 0; }
        pv_state(ST_SCANNING, L"Profil 3/10 deneniyor…", L"Auto-TTL (-5, DNS)");
    } else if (!strcmp(name, "standby")) {
        g_ui.preset = -1;
        g_ui.st_packets = 0;
        pv_state(ST_STANDBY, L"Engel algılanmadı", L"Bekleme modunda – engel çıkarsa devreye girer");
    } else if (!strcmp(name, "paused")) {
        pv_targets(3, t3, unk, NULL);
        g_ui.preset = -1; g_paused = 1;
        g_ui.st_packets = 0;
        pv_state(ST_PAUSED, L"Duraklatıldı", L"Açmak için düğmeye basın");
    } else if (!strcmp(name, "offline")) {
        pv_targets(3, t3, unk, NULL);
        g_ui.preset = -1;
        g_ui.network[0] = 0;
        g_ui.st_packets = 0;
        pv_state(ST_OFFLINE, L"İnternet bağlantısı yok", L"Bağlantı gelince otomatik başlayacak");
    } else if (!strcmp(name, "error-driver")) {
        pv_targets(3, t3, unk, NULL);
        g_ui.preset = -1; g_ui.driver_err = 2;
        g_ui.st_packets = 0;
        pv_state(ST_ERROR, L"Sürücü başlatılamadı (2)",
                 L"WinDivert64.sys bulunamadı. Zip'teki tüm dosyaları TurkishDPI.exe ile aynı "
                 L"klasöre çıkarın; dosya oradaysa antivirüs karantinasına bakın.");
    } else if (!strcmp(name, "error-av")) {
        pv_targets(3, t3, unk, NULL);
        g_ui.preset = -1; g_ui.driver_err = 1275;
        g_ui.st_packets = 0;
        pv_state(ST_ERROR, L"Sürücü başlatılamadı (1275)",
                 L"Sürücü bir güvenlik yazılımı tarafından engellendi (ör. Kaspersky).");
    } else if (!strcmp(name, "error-noprofile")) {
        pv_targets(3, t3, none, NULL);
        g_ui.preset = -1;
        g_ui.st_packets = 0;
        pv_state(ST_ERROR, L"Çalışan profil bulunamadı", L"Günlük dosyasına bakın");
    } else if (!strcmp(name, "banner-update")) {
        wcscpy(g_ui.update_tag, L"v2.0.2");
    } else if (!strcmp(name, "banner-updating")) {
        wcscpy(g_ui.update_tag, L"v2.0.2");
        g_ui.update_state = 1;
    } else if (!strcmp(name, "banner-update-failed")) {
        wcscpy(g_ui.update_tag, L"v2.0.2");
        g_ui.update_state = 2;
    } else if (!strcmp(name, "banner-proxy")) {
        g_ui.proxy_broken = 1;
    } else if (!strcmp(name, "banner-rival")) {
        wcscpy(g_ui.rival, L"GoodbyeDPI");
    } else if (!strcmp(name, "banner-multi")) {
        wcscpy(g_ui.update_tag, L"v2.0.2");
        g_ui.proxy_broken = 1;
        wcscpy(g_ui.rival, L"GoodbyeDPI");
    } else if (!strcmp(name, "manual")) {
        g_auto = 0; g_preset = 7; g_ui.preset = 7;
        pv_state(ST_ACTIVE, L"Koruma aktif", L"Superonline (-9)");
    } else if (!strcmp(name, "payment-off")) {
        g_payment_shown = 0;
    } else {
        return 0;
    }
    return 1;
}

/* Writes the client area as a top-down 32-bit BMP (no GDI+ codecs). */
static int preview_capture(HWND h, const char *path) {
    RECT cr;
    BITMAPINFO bi;
    BITMAPFILEHEADER fh;
    void *bits = NULL;
    HDC sdc, mem;
    HBITMAP bmp, old;
    FILE *f;
    int ok = 0, w, hh;

    GetClientRect(h, &cr);
    w = cr.right;
    hh = cr.bottom;
    if (w <= 0 || hh <= 0) return 0;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -hh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    sdc = GetDC(NULL);
    mem = CreateCompatibleDC(sdc);
    bmp = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, sdc);
    if (!mem || !bmp) {
        if (mem) DeleteDC(mem);
        if (bmp) DeleteObject(bmp);
        return 0;
    }
    old = SelectObject(mem, bmp);
    if (PrintWindow(h, mem, 1 /* PW_CLIENTONLY */)) {
        GdiFlush();
        ZeroMemory(&fh, sizeof(fh));
        fh.bfType = 0x4D42;
        fh.bfOffBits = sizeof(fh) + sizeof(bi.bmiHeader);
        fh.bfSize = fh.bfOffBits + (DWORD)w * (DWORD)hh * 4;
        f = fopen(path, "wb");
        if (f) {
            ok = fwrite(&fh, sizeof(fh), 1, f) == 1 &&
                 fwrite(&bi.bmiHeader, sizeof(bi.bmiHeader), 1, f) == 1 &&
                 fwrite(bits, (size_t)w * hh * 4, 1, f) == 1;
            ok = (fclose(f) == 0) && ok;
        }
    }
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    return ok;
}

/* TurkishDPI.exe --ui-preview <scenario> [--scale F] [--page P] [--shot out.bmp]
 * Exit codes: 0 ok, 2 bad arguments/scenario, 3 window failed, 4 capture failed. */
int ui_preview_main(int argc, char **argv) {
    const char *scenario, *shot = NULL;
    int page = PG_STATUS;
    MSG m;

    if (argc < 3 || argv[2][0] == '-') return 2;
    scenario = argv[2];
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
            g_force_scale = (float)atof(argv[++i]);
            if (g_force_scale < 0.5f || g_force_scale > 4.0f) return 2;
        } else if (!strcmp(argv[i], "--page") && i + 1 < argc) {
            const char *p = argv[++i];
            if (strcmp(p, "status") && strcmp(p, "details") && strcmp(p, "settings")) return 2;
            page = !strcmp(p, "details") ? PG_DETAILS : !strcmp(p, "settings") ? PG_SETTINGS : PG_STATUS;
        } else if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
            shot = argv[++i];
        } else if (!strcmp(argv[i], "--focus") && i + 1 < argc) {
            g_preview_focus = ID_TAB0 + atoi(argv[++i]);   /* index into g_ctl ids */
            if (!ctl(g_preview_focus)) return 2;
        } else if (!strcmp(argv[i], "--press") && i + 1 < argc) {
            g_preview_press = ID_TAB0 + atoi(argv[++i]);
            if (!ctl(g_preview_press)) return 2;
        } else {
            return 2;
        }
    }
    g_ui_preview = 1;
    g_preview_shot = shot != NULL;
    InitializeCriticalSection(&g_lock);
    if (!preview_fill(scenario)) return 2;
    g_anim = 40;   /* fixed spinner angle: screenshots are reproducible */
    if (!ui_init(GetModuleHandleW(NULL))) return 3;
    show_page(page);
    if (shot) {
        ShowWindow(g_wnd, SW_SHOWNOACTIVATE);
        update_controls();
        UpdateWindow(g_wnd);
        return preview_capture(g_wnd, shot) ? 0 : 4;
    }
    ui_show();
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(g_wnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    return 0;
}
