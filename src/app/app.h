/*
 * TurkishDPI - shared state between the controller thread and the UI.
 */
#ifndef TURKISHDPI_APP_H
#define TURKISHDPI_APP_H
#include <windows.h>
#include "probe.h"

#define WM_TRAY    (WM_APP + 1)
#define WM_STATUS  (WM_APP + 2)

typedef enum {
    ST_STARTING, ST_SCANNING, ST_ACTIVE, ST_PARTIAL, ST_STANDBY,
    ST_PAUSED, ST_OFFLINE, ST_ERROR
} state_kind_t;

enum { CMD_NONE, CMD_RESCAN, CMD_SELECT, CMD_AUTO, CMD_PAUSE, CMD_RESUME,
       CMD_TESTSITE, CMD_RESTART, CMD_TOGGLE_PAUSE, CMD_AUTOSTART };

typedef struct {
    state_kind_t kind;
    wchar_t status[128];        /* one line headline              */
    wchar_t detail[160];        /* second line (profile, hint)    */
    wchar_t network[64];        /* adapter name                   */
    int preset;                 /* running preset or -1           */
    int dns;
    int scan_pos, scan_total;
    int ntargets;
    wchar_t target[PROBE_MAX_HOSTS][64];
    int target_ok[PROBE_MAX_HOSTS];    /* -1 unknown, 0 fail, 1 ok */
    int target_ms[PROBE_MAX_HOSTS];
    int avg_ms;                 /* average TCP+TLS set-up time        */
    int dns_ms;                 /* round trip of the chosen clean DNS */
    int hybrid;                 /* hybrid DNS on                      */
    int n_block_ips;            /* learned block-page addresses       */
    long driver_err;            /* last WinDivert error code, 0 = none */
    int autostart;              /* Windows logon task present (-1 unknown) */
    int learned;                /* presets with history on this net */
    wchar_t site_result[160];   /* result of the last site test   */
    DWORD started_tick;         /* when the current preset started */
    /* live engine counters */
    unsigned long st_packets, st_https, st_http, st_excluded, st_dns;
    /* notices shown as a banner */
    wchar_t rival[48];          /* another bypass/VPN tool running     */
    int proxy_broken;           /* system proxy points at nothing      */
    wchar_t update_tag[32];     /* newer release available             */
} ui_state_t;

/* app.c */
extern CRITICAL_SECTION g_lock;
extern ui_state_t g_ui;
extern int g_auto, g_preset, g_paused, g_payment_protect;
extern wchar_t g_log[MAX_PATH], g_ini[MAX_PATH];
void post_cmd(LONG cmd, LONG arg);
void post_site_test(const wchar_t *host);
int  autostart_enabled(void);
void autostart_set(int on);
void app_exit(void);
void set_payment_protect(int on);
void app_network_event(void);
void app_open_url(const wchar_t *url);   /* in the user's (non-elevated) shell */
void app_open_file(const wchar_t *path); /* Notepad from System32 */
void app_repair_proxy(void);
void app_uninstall(void);

/* ui.c */
int  ui_init(HINSTANCE inst);
void ui_show(void);
void ui_toggle(void);
void ui_refresh(void);
HWND ui_hwnd(void);
void ui_tray_menu(HWND owner);
extern int g_payment_domains;

/* smart.c */
void smart_network_changed(void);           /* re-read network identity      */
const wchar_t *smart_network_name(void);
const wchar_t *smart_network_id(void);
void smart_order(int *order, int n);         /* best-first preset order       */
void smart_record(int preset, int ok, int avg_ms);
int  smart_known(int preset);                /* has history on this network   */
int  smart_learned_count(void);
int  smart_get_dns(int def);
void smart_set_dns(int dns);
int  smart_best(void);                        /* -1 if nothing learned          */

#endif
