/*
 * TurkishDPI - main window and tray icon.
 *
 * Hand-drawn dark UI: plain Win32 + GDI+ (flat API, anti-aliased shapes) and
 * GDI ClearType text. No framework, no web view: the window costs a few
 * hundred KB while open and nothing at all while hidden.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <math.h>
#include <stdio.h>
#include <wchar.h>
#include "app.h"
#include "presets.h"
#include "version.h"

/* ---------- minimal GDI+ flat API ---------- */
typedef struct { UINT32 ver; void *cb; BOOL no_thread; BOOL no_codecs; } GpStartupInput;
typedef void GpGraphics, GpBrush, GpPen, GpPath;
typedef struct { float X, Y; } GpPointF;
#define GPAPI __stdcall
int GPAPI GdiplusStartup(ULONG_PTR *, const GpStartupInput *, void *);
int GPAPI GdipCreateFromHDC(HDC, GpGraphics **);
int GPAPI GdipDeleteGraphics(GpGraphics *);
int GPAPI GdipSetSmoothingMode(GpGraphics *, int);
int GPAPI GdipCreateSolidFill(DWORD, GpBrush **);
int GPAPI GdipCreateLineBrush(const GpPointF *, const GpPointF *, DWORD, DWORD, int, GpBrush **);
int GPAPI GdipDeleteBrush(GpBrush *);
int GPAPI GdipCreatePen1(DWORD, float, int, GpPen **);
int GPAPI GdipSetPenStartCap(GpPen *, int);
int GPAPI GdipSetPenEndCap(GpPen *, int);
int GPAPI GdipDeletePen(GpPen *);
int GPAPI GdipFillEllipse(GpGraphics *, GpBrush *, float, float, float, float);
int GPAPI GdipDrawEllipse(GpGraphics *, GpPen *, float, float, float, float);
int GPAPI GdipDrawArc(GpGraphics *, GpPen *, float, float, float, float, float, float);
int GPAPI GdipDrawLine(GpGraphics *, GpPen *, float, float, float, float);
int GPAPI GdipCreatePath(int, GpPath **);
int GPAPI GdipAddPathArc(GpPath *, float, float, float, float, float, float);
int GPAPI GdipClosePathFigure(GpPath *);
int GPAPI GdipFillPath(GpGraphics *, GpBrush *, GpPath *);
int GPAPI GdipDrawPath(GpGraphics *, GpPen *, GpPath *);
int GPAPI GdipDeletePath(GpPath *);

/* ---------- palette ---------- */
#define ARGB(a, r, g, b) ((DWORD)(((a) << 24) | ((r) << 16) | ((g) << 8) | (b)))
#define C_BG       RGB(0x11, 0x13, 0x1A)
#define C_CARD     RGB(0x1A, 0x1E, 0x29)
#define C_TEXT     RGB(0xEC, 0xEE, 0xF3)
#define C_SUB      RGB(0x8A, 0x92, 0xA6)
#define C_FAINT    RGB(0x5C, 0x63, 0x75)
#define C_GREEN    RGB(0x22, 0xC5, 0x5E)
#define C_RED      RGB(0xEF, 0x44, 0x44)
#define C_AMBER    RGB(0xF5, 0xB0, 0x14)

static const DWORD A_CARD = ARGB(255, 0x1A, 0x1E, 0x29);
static const DWORD A_BORDER = ARGB(255, 0x27, 0x2D, 0x3B);
static const DWORD A_HOVER = ARGB(255, 0x24, 0x29, 0x37);
static const DWORD A_BRAND = ARGB(255, 0xE5, 0x29, 0x3A);

/* ---------- window state ---------- */
static HWND g_wnd, g_edit;
static HINSTANCE g_inst;
static UINT g_taskbar_created;
static ULONG_PTR g_gdip;
static float g_scale = 1.0f;
static HFONT f_title, f_sub, f_big, f_body, f_small, f_label, f_value;
static HBRUSH g_card_brush;
static HICON g_logo, g_tray_icons[5];
static int g_anim = 0, g_hover = 0, g_tracking = 0;
static NOTIFYICONDATAW g_nid;
static WNDPROC g_edit_proc;
int g_payment_domains = 100;

enum { HIT_NONE, HIT_POWER, HIT_RESCAN, HIT_PROFILE, HIT_SETTINGS, HIT_TEST, HIT_BANNER };
enum { IDM_AUTO = 1000, IDM_AUTOSTART, IDM_PAYMENT, IDM_LOG, IDM_INI, IDM_GITHUB,
       IDM_EXIT, IDM_SHOW, IDM_PAUSE, IDM_RESCAN, IDM_UNINSTALL, IDM_AVHELP, IDM_PROXY,
       IDM_UPDATE, IDM_PRESET = 1100 };

static RECT r_power, r_rescan, r_profile, r_settings, r_test, r_edit, r_banner;
static int g_banner_kind;   /* 0 none, 1 update, 2 proxy, 3 rival, 4 antivirus */

#define BASE_W 440
#define BASE_H 734

static int S(int v) { return (int)(v * g_scale + 0.5f); }
static float SF(float v) { return v * g_scale; }

/* ---------- helpers ---------- */
static DWORD state_argb(state_kind_t k) {
    switch (k) {
    case ST_ACTIVE:   return ARGB(255, 0x22, 0xC5, 0x5E);
    case ST_PARTIAL:  return ARGB(255, 0xF5, 0xB0, 0x14);
    case ST_SCANNING:
    case ST_STARTING: return ARGB(255, 0x3B, 0x82, 0xF6);
    case ST_STANDBY:  return ARGB(255, 0x38, 0xBD, 0xF8);
    case ST_PAUSED:   return ARGB(255, 0x64, 0x74, 0x8B);
    case ST_OFFLINE:  return ARGB(255, 0x94, 0xA3, 0xB8);
    default:          return ARGB(255, 0xEF, 0x44, 0x44);
    }
}

static int tray_icon_index(state_kind_t k) {
    switch (k) {
    case ST_ACTIVE: case ST_STANDBY: return 1;
    case ST_SCANNING: case ST_STARTING: case ST_PARTIAL: return 2;
    case ST_PAUSED: case ST_OFFLINE: return 3;
    default: return 4;
    }
}

static void round_rect_path(GpPath *p, float x, float y, float w, float h, float r) {
    float d = r * 2;
    GdipAddPathArc(p, x, y, d, d, 180, 90);
    GdipAddPathArc(p, x + w - d, y, d, d, 270, 90);
    GdipAddPathArc(p, x + w - d, y + h - d, d, d, 0, 90);
    GdipAddPathArc(p, x, y + h - d, d, d, 90, 90);
    GdipClosePathFigure(p);
}

static void fill_round(GpGraphics *g, RECT r, float rad, DWORD fill, DWORD border) {
    GpPath *p;
    GpBrush *b;
    GdipCreatePath(0, &p);
    round_rect_path(p, (float)r.left, (float)r.top, (float)(r.right - r.left),
                    (float)(r.bottom - r.top), SF(rad));
    GdipCreateSolidFill(fill, &b);
    GdipFillPath(g, b, p);
    GdipDeleteBrush(b);
    if (border) {
        GpPen *pen;
        GdipCreatePen1(border, SF(1.0f), 2, &pen);
        GdipDrawPath(g, pen, p);
        GdipDeletePen(pen);
    }
    GdipDeletePath(p);
}

static void text(HDC dc, HFONT f, COLORREF c, RECT r, const wchar_t *s, UINT flags) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, flags | DT_NOPREFIX);
}

static RECT rc(int x, int y, int w, int h) {
    RECT r = { S(x), S(y), S(x + w), S(y + h) };
    return r;
}

static HFONT mkfont(int px, int weight) {
    return CreateFontW(-S(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

static void fonts_create(void) {
    HFONT *all[] = { &f_title, &f_sub, &f_big, &f_body, &f_small, &f_label, &f_value };
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++)
        if (*all[i]) DeleteObject(*all[i]);
    f_title = mkfont(20, FW_BOLD);
    f_sub   = mkfont(12, FW_NORMAL);
    f_big   = mkfont(22, FW_BOLD);
    f_body  = mkfont(13, FW_NORMAL);
    f_small = mkfont(12, FW_NORMAL);
    f_label = mkfont(10, FW_BOLD);
    f_value = mkfont(14, FW_SEMIBOLD);
    if (g_edit) SendMessageW(g_edit, WM_SETFONT, (WPARAM)f_body, TRUE);
}

static void layout(void) {
    r_power = rc(BASE_W / 2 - 66, 92, 132, 132);
    r_banner = rc(20, 66, 400, 24);
    r_edit = rc(32, 584, 280, 30);
    r_test = rc(328, 580, 92, 38);
    r_rescan = rc(20, 650, 128, 40);
    r_profile = rc(156, 650, 128, 40);
    r_settings = rc(292, 650, 128, 40);
    if (g_edit)
        MoveWindow(g_edit, r_edit.left, r_edit.top + S(6), r_edit.right - r_edit.left,
                   S(20), TRUE);
}

/* ---------- painting ---------- */
static void draw_button(GpGraphics *g, RECT r, int hot, int primary) {
    DWORD fill = primary ? (hot ? ARGB(255, 0xF0, 0x3A, 0x4B) : A_BRAND)
                         : (hot ? A_HOVER : A_CARD);
    fill_round(g, r, 10, fill, primary ? 0 : A_BORDER);
}

static void paint(HDC dc, RECT client) {
    ui_state_t st;
    GpGraphics *g;
    GpBrush *b;
    GpPen *pen;
    wchar_t buf[160];
    DWORD col;
    float cx, cy, rad;
    HBRUSH bg = CreateSolidBrush(C_BG);

    EnterCriticalSection(&g_lock);
    st = g_ui;
    LeaveCriticalSection(&g_lock);
    g_banner_kind = st.kind == ST_ERROR && wcsstr(st.status, L"Sürücü") ? 4
                  : st.proxy_broken ? 2 : st.rival[0] ? 3 : st.update_tag[0] ? 1 : 0;

    FillRect(dc, &client, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);

    GdipCreateFromHDC(dc, &g);
    GdipSetSmoothingMode(g, 4);

    /* header glow */
    {
        GpPointF p1 = { 0, 0 }, p2 = { 0, SF(90) };
        GdipCreateLineBrush(&p1, &p2, ARGB(60, 0xE5, 0x29, 0x3A), ARGB(0, 0xE5, 0x29, 0x3A), 0, &b);
        GdipFillEllipse(g, b, SF(-60), SF(-120), SF(BASE_W + 120), SF(200));
        GdipDeleteBrush(b);
    }

    /* power button */
    col = state_argb(st.kind);
    cx = (r_power.left + r_power.right) / 2.0f;
    cy = (r_power.top + r_power.bottom) / 2.0f;
    rad = (r_power.right - r_power.left) / 2.0f;
    GdipCreateSolidFill((col & 0x00FFFFFF) | 0x18000000, &b);
    GdipFillEllipse(g, b, cx - rad - SF(18), cy - rad - SF(18), (rad + SF(18)) * 2, (rad + SF(18)) * 2);
    GdipDeleteBrush(b);
    GdipCreateSolidFill((col & 0x00FFFFFF) | 0x30000000, &b);
    GdipFillEllipse(g, b, cx - rad - SF(8), cy - rad - SF(8), (rad + SF(8)) * 2, (rad + SF(8)) * 2);
    GdipDeleteBrush(b);
    {
        GpPointF p1 = { cx, cy - rad }, p2 = { cx, cy + rad };
        GdipCreateLineBrush(&p1, &p2, g_hover == HIT_POWER ? ARGB(255, 0x2A, 0x30, 0x40) : ARGB(255, 0x24, 0x29, 0x37),
                            ARGB(255, 0x15, 0x18, 0x21), 0, &b);
        GdipFillEllipse(g, b, cx - rad, cy - rad, rad * 2, rad * 2);
        GdipDeleteBrush(b);
    }
    GdipCreatePen1(col, SF(4), 2, &pen);
    GdipSetPenStartCap(pen, 2);
    GdipSetPenEndCap(pen, 2);
    if (st.kind == ST_SCANNING || st.kind == ST_STARTING) {
        GdipDrawArc(g, pen, cx - rad + SF(2), cy - rad + SF(2), (rad - SF(2)) * 2, (rad - SF(2)) * 2,
                    (float)g_anim, 100);
        GdipDrawArc(g, pen, cx - rad + SF(2), cy - rad + SF(2), (rad - SF(2)) * 2, (rad - SF(2)) * 2,
                    (float)g_anim + 180, 100);
    } else {
        GdipDrawEllipse(g, pen, cx - rad + SF(2), cy - rad + SF(2), (rad - SF(2)) * 2, (rad - SF(2)) * 2);
    }
    GdipDeletePen(pen);
    /* power glyph */
    GdipCreatePen1(st.kind == ST_PAUSED ? ARGB(255, 0x8A, 0x92, 0xA6) : col, SF(7), 2, &pen);
    GdipSetPenStartCap(pen, 2);
    GdipSetPenEndCap(pen, 2);
    GdipDrawArc(g, pen, cx - SF(28), cy - SF(26), SF(56), SF(56), -50, 280);
    GdipDrawLine(g, pen, cx, cy - SF(34), cx, cy - SF(4));
    GdipDeletePen(pen);

    /* info cards */
    for (int i = 0; i < 3; i++) {
        RECT r = rc(20 + i * 136, 318, 128, 60);
        fill_round(g, r, 12, A_CARD, A_BORDER);
    }
    /* sites card */
    fill_round(g, rc(20, 394, 400, 172), 12, A_CARD, A_BORDER);
    for (int i = 0; i < st.ntargets && i < PROBE_MAX_HOSTS; i++) {
        DWORD dcol = st.target_ok[i] < 0 ? ARGB(255, 0x5C, 0x63, 0x75)
                   : st.target_ok[i] ? ARGB(255, 0x22, 0xC5, 0x5E) : ARGB(255, 0xEF, 0x44, 0x44);
        GdipCreateSolidFill(dcol, &b);
        GdipFillEllipse(g, b, SF(38), SF(430 + i * 16.5f + 5), SF(8), SF(8));
        GdipDeleteBrush(b);
    }
    /* site test input + buttons */
    fill_round(g, rc(20, 580, 300, 38), 10, ARGB(255, 0x15, 0x18, 0x21), A_BORDER);
    draw_button(g, r_test, g_hover == HIT_TEST, 1);
    draw_button(g, r_rescan, g_hover == HIT_RESCAN, 0);
    draw_button(g, r_profile, g_hover == HIT_PROFILE, 0);
    draw_button(g, r_settings, g_hover == HIT_SETTINGS, 0);
    /* notice banner */
    if (g_banner_kind) {
        DWORD bc = g_banner_kind == 1 ? ARGB(255, 0x1E, 0x3A, 0x8A)
                 : g_banner_kind == 2 ? ARGB(255, 0x78, 0x35, 0x0F)
                 : ARGB(255, 0x7F, 0x1D, 0x1D);
        fill_round(g, r_banner, 8, g_hover == HIT_BANNER ? (bc | 0xFF000000) + 0x00101010 : bc, 0);
    }
    /* payment shield dot */
    GdipCreateSolidFill(g_payment_protect ? ARGB(255, 0x22, 0xC5, 0x5E) : ARGB(255, 0xF5, 0xB0, 0x14), &b);
    GdipFillEllipse(g, b, SF(24), SF(707), SF(8), SF(8));
    GdipDeleteBrush(b);
    GdipDeleteGraphics(g);

    /* ---- text (GDI, ClearType) ---- */
    DrawIconEx(dc, S(20), S(18), g_logo, S(40), S(40), 0, NULL, DI_NORMAL);
    text(dc, f_title, C_TEXT, rc(70, 16, 250, 26), L"TurkishDPI", DT_LEFT | DT_SINGLELINE);
    text(dc, f_sub, C_SUB, rc(70, 42, 250, 18), L"Akıllı DPI kalkanı · v" TURKISHDPI_VERSION_W,
         DT_LEFT | DT_SINGLELINE);
    {
        RECT pill = rc(330, 24, 90, 24);
        HBRUSH pb = CreateSolidBrush(g_auto ? RGB(0x14, 0x3D, 0x26) : RGB(0x3A, 0x2E, 0x10));
        HPEN np = SelectObject(dc, GetStockObject(NULL_PEN));
        HBRUSH ob = SelectObject(dc, pb);
        RoundRect(dc, pill.left, pill.top, pill.right, pill.bottom, S(24), S(24));
        SelectObject(dc, ob);
        SelectObject(dc, np);
        DeleteObject(pb);
        text(dc, f_label, g_auto ? C_GREEN : C_AMBER, pill, g_auto ? L"OTOMATİK" : L"MANUEL",
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    text(dc, f_big, C_TEXT, rc(20, 246, 400, 30), st.status, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    text(dc, f_small, C_SUB, rc(30, 278, 380, 34), st.detail, DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS);

    /* cards */
    text(dc, f_label, C_FAINT, rc(34, 328, 110, 14), L"AĞ", DT_LEFT | DT_SINGLELINE);
    text(dc, f_value, C_TEXT, rc(34, 346, 106, 22), st.network[0] ? st.network : L"—",
         DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    text(dc, f_label, C_FAINT, rc(170, 328, 110, 14), L"GECİKME", DT_LEFT | DT_SINGLELINE);
    if (st.avg_ms > 0) swprintf(buf, 32, L"%d ms", st.avg_ms); else wcscpy(buf, L"—");
    text(dc, f_value, C_TEXT, rc(170, 346, 106, 22), buf, DT_LEFT | DT_SINGLELINE);
    text(dc, f_label, C_FAINT, rc(306, 328, 110, 14), L"ÖĞRENİLEN", DT_LEFT | DT_SINGLELINE);
    swprintf(buf, 32, L"%d / %d profil", st.learned, PRESET_COUNT);
    text(dc, f_value, C_TEXT, rc(306, 346, 106, 22), buf, DT_LEFT | DT_SINGLELINE);

    /* sites */
    text(dc, f_label, C_FAINT, rc(36, 406, 200, 14), L"TEST SİTELERİ", DT_LEFT | DT_SINGLELINE);
    if (st.preset >= 0) {
        wchar_t ip[32];
        MultiByteToWideChar(CP_ACP, 0, DNS_SERVERS[st.dns].v4, -1, ip, 32);
        swprintf(buf, 160, L"DNS %ls:%u", ip, DNS_SERVERS[st.dns].port);
        text(dc, f_label, C_FAINT, rc(200, 406, 204, 14), buf, DT_RIGHT | DT_SINGLELINE);
    }
    for (int i = 0; i < st.ntargets && i < PROBE_MAX_HOSTS; i++) {
        int y = 428 + (int)(i * 16.5f);
        text(dc, f_small, C_TEXT, rc(54, y, 240, 18), st.target[i], DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (st.target_ok[i] < 0) wcscpy(buf, L"—");
        else if (st.target_ok[i]) swprintf(buf, 32, L"%d ms", st.target_ms[i]);
        else wcscpy(buf, L"engelli");
        text(dc, f_small, st.target_ok[i] == 0 ? C_RED : C_SUB, rc(300, y, 104, 18), buf,
             DT_RIGHT | DT_SINGLELINE);
    }
    if (!st.ntargets)
        text(dc, f_small, C_SUB, rc(36, 430, 368, 18), L"Test sitesi yok", DT_LEFT | DT_SINGLELINE);

    /* live engine counters */
    if (st.st_packets) {
        swprintf(buf, 160, L"%lu paket · %lu HTTPS · %lu HTTP · %lu DNS · %lu ödeme atlandı",
                 st.st_packets, st.st_https, st.st_http, st.st_dns, st.st_excluded);
        text(dc, f_label, C_FAINT, rc(36, 546, 368, 14), buf, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    /* banner text */
    if (g_banner_kind) {
        if (g_banner_kind == 1)
            swprintf(buf, 160, L"Yeni sürüm hazır: %ls – indirmek için tıklayın", st.update_tag);
        else if (g_banner_kind == 2)
            wcscpy(buf, L"Sistem proxy'si kapalı bir programı gösteriyor – onarmak için tıklayın");
        else if (g_banner_kind == 3)
            swprintf(buf, 160, L"%ls çalışıyor – çakışmayı önlemek için kapatın", st.rival);
        else
            wcscpy(buf, L"Antivirüs sürücüyü engelledi – çözüm için tıklayın");
        text(dc, f_small, RGB(255, 255, 255), r_banner, buf, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    text(dc, f_value, RGB(255, 255, 255), r_test, L"Test et", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text(dc, f_small, C_SUB, rc(20, 622, 400, 18),
         st.site_result[0] ? st.site_result : L"Açılmayan bir siteyi yazın; TurkishDPI çözümü kendisi arasın.",
         DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    text(dc, f_value, C_TEXT, r_rescan, L"⟳  Yeniden tara", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text(dc, f_value, C_TEXT, r_profile, L"Profil  ▾", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text(dc, f_value, C_TEXT, r_settings, L"Ayarlar  ▾", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    swprintf(buf, 160, g_payment_protect
             ? L"Ödeme koruması açık · %d+ banka ve ödeme alan adına dokunulmaz"
             : L"Ödeme koruması kapalı", g_payment_domains);
    text(dc, f_small, g_payment_protect ? C_SUB : C_AMBER, rc(38, 702, 390, 18), buf,
         DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
}

/* ---------- tray ---------- */
static void tray_update(void) {
    ui_state_t st;
    EnterCriticalSection(&g_lock);
    st = g_ui;
    LeaveCriticalSection(&g_lock);
    g_nid.uFlags = NIF_TIP | NIF_ICON | NIF_MESSAGE;
    g_nid.hIcon = g_tray_icons[tray_icon_index(st.kind)];
    swprintf(g_nid.szTip, 128, L"TurkishDPI – %ls", st.status);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void tray_add(void) {
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_wnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = g_tray_icons[2];
    wcscpy(g_nid.szTip, L"TurkishDPI");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_nid.uVersion = NOTIFYICON_VERSION;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
}

/* ---------- menus ---------- */
static void dark_menus(void) {
    /* Undocumented but stable since Win10 1903: uxtheme #135 SetPreferredAppMode. */
    typedef int (WINAPI *set_mode_fn)(int);
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
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
    POINT p = { r_profile.left, r_profile.bottom };
    AppendMenuW(m, MF_STRING | (g_auto ? MF_CHECKED : 0), IDM_AUTO, L"Otomatik (önerilen)");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    for (int i = 0; i < PRESET_COUNT; i++)
        AppendMenuW(m, MF_STRING | ((!g_auto && g_preset == i) ? MF_CHECKED : 0),
                    IDM_PRESET + i, PRESETS[i].name);
    ClientToScreen(g_wnd, &p);
    popup(m, p.x, p.y - S(4) - S(40) * 0);
}

static void settings_menu(void) {
    HMENU m = CreatePopupMenu();
    POINT p = { r_settings.left, r_settings.bottom };
    AppendMenuW(m, MF_STRING | (g_auto ? MF_CHECKED : 0), IDM_AUTO, L"Otomatik mod");
    AppendMenuW(m, MF_STRING | (autostart_enabled() ? MF_CHECKED : 0), IDM_AUTOSTART, L"Windows ile başlat");
    AppendMenuW(m, MF_STRING | (g_payment_protect ? MF_CHECKED : 0), IDM_PAYMENT,
                L"Ödeme ve banka koruması");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_LOG, L"Günlüğü aç");
    AppendMenuW(m, MF_STRING, IDM_INI, L"Gelişmiş ayarlar (ini)");
    AppendMenuW(m, MF_STRING, IDM_AVHELP, L"Antivirüs sorunu yardımı");
    AppendMenuW(m, MF_STRING, IDM_GITHUB, L"GitHub sayfası");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_UNINSTALL, L"Kaldır ve temizle…");
    AppendMenuW(m, MF_STRING, IDM_EXIT, L"Çıkış");
    ClientToScreen(g_wnd, &p);
    popup(m, p.x, p.y);
}

void ui_tray_menu(HWND owner) {
    HMENU m = CreatePopupMenu();
    POINT p;
    ui_state_t st;
    (void)owner;
    EnterCriticalSection(&g_lock);
    st = g_ui;
    LeaveCriticalSection(&g_lock);
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, st.status);
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
static int hit(int x, int y) {
    POINT p = { x, y };
    float cx = (r_power.left + r_power.right) / 2.0f, cy = (r_power.top + r_power.bottom) / 2.0f;
    float r = (r_power.right - r_power.left) / 2.0f;
    if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) return HIT_POWER;
    if (PtInRect(&r_rescan, p)) return HIT_RESCAN;
    if (PtInRect(&r_profile, p)) return HIT_PROFILE;
    if (PtInRect(&r_settings, p)) return HIT_SETTINGS;
    if (PtInRect(&r_test, p)) return HIT_TEST;
    if (g_banner_kind && PtInRect(&r_banner, p)) return HIT_BANNER;
    return HIT_NONE;
}

static void do_site_test(void) {
    wchar_t buf[256];
    GetWindowTextW(g_edit, buf, 256);
    if (buf[0]) post_site_test(buf);
}

static LRESULT CALLBACK edit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN) { do_site_test(); return 0; }
    if (msg == WM_CHAR && (wp == L'\r' || wp == L'\n')) return 0;
    return CallWindowProcW(g_edit_proc, h, msg, wp, lp);
}

static void command(int id);

static void click(int id) {
    switch (id) {
    case HIT_POWER:    post_cmd(g_paused ? CMD_RESUME : CMD_PAUSE, 0); break;
    case HIT_RESCAN:   post_cmd(CMD_RESCAN, 0); break;
    case HIT_PROFILE:  profile_menu(); break;
    case HIT_SETTINGS: settings_menu(); break;
    case HIT_TEST:     do_site_test(); break;
    case HIT_BANNER:
        command(g_banner_kind == 1 ? IDM_UPDATE : g_banner_kind == 2 ? IDM_PROXY
              : g_banner_kind == 4 ? IDM_AVHELP : 0);
        break;
    }
}

static void command(int id) {
    wchar_t buf[MAX_PATH + 8];
    if (id >= IDM_PRESET && id < IDM_PRESET + PRESET_COUNT) {
        post_cmd(CMD_SELECT, id - IDM_PRESET);
        return;
    }
    switch (id) {
    case IDM_AUTO:      post_cmd(CMD_AUTO, 0); break;
    case IDM_AUTOSTART: autostart_set(!autostart_enabled()); break;
    case IDM_PAYMENT:
        if (g_payment_protect &&
            MessageBoxW(g_wnd, L"Ödeme koruması kapatılırsa banka ve ödeme sitelerinin trafiği de "
                               L"değiştirilir. Bu, 3D Secure ve ödeme hatalarına yol açabilir.\n\n"
                               L"Yine de kapatılsın mı?", L"TurkishDPI",
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            break;
        set_payment_protect(!g_payment_protect);
        InvalidateRect(g_wnd, NULL, FALSE);
        break;
    case IDM_LOG:       ShellExecuteW(NULL, L"open", L"notepad.exe", g_log, NULL, SW_SHOWNORMAL); break;
    case IDM_INI:       ShellExecuteW(NULL, L"open", L"notepad.exe", g_ini, NULL, SW_SHOWNORMAL); break;
    case IDM_GITHUB:    ShellExecuteW(NULL, L"open", TURKISHDPI_URL, NULL, NULL, SW_SHOWNORMAL); break;
    case IDM_EXIT:      app_exit(); break;
    case IDM_UPDATE:    ShellExecuteW(NULL, L"open", TURKISHDPI_RELEASES_URL, NULL, NULL, SW_SHOWNORMAL); break;
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
            L"2) Aynı yerde Ayarları yönet > Dışlamalar > Klasör olarak şunları ekleyin:\n"
            L"     C:\\ProgramData\\TurkishDPI\n"
            L"     TurkishDPI.exe'nin bulunduğu klasör\n"
            L"3) Kaspersky kullanıyorsanız kaldırmanız gerekebilir.\n\n"
            L"Tamam'a basınca Windows Güvenliği açılır.", L"Antivirüs yardımı", MB_ICONINFORMATION);
        ShellExecuteW(NULL, L"open", L"windowsdefender://threat", NULL, NULL, SW_SHOWNORMAL);
        break;
    case IDM_UNINSTALL:
        if (MessageBoxW(g_wnd, L"TurkishDPI durdurulacak; Windows başlangıç görevi, ayarlar, öğrenilen "
                               L"bilgiler, günlük ve WinDivert sürücüsü kaldırılacak.\n\nDevam edilsin mi?",
                        L"Kaldır ve temizle", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES)
            app_uninstall();
        break;
    case IDM_SHOW:      ui_show(); break;
    case IDM_PAUSE:     post_cmd(g_paused ? CMD_RESUME : CMD_PAUSE, 0); break;
    case IDM_RESCAN:    post_cmd(CMD_RESCAN, 0); break;
    }
    (void)buf;
}

static void apply_dpi(UINT dpi) {
    g_scale = dpi / 96.0f;
    fonts_create();
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
    RECT r = { 0, 0, S(BASE_W), S(BASE_H) };
    DWORD style = (DWORD)GetWindowLongW(g_wnd, GWL_STYLE);
    AdjustWindowRect(&r, style, FALSE);
    SetWindowPos(g_wnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void dark_titlebar(HWND h) {
    typedef HRESULT (WINAPI *dwm_fn)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE m = LoadLibraryExW(L"dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (m) {
        dwm_fn f = (dwm_fn)(void *)GetProcAddress(m, "DwmSetWindowAttribute");
        BOOL on = TRUE;
        COLORREF cap = C_BG;
        if (f) {
            f(h, 20, &on, sizeof(on));      /* DWMWA_USE_IMMERSIVE_DARK_MODE */
            f(h, 35, &cap, sizeof(cap));    /* DWMWA_CAPTION_COLOR (Win11)   */
        }
    }
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_taskbar_created && msg) { tray_add(); tray_update(); return 0; }
    switch (msg) {
    case WM_CREATE:
        g_wnd = h;
        apply_dpi(window_dpi(h));
        g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                 0, 0, 10, 10, h, (HMENU)1, g_inst, NULL);
        SendMessageW(g_edit, WM_SETFONT, (WPARAM)f_body, FALSE);
        SendMessageW(g_edit, EM_SETCUEBANNER, TRUE, (LPARAM)L"ör. discord.com");
        SendMessageW(g_edit, EM_SETLIMITTEXT, 200, 0);
        g_edit_proc = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
        layout();
        return 0;
    case WM_DPICHANGED: {
        RECT *r = (RECT *)lp;
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
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, C_TEXT);
        SetBkColor((HDC)wp, RGB(0x15, 0x18, 0x21));
        return (LRESULT)g_card_brush;
    case WM_MOUSEMOVE: {
        int ht = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (!g_tracking) {
            TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
            g_tracking = 1;
        }
        if (ht != g_hover) { g_hover = ht; InvalidateRect(h, NULL, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_tracking = 0;
        if (g_hover) { g_hover = 0; InvalidateRect(h, NULL, FALSE); }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && g_hover) { SetCursor(LoadCursor(NULL, IDC_HAND)); return TRUE; }
        break;
    case WM_LBUTTONUP:
        click(hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)));
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) >= 1000) command(LOWORD(wp));
        return 0;
    case WM_TIMER:
        g_anim = (g_anim + 6) % 360;
        InvalidateRect(h, &r_power, FALSE);
        return 0;
    case WM_TRAY:
        if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK) ui_toggle();
        else if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) ui_tray_menu(h);
        return 0;
    case WM_STATUS:
        tray_update();
        if (IsWindowVisible(h)) {
            int spin = g_ui.kind == ST_SCANNING || g_ui.kind == ST_STARTING;
            if (spin) SetTimer(h, 1, 33, NULL); else KillTimer(h, 1);
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);
        KillTimer(h, 1);
        SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC) app_network_event();
        return TRUE;
    case WM_ENDSESSION:
        if (wp) app_exit();
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
    GdiplusStartup(&g_gdip, &gi, NULL);
    dark_menus();
    g_card_brush = CreateSolidBrush(RGB(0x15, 0x18, 0x21));
    g_logo = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 128, 128, 0);
    for (int i = 0; i < 5; i++)
        g_tray_icons[i] = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(i == 0 ? 1 : 10 + i), IMAGE_ICON,
                                            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = g_tray_icons[0];
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"TurkishDPI_main";
    if (!RegisterClassExW(&wc)) return 0;

    g_wnd = CreateWindowExW(0, wc.lpszClassName, L"TurkishDPI",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, NULL, NULL, inst, NULL);
    if (!g_wnd) return 0;
    dark_titlebar(g_wnd);
    fit_window();
    {
        /* centre on the work area */
        RECT wr, wa;
        GetWindowRect(g_wnd, &wr);
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        SetWindowPos(g_wnd, NULL, wa.left + ((wa.right - wa.left) - (wr.right - wr.left)) / 2,
                     wa.top + ((wa.bottom - wa.top) - (wr.bottom - wr.top)) / 2, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    g_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
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
