/*
 * TurkishDPI - automatic DPI circumvention for Turkey.
 *
 * One small native executable with two roles:
 *
 *   TurkishDPI.exe                 the app: finds a working strategy by itself
 *                                  (learning per network), runs it, watches it
 *                                  and re-tunes when something changes.
 *   TurkishDPI.exe --engine ARGS   the packet engine (GoodbyeDPI-compatible
 *                                  options). Also used directly when the first
 *                                  argument looks like a GoodbyeDPI option,
 *                                  so old .cmd scripts and services keep working.
 *
 * Copyright 2026 TurkishDPI contributors. Apache License 2.0.
 * Engine: GoodbyeDPI (ValdikSS) / GoodbyeDPI-Turkey (cagritaskn).
 * Auto-detection, watchdog and SNI strategies inspired by Shroudly (umutxyp, MIT).
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "sysdll.h"
#include "app.h"
#include "presets.h"
#include "version.h"
#include "system.h"

int gdpi_main(int argc, char *argv[]);
static DWORD run_hidden(const wchar_t *exe, const wchar_t *args);
static void autostart_query(void);
static void autostart_migrate(void);

/* ---------- shared state ---------- */
CRITICAL_SECTION g_lock;
ui_state_t g_ui;
int g_auto = 1, g_preset = 0, g_paused = 0, g_payment_protect = 1;
wchar_t g_log[MAX_PATH], g_ini[MAX_PATH];

static wchar_t g_dir[MAX_PATH], g_exe[MAX_PATH];
/* The running exe's file, opened at start without delete sharing: it cannot
 * be renamed and swapped for another file while we run, and it is what gets
 * copied into Program Files for the logon task (sys_install_copy). */
static HANDLE g_self_exe = INVALID_HANDLE_VALUE;
static HANDLE g_job, g_cmd_event, g_net_event, g_resume_event, g_quit_event;
static CRITICAL_SECTION g_log_lock;   /* file I/O never blocks the UI's g_lock */
/* Small command queue: clicks are never lost or merged (UI -> controller). */
#define CMD_QUEUE 16
static struct { LONG cmd, arg; } g_cmdq[CMD_QUEUE];
static int g_cmdq_head, g_cmdq_count;
static volatile LONG g_quitting = 0;  /* uninstall/exit in progress: start nothing */
static volatile LONG g_scan_aborted = 0;
static int g_last_score = -1;         /* score that put us in ST_PARTIAL */
static long g_last_fatal = 0;         /* last driver error that was announced */
static HANDLE g_reader;               /* engine stdout reader thread */
static wchar_t g_site_req[256];
static int g_dns = 0, g_running_preset = -1, g_quiet_start = 0;
static int g_watchdog_min = 10;
static int g_dns_hybrid = 1;          /* DnsHybrid in the ini */
static char g_block_ips[8][16];       /* block-page answers learned on this network */
static int g_n_block_ips = 0;
static int g_selftest = 0;        /* --selftest: scan, verify, log, exit */
static wchar_t g_rival_notified[48];

static PROCESS_INFORMATION g_engine;
static char g_running_args[1024];     /* arguments of the running engine */
static HANDLE g_engine_active;
static volatile LONG g_engine_err = 0;

static wchar_t g_targets_buf[512] = L"discord.com,www.roblox.com,www.wattpad.com";
static wchar_t g_control_buf[256] = L"www.google.com,www.microsoft.com";
static const wchar_t *g_targets[PROBE_MAX_HOSTS], *g_control[PROBE_MAX_HOSTS];
static int g_ntargets, g_ncontrol;

/* ---------- logging ---------- */
#define LOG_MAX_BYTES (1024 * 1024)

static void log_write(const char *data, DWORD len) {
    HANDLE f;
    DWORD w;
    LARGE_INTEGER size;
    int rotate = 0;
    EnterCriticalSection(&g_log_lock);
    f = CreateFileW(g_log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        WriteFile(f, data, len, &w, NULL);
        /* rotated while running too: an instance that stays up for months
         * must not grow the log without bound */
        rotate = GetFileSizeEx(f, &size) && size.QuadPart > LOG_MAX_BYTES;
        CloseHandle(f);
    }
    if (rotate) {
        wchar_t old[MAX_PATH];
        _snwprintf(old, MAX_PATH - 1, L"%ls.old", g_log);
        old[MAX_PATH - 1] = 0;
        MoveFileExW(g_log, old, MOVEFILE_REPLACE_EXISTING);
    }
    LeaveCriticalSection(&g_log_lock);
}

static void log_line(const char *fmt, ...) {
    char buf[1024];
    SYSTEMTIME t;
    va_list ap;
    int n;
    GetLocalTime(&t);
    n = snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d [app] ",
                 t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof(buf) - (size_t)n - 3, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(buf) - 3) n = (int)sizeof(buf) - 3;
    buf[n++] = '\r'; buf[n++] = '\n';
    log_write(buf, (DWORD)n);
}

static void log_rotate(void) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExW(g_log, GetFileExInfoStandard, &a) &&
        (a.nFileSizeHigh || a.nFileSizeLow > 1024 * 1024)) {
        wchar_t old[MAX_PATH];
        swprintf(old, MAX_PATH, L"%ls.old", g_log);
        MoveFileExW(g_log, old, MOVEFILE_REPLACE_EXISTING);
    }
}

/* ---------- state updates (worker thread -> UI) ---------- */
static void set_state(state_kind_t k, const wchar_t *status, const wchar_t *detail) {
    EnterCriticalSection(&g_lock);
    g_ui.kind = k;
    if (status) { wcsncpy(g_ui.status, status, 127); g_ui.status[127] = 0; }
    if (detail) { wcsncpy(g_ui.detail, detail, 159); g_ui.detail[159] = 0; }
    g_ui.preset = g_running_preset;
    g_ui.dns = g_dns;
    LeaveCriticalSection(&g_lock);
    ui_refresh();
}

static void set_targets_ui(const int *ok, const int *ms) {
    int sum = 0, cnt = 0;
    EnterCriticalSection(&g_lock);
    g_ui.ntargets = g_ntargets;
    for (int i = 0; i < g_ntargets; i++) {
        wcsncpy(g_ui.target[i], g_targets[i], 63);
        g_ui.target[i][63] = 0;
        g_ui.target_ok[i] = ok ? ok[i] : -1;
        g_ui.target_ms[i] = ms ? ms[i] : -1;
        if (ok && ok[i] && ms && ms[i] > 0) { sum += ms[i]; cnt++; }
    }
    g_ui.avg_ms = cnt ? sum / cnt : -1;
    LeaveCriticalSection(&g_lock);
    ui_refresh();
}

static int notify(const wchar_t *title, const wchar_t *text) {
    NOTIFYICONDATAW n;
    if (g_quiet_start) return 0;
    ZeroMemory(&n, sizeof(n));
    n.cbSize = sizeof(n);
    n.hWnd = ui_hwnd();
    n.uID = 1;
    n.uFlags = NIF_INFO;
    wcsncpy(n.szInfoTitle, title, 63);
    wcsncpy(n.szInfo, text, 255);
    n.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON | NIIF_NOSOUND;
    n.hBalloonIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));
    return Shell_NotifyIconW(NIM_MODIFY, &n) ? 1 : 0;
}

/* Kind of state the user was last told about with a balloon (see notify_state). */
static state_kind_t g_notified_kind = ST_STARTING;

/* ---------- why the controller is doing what it does ---------- */
/* Set before each scan/start; written to the log ("decision: ... reason=")
 * and shown next to the profile name, so a re-tune is never unexplained. */
typedef enum {
    WHY_STARTUP, WHY_USER_RESCAN, WHY_USER_AUTO, WHY_USER_PROFILE, WHY_RESUME_PAUSE,
    WHY_SETTINGS, WHY_NETWORK, WHY_WAKE, WHY_WATCHDOG, WHY_CRASH, WHY_ONLINE, WHY_SITE_TEST
} why_t;

static const struct { const char *code; const wchar_t *text; } WHY[] = {
    { "startup",       L"açılışta seçildi" },
    { "user_rescan",   L"yeniden taramada seçildi" },
    { "user_auto",     L"otomatik moda geçince seçildi" },
    { "user_profile",  L"elle seçildi" },
    { "resume",        L"devam ettirildi" },
    { "settings",      L"ayar değişince yeniden başlatıldı" },
    { "network",       L"ağ değişince seçildi" },
    { "wake",          L"uykudan dönünce seçildi" },
    { "watchdog",      L"bekçi sorun bulunca seçildi" },
    { "engine_crash",  L"motor kapanınca yeniden başlatıldı" },
    { "online",        L"internet gelince seçildi" },
    { "site_test",     L"site testi için seçildi" },
};
static const char *g_reason_code = "startup";
static const wchar_t *g_reason_text = L"açılışta seçildi";

static void set_reason(why_t w) {
    g_reason_code = WHY[w].code;
    g_reason_text = WHY[w].text;
}

/* ---------- config ---------- */
static int ini_int(const wchar_t *key, int def) {
    return (int)GetPrivateProfileIntW(L"TurkishDPI", key, def, g_ini);
}

static void ini_set_int(const wchar_t *key, int v) {
    wchar_t b[16];
    swprintf(b, 16, L"%d", v);
    WritePrivateProfileStringW(L"TurkishDPI", key, b, g_ini);
}

static int split_hosts(wchar_t *buf, const wchar_t **out) {
    /* Portable tokenizer: wcstok's signature differs between msvcrt and ucrt. */
    int n = 0;
    wchar_t *p = buf;
    while (*p && n < PROBE_MAX_HOSTS) {
        while (*p == L',' || *p == L' ' || *p == L';') *p++ = 0;
        if (!*p) break;
        out[n++] = p;
        while (*p && *p != L',' && *p != L' ' && *p != L';') p++;
    }
    return n;
}

static void targets_reload(void) {
    GetPrivateProfileStringW(L"TurkishDPI", L"Targets", g_targets_buf, g_targets_buf,
                             sizeof(g_targets_buf) / sizeof(wchar_t), g_ini);
    g_ntargets = split_hosts(g_targets_buf, g_targets);
    if (g_ntargets == 0) {   /* an empty list would make every preset "work" */
        wcscpy(g_targets_buf, L"discord.com,www.roblox.com,www.wattpad.com");
        g_ntargets = split_hosts(g_targets_buf, g_targets);
    }
    set_targets_ui(NULL, NULL);
}

static void config_load(void) {
    g_auto = ini_int(L"Auto", 1) != 0;
    g_preset = ini_int(L"Preset", 0);
    if (g_preset < 0 || g_preset >= PRESET_COUNT) g_preset = 0;
    g_dns = ini_int(L"Dns", 0);
    if (g_dns < 0 || g_dns >= DNS_SERVER_COUNT) g_dns = 0;
    g_payment_protect = ini_int(L"PaymentProtection", 1) != 0;
    g_dns_hybrid = ini_int(L"DnsHybrid", 1) != 0;
    g_watchdog_min = ini_int(L"WatchdogMinutes", 10);
    if (g_watchdog_min < 1) g_watchdog_min = 1;
    if (g_watchdog_min > 240) g_watchdog_min = 240;   /* minutes * 60000 must fit a DWORD */
    GetPrivateProfileStringW(L"TurkishDPI", L"Control", g_control_buf, g_control_buf,
                             sizeof(g_control_buf) / sizeof(wchar_t), g_ini);
    g_ncontrol = split_hosts(g_control_buf, g_control);
    if (g_ncontrol == 0) {   /* no control site = "offline" forever */
        wcscpy(g_control_buf, L"www.google.com,www.microsoft.com");
        g_ncontrol = split_hosts(g_control_buf, g_control);
    }
    g_ui.hybrid = g_dns_hybrid;
    targets_reload();
}

static void config_save(void) {
    ini_set_int(L"Auto", g_auto);
    ini_set_int(L"Preset", g_preset);
    ini_set_int(L"Dns", g_dns);
    ini_set_int(L"PaymentProtection", g_payment_protect);
    ini_set_int(L"DnsHybrid", g_dns_hybrid);
    ini_set_int(L"WatchdogMinutes", g_watchdog_min);
}

static int targets_add(const wchar_t *host) {
    wchar_t buf[512];
    for (int i = 0; i < g_ntargets; i++)
        if (!_wcsicmp(g_targets[i], host)) return 1;
    if (g_ntargets >= PROBE_MAX_HOSTS) return 0;
    buf[0] = 0;
    {
        size_t used = 0, need;
        for (int i = 0; i < g_ntargets; i++) {
            need = wcslen(g_targets[i]) + 1;
            if (used + need >= 512) return 0;
            memcpy(buf + used, g_targets[i], (need - 1) * sizeof(wchar_t));
            buf[used + need - 1] = L',';
            used += need;
        }
        need = wcslen(host);
        if (used + need >= 512) return 0;   /* would not fit: do not save it truncated */
        memcpy(buf + used, host, need * sizeof(wchar_t));
        buf[used + need] = 0;
    }
    WritePrivateProfileStringW(L"TurkishDPI", L"Targets", buf, g_ini);
    wcscpy(g_targets_buf, buf);
    g_ntargets = split_hosts(g_targets_buf, g_targets);
    log_line("learned new target site");
    set_targets_ui(NULL, NULL);
    return 1;
}

/* ---------- engine process control ---------- */
static void build_args(int preset, int dns, char *out, size_t cap) {
    const char *src = PRESETS[preset].args;
    const char *tag = strstr(src, "{DNS}");
    char dnsargs[512];
    const dns_server_t *d = &DNS_SERVERS[dns];
    size_t n;

    snprintf(dnsargs, sizeof(dnsargs),
             "--dns-addr %s --dns-port %u --dnsv6-addr %s --dnsv6-port %u%s",
             d->v4, d->port, d->v6, d->port, g_dns_hybrid ? " --dns-hybrid" : "");
    for (int i = 0; g_dns_hybrid && i < g_n_block_ips; i++) {
        size_t l = strlen(dnsargs);
        snprintf(dnsargs + l, sizeof(dnsargs) - l, " --dns-block-ip %s", g_block_ips[i]);
    }
    if (tag)
        snprintf(out, cap, "%.*s%s%s", (int)(tag - src), src, dnsargs, tag + 5);
    else
        snprintf(out, cap, "%s", src);
    n = strlen(out);
    if (!g_payment_protect && n + 24 < cap)
        strcat(out, " --no-payment-exclude");
}

static DWORD WINAPI engine_reader(LPVOID p) {
    HANDLE pipe = (HANDLE)p;
    char buf[2048], line[514];
    size_t ll = 0;
    DWORD n;
    while (ReadFile(pipe, buf, sizeof(buf), &n, NULL) && n) {
        for (DWORD i = 0; i < n; i++) {
            int eol = buf[i] == '\n';
            if (!eol && buf[i] != '\r' && ll < sizeof(line) - 1) {
                line[ll++] = buf[i];
                if (ll < sizeof(line) - 1) continue;
                eol = 1;   /* over-long line: flush it, nothing is dropped */
            }
            if (eol) {
                line[ll] = 0;
                if (strncmp(line, "STATS ", 6) != 0) {   /* counters stay out of the log */
                    line[ll] = '\r';
                    line[ll + 1 < sizeof(line) ? ll + 1 : ll] = '\n';
                    log_write(line, (DWORD)(ll + 2 <= sizeof(line) ? ll + 2 : ll + 1));
                    line[ll] = 0;
                }
                if (!strncmp(line, "STATS ", 6)) {
                    unsigned long a = 0, b = 0, c = 0, d = 0, e = 0;
                    if (sscanf(line + 6, "packets=%lu https=%lu http=%lu excluded=%lu dns=%lu",
                               &a, &b, &c, &d, &e) == 5) {
                        EnterCriticalSection(&g_lock);
                        g_ui.st_packets = a; g_ui.st_https = b; g_ui.st_http = c;
                        g_ui.st_excluded = d; g_ui.st_dns = e;
                        LeaveCriticalSection(&g_lock);
                        ui_refresh();
                    }
                    ll = 0;
                    continue;
                }
                if (strstr(line, "Filter activated"))
                    SetEvent(g_engine_active);
                else if (!strncmp(line, "Error opening filter: ", 22))
                    InterlockedExchange(&g_engine_err, atol(line + 22));
                ll = 0;
            }
        }
    }
    CloseHandle(pipe);
    return 0;
}

static void engine_stop(void) {
    PROCESS_INFORMATION pi;
    HANDLE reader;
    EnterCriticalSection(&g_lock);
    pi = g_engine;
    ZeroMemory(&g_engine, sizeof(g_engine));
    reader = g_reader;
    g_reader = NULL;
    g_running_preset = -1;
    g_running_args[0] = 0;
    LeaveCriticalSection(&g_lock);
    /* slow part outside the lock, so painting never waits on it */
    if (pi.hProcess) {
        TerminateProcess(pi.hProcess, 0);
        WaitForSingleObject(pi.hProcess, 3000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    if (reader) {
        WaitForSingleObject(reader, 2000);   /* pipe closes with the process */
        CloseHandle(reader);
    }
}

static int engine_alive(void) {
    return g_engine.hProcess && WaitForSingleObject(g_engine.hProcess, 0) == WAIT_TIMEOUT;
}

/* Returns 1 when the filter is active, 0 on failure (g_engine_err may be set). */
static int engine_start(int preset, int dns) {
    char args[1024];
    wchar_t cmd[2048], wargs[1024];
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE rd = NULL, wr = NULL, waits[2], t;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD r;

    build_args(preset, dns, args, sizeof(args));
    /* Already running with exactly these arguments: keep it. Restarting
     * would only drop the user's connections for nothing (a watchdog or
     * rescan that ends on the same preset). */
    if (g_running_preset >= 0 && engine_alive() && !strcmp(args, g_running_args)) {
        g_running_preset = preset;
        return 1;
    }
    engine_stop();
    if (g_quitting) return 0;
    MultiByteToWideChar(CP_ACP, 0, args, -1, wargs, 1024);
    swprintf(cmd, 2048, L"\"%ls\" --engine %ls", g_exe, wargs);
    log_line("starting engine: preset %d: %s", preset, args);

    if (!CreatePipe(&rd, &wr, &sa, 0)) return 0;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;

    ResetEvent(g_engine_active);
    InterlockedExchange(&g_engine_err, 0);
    EnterCriticalSection(&g_lock);
    g_ui.st_packets = g_ui.st_https = g_ui.st_http = g_ui.st_excluded = g_ui.st_dns = 0;
    LeaveCriticalSection(&g_lock);
    if (!CreateProcessW(g_exe, cmd, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED | ABOVE_NORMAL_PRIORITY_CLASS,
                        NULL, g_dir, &si, &pi)) {
        log_line("CreateProcess failed: %lu", GetLastError());
        CloseHandle(rd); CloseHandle(wr);
        return 0;
    }
    CloseHandle(wr);
    AssignProcessToJobObject(g_job, pi.hProcess);   /* dies with us */
    t = CreateThread(NULL, 64 * 1024, engine_reader, rd, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (!t) CloseHandle(rd);
    /* publish before it runs, so a concurrent engine_stop() always sees it */
    EnterCriticalSection(&g_lock);
    g_engine = pi;
    g_reader = t;
    LeaveCriticalSection(&g_lock);
    if (g_quitting) { engine_stop(); return 0; }
    ResumeThread(pi.hThread);

    waits[0] = g_engine_active;
    waits[1] = pi.hProcess;
    r = WaitForMultipleObjects(2, waits, FALSE, 10000);
    if (r == WAIT_OBJECT_0) {
        g_running_preset = preset;
        memcpy(g_running_args, args, sizeof(g_running_args));
        EnterCriticalSection(&g_lock);
        g_ui.started_tick = GetTickCount();
        LeaveCriticalSection(&g_lock);
        Sleep(300);  /* let the DNS cache flush and filters settle */
        return 1;
    }
    /* The engine printed its error and exited: let the reader finish the
     * pipe before g_engine_err is read, otherwise the reason is lost. */
    if (r == WAIT_OBJECT_0 + 1 && t)
        WaitForSingleObject(t, 2000);
    log_line("engine did not start (wait=%lu, err=%ld)", r, g_engine_err);
    engine_stop();
    return 0;
}

static const wchar_t *windivert_error_text(long e) {
    switch (e) {
    case 2:    return L"WinDivert64.sys bulunamadı. Zip'teki tüm dosyaları TurkishDPI.exe ile aynı "
                      L"klasöre çıkarın; dosya oradaysa antivirüs karantinasına bakın.";
    case 126:  return L"WinDivert.dll bulunamadı. Zip'teki tüm dosyaları TurkishDPI.exe ile aynı "
                      L"klasöre çıkarın; dosya oradaysa antivirüs karantinasına bakın.";
    case 5:    return L"Yönetici izni gerekiyor. Programı yönetici olarak çalıştırın.";
    case 577:  return L"Sürücü imzası doğrulanamadı. Windows güncellemelerini yükleyin.";
    case 654:  return L"Farklı sürümde bir WinDivert sürücüsü yüklü (eski GoodbyeDPI/zapret?). "
                      L"Onları kapatıp bilgisayarı yeniden başlatın.";
    case 1275: return L"Sürücü bir güvenlik yazılımı tarafından engellendi (ör. Kaspersky).";
    case 1753: return L"Base Filtering Engine (BFE) hizmeti kapalı. Hizmetler'den açın.";
    case 193:  return L"WinDivert.dll bu sistemin mimarisine uymuyor (x64/x86 zip'leri karışmış olabilir). "
                      L"Doğru zip'i yeniden çıkarın.";
    case 1006: return L"WinDivert dosyaları (WinDivert.dll ya da sürücü) beklenen dosyalar değil: "
                      L"değiştirilmiş ya da başka bir sürüm. Güvenlik için yüklenmedi. "
                      L"TurkishDPI'ı resmi zip'ten yeniden çıkarın.";
    default:   return NULL;
    }
}

/* ---------- probing ---------- */
static int control_ok(void) {
    return probe_https_many(g_control, g_ncontrol, 5000, NULL, NULL) > 0;
}

/* Probes all target sites, updates the UI, returns how many opened. */
static int targets_score(int *avg_ms) {
    int ok[PROBE_MAX_HOSTS], ms[PROBE_MAX_HOSTS], score, sum = 0, cnt = 0;
    probe_flush_dns_cache();
    score = probe_https_many(g_targets, g_ntargets, 5000, ok, ms);
    for (int i = 0; i < g_ntargets; i++)
        if (ok[i] && ms[i] > 0) { sum += ms[i]; cnt++; }
    if (avg_ms) *avg_ms = cnt ? sum / cnt : -1;
    set_targets_ui(ok, ms);
    return score;
}

typedef struct { int idx, ms; } dns_job_t;

static DWORD WINAPI dns_job(LPVOID p) {
    dns_job_t *j = (dns_job_t *)p;
    int best = -1;
    /* best of two: one lost packet or a cold cache must not decide */
    for (int k = 0; k < 2; k++) {
        int m = probe_dns_ms(DNS_SERVERS[j->idx].v4, DNS_SERVERS[j->idx].port, "discord.com",
                             1200, NULL, NULL);
        if (m >= 0 && (best < 0 || m < best)) best = m;
    }
    j->ms = best;
    return 0;
}

/* Measures every clean resolver in parallel and keeps the fastest one that
 * answers truthfully. The one used before stays unless another is clearly
 * (>20%) faster, so the choice does not flap between near-equal servers. */
static void pick_dns(void) {
    dns_job_t jobs[DNS_SERVER_COUNT];
    HANDLE th[DNS_SERVER_COUNT];
    int saved = smart_get_dns(g_dns), best = -1;

    for (int i = 0; i < DNS_SERVER_COUNT; i++) {
        jobs[i].idx = i;
        jobs[i].ms = -1;
        th[i] = CreateThread(NULL, 64 * 1024, dns_job, &jobs[i], STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
        if (!th[i]) dns_job(&jobs[i]);
    }
    for (int i = 0; i < DNS_SERVER_COUNT; i++)
        if (th[i]) { WaitForSingleObject(th[i], INFINITE); CloseHandle(th[i]); }
    for (int i = 0; i < DNS_SERVER_COUNT; i++)
        if (jobs[i].ms >= 0 && (best < 0 || jobs[i].ms < jobs[best].ms)) best = i;

    if (best < 0) {
        log_line("no clean DNS answered, keeping %s", DNS_SERVERS[g_dns].v4);
        return;
    }
    if (saved >= 0 && saved < DNS_SERVER_COUNT && jobs[saved].ms >= 0 &&
        jobs[saved].ms * 8 <= jobs[best].ms * 10)
        best = saved;
    if (best != g_dns)
        log_line("DNS -> %s:%u (%d ms)", DNS_SERVERS[best].v4, DNS_SERVERS[best].port, jobs[best].ms);
    g_dns = best;
    smart_set_dns(best);
    EnterCriticalSection(&g_lock);
    g_ui.dns_ms = jobs[best].ms;
    LeaveCriticalSection(&g_lock);
}

/* Learns which addresses this network's resolver answers blocked names with
 * (needs the engine stopped, so the real answers are seen). Kept per network. */
static void load_block_ips(void);

static int learn_block_ips(void) {
    char found[8][16];
    wchar_t sec[80], key[8], val[32];
    int n = probe_learn_block_ips(DNS_SERVERS[g_dns].v4, DNS_SERVERS[g_dns].port, found, 8);
    swprintf(sec, 80, L"net_%ls", smart_network_id());
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            swprintf(key, 8, L"b%d", i);
            MultiByteToWideChar(CP_ACP, 0, found[i], -1, val, 32);
            WritePrivateProfileStringW(sec, key, val, g_ini);
        }
        swprintf(key, 8, L"b%d", n);
        WritePrivateProfileStringW(sec, key, NULL, g_ini);
        log_line("learned %d block-page address(es), first %s", n, found[0]);
    }
    load_block_ips();
    return n;
}

static unsigned long long now_seconds(void) {
    FILETIME ft;
    ULARGE_INTEGER u;
    GetSystemTimeAsFileTime(&ft);
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart / 10000000ull;
}

/* Learning needs the engine stopped (a moment without protection), so it is
 * done at most once a day per network; block pages rarely change. */
#define BLOCK_IP_RELEARN_S (24ull * 3600ull)

static int block_ips_stale(void) {
    wchar_t sec[80], val[32];
    unsigned long long t;
    swprintf(sec, 80, L"net_%ls", smart_network_id());
    GetPrivateProfileStringW(sec, L"bt", L"", val, 32, g_ini);
    if (!val[0]) return 1;
    t = _wcstoui64(val, NULL, 10);
    return now_seconds() < t || now_seconds() - t > BLOCK_IP_RELEARN_S;
}

static void learn_block_ips_if_stale(void) {
    wchar_t sec[80], val[32];
    if (!block_ips_stale()) { load_block_ips(); return; }
    engine_stop();   /* the network's real answers must be seen */
    swprintf(sec, 80, L"net_%ls", smart_network_id());
    /* Nothing found (no DNS blocking here, or a bad moment): try again in an
     * hour instead of a day, so a passing DNS problem is not remembered. */
    _ui64tow(now_seconds() - (learn_block_ips() > 0 ? 0 : BLOCK_IP_RELEARN_S - 3600ull), val, 10);
    WritePrivateProfileStringW(sec, L"bt", val, g_ini);
}

/* Block-page addresses stored for the current network. Every value is
 * validated: it ends up on the elevated engine's command line. */
static void load_block_ips(void) {
    wchar_t sec[80], key[8], val[32];
    swprintf(sec, 80, L"net_%ls", smart_network_id());
    g_n_block_ips = 0;
    for (int i = 0; i < 8; i++) {
        char a[16];
        struct in_addr tmp;
        swprintf(key, 8, L"b%d", i);
        GetPrivateProfileStringW(sec, key, L"", val, 32, g_ini);
        if (!val[0]) break;
        if (WideCharToMultiByte(CP_ACP, 0, val, -1, a, sizeof(a), NULL, NULL) <= 0) continue;
        if (inet_pton(AF_INET, a, &tmp) != 1) continue;
        memcpy(g_block_ips[g_n_block_ips++], a, sizeof(a));
    }
    EnterCriticalSection(&g_lock);
    g_ui.n_block_ips = g_n_block_ips;
    g_ui.hybrid = g_dns_hybrid;
    LeaveCriticalSection(&g_lock);
}

static int engine_fatal(void) {
    const wchar_t *msg = windivert_error_text(g_engine_err);
    wchar_t st[64];
    if (!msg) return 0;
    swprintf(st, 64, L"Sürücü başlatılamadı (%ld)", g_engine_err);
    EnterCriticalSection(&g_lock);
    g_ui.driver_err = g_engine_err;
    LeaveCriticalSection(&g_lock);
    set_state(ST_ERROR, st, msg);
    g_quiet_start = 0;
    if (g_engine_err != g_last_fatal) {   /* no repeated balloons from the watchdog */
        notify(L"TurkishDPI çalışamıyor", msg);
        g_notified_kind = ST_ERROR;   /* the next success is news again */
        g_last_fatal = g_engine_err;
    }
    return 1;
}

/*
 * Balloon policy: a balloon only when the kind of state the user was last
 * told about changes (e.g. error -> active, active -> partial). Restarts after
 * an engine crash, watchdog re-checks, setting changes and rescans that end
 * where they started stay silent; the log and the window still show them.
 * A quiet start (--tray) records the state without showing it.
 */

static void notify_state(state_kind_t kind, const wchar_t *title, const wchar_t *text) {
    if (kind == g_notified_kind) return;
    g_notified_kind = kind;
    notify(title, text);
}

static void announce(int p, int score) {
    wchar_t d[160];
    g_preset = p;
    g_last_fatal = 0;
    g_last_score = score;
    EnterCriticalSection(&g_lock);
    g_ui.driver_err = 0;
    LeaveCriticalSection(&g_lock);
    config_save();
    _snwprintf(d, 159, L"%ls · %ls", PRESETS[p].name, g_reason_text);
    d[159] = 0;
    log_line("decision: preset %d (%d/%d) reason=%s", p, score, g_ntargets, g_reason_code);
    if (score >= g_ntargets) {
        set_state(ST_ACTIVE, L"Koruma aktif", d);
        notify_state(ST_ACTIVE, L"TurkishDPI aktif", PRESETS[p].name);
    } else {
        set_state(ST_PARTIAL, L"Kısmen aktif", d);
        notify_state(ST_PARTIAL, L"TurkishDPI kısmen çalışıyor",
                     L"Bazı test siteleri açılamadı; en iyi sonuç veren profil kullanılıyor.");
    }
    g_quiet_start = 0;
}

/* Autopilot-style checks of the rest of the system (cheap, run per scan). */
static void system_checks(void) {
    wchar_t rival[48] = L"";
    int broken;
    sys_find_rival(rival, 48);
    broken = sys_proxy_broken();
    EnterCriticalSection(&g_lock);
    wcscpy(g_ui.rival, rival);
    g_ui.proxy_broken = broken;
    LeaveCriticalSection(&g_lock);
    if (rival[0] && wcscmp(rival, g_rival_notified) != 0) {
        wchar_t msg[200];
        swprintf(msg, 200, L"%ls çalışıyor. İki araç aynı bağlantıları değiştirirse siteler açılmaz; "
                           L"%ls'ı kapatın.", rival, rival);
        if (notify(L"Çakışan program", msg))
            wcscpy(g_rival_notified, rival);
        log_line("rival tool running");
    }
    if (broken) log_line("system proxy points at a dead local port");
    ui_refresh();
}

void app_repair_proxy(void) {
    if (sys_proxy_repair()) {
        log_line("system proxy switched off (was pointing at nothing)");
        EnterCriticalSection(&g_lock);
        g_ui.proxy_broken = 0;
        LeaveCriticalSection(&g_lock);
        ui_refresh();
    }
}

static HANDLE g_controller;
static DWORD g_controller_id;

/* Stops the controller thread (it checks g_quitting between steps) so that
 * nothing restarts the engine or writes files while we tear down. */
static void stop_controller(DWORD wait_ms) {
    InterlockedExchange(&g_quitting, 1);
    if (g_quit_event) SetEvent(g_quit_event);
    SetEvent(g_cmd_event);
    if (g_controller && GetCurrentThreadId() != g_controller_id)
        WaitForSingleObject(g_controller, wait_ms);
}

void app_uninstall(void) {
    NOTIFYICONDATAW n;
    ZeroMemory(&n, sizeof(n));
    n.cbSize = sizeof(n);
    n.hWnd = ui_hwnd();
    n.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &n);
    stop_controller(30000);
    engine_stop();
    run_hidden(L"schtasks.exe", L"/Delete /F /TN \"TurkishDPI\"");
    sys_uninstall(g_dir);
    sys_uninstall_install_dir(g_exe);
    probe_flush_dns_cache();
    MessageBoxW(NULL, L"TurkishDPI kaldırıldı: başlangıç görevi, Program Files kopyası, ayarlar, günlük ve "
                      L"WinDivert sürücü hizmeti silindi.\n"
                      L"Son olarak TurkishDPI.exe dosyasını silebilirsiniz.", L"TurkishDPI",
                MB_ICONINFORMATION);
    ExitProcess(0);
}

static DWORD WINAPI update_thread(LPVOID u) {
    (void)u;
    /* Waits on an event instead of sleeping: the thread ends at once on exit,
     * and nothing here looks like a long "sleep to outwait a sandbox". */
    if (WaitForSingleObject(g_quit_event, 20000) == WAIT_OBJECT_0) return 0;
    for (;;) {
        wchar_t tag[32];
        if (sys_check_update(tag, 32)) {
            EnterCriticalSection(&g_lock);
            wcscpy(g_ui.update_tag, tag);
            LeaveCriticalSection(&g_lock);
            ui_refresh();
            log_line("update available");
            return 0;
        }
        for (int h = 0; h < 24; h++)   /* once a day, checked hourly */
            if (WaitForSingleObject(g_quit_event, 3600u * 1000u) == WAIT_OBJECT_0) return 0;
    }
}

/* --selftest: verifies the running preset, payment pass-through and logs a verdict. */
static void selftest_finish(void) {
    static const wchar_t *pay[] = { L"www.paypal.com", L"www.garantibbva.com.tr", L"www.iyzico.com" };
    int pok[3], score, ms = -1;
    unsigned long ex0, ex1;
    Sleep(1500);
    score = g_ntargets ? targets_score(&ms) : 0;
    EnterCriticalSection(&g_lock);
    ex0 = g_ui.st_excluded;
    LeaveCriticalSection(&g_lock);
    probe_flush_dns_cache();
    probe_https_many(pay, 3, 8000, pok, NULL);
    Sleep(3500);   /* let the engine report its counters */
    EnterCriticalSection(&g_lock);
    ex1 = g_ui.st_excluded;
    LeaveCriticalSection(&g_lock);
    log_line("SELFTEST state=%d preset=%d targets=%d/%d avg_ms=%d engine_alive=%d",
             (int)g_ui.kind, g_running_preset, score, g_ntargets, ms, engine_alive());
    log_line("SELFTEST payment paypal=%d garanti=%d iyzico=%d excluded_before=%lu excluded_after=%lu",
             pok[0], pok[1], pok[2], ex0, ex1);
    log_line("SELFTEST stats packets=%lu https=%lu http=%lu dns=%lu",
             g_ui.st_packets, g_ui.st_https, g_ui.st_http, g_ui.st_dns);
    /* Latency the engine adds: the same unblocked sites, with and without it
     * (best of three rounds each, connection set-up time only). */
    {
        int on = -1, off = -1, round_ms[PROBE_MAX_HOSTS], okv[PROBE_MAX_HOSTS];
        for (int pass = 0; pass < 2; pass++) {
            int *best = pass ? &off : &on;
            if (pass) { engine_stop(); Sleep(500); }
            for (int k = 0; k < 3; k++) {
                int sum = 0, n = 0;
                probe_https_many(g_control, g_ncontrol, 5000, okv, round_ms);
                for (int i = 0; i < g_ncontrol; i++)
                    if (okv[i]) { sum += round_ms[i]; n++; }
                if (n && (*best < 0 || sum / n < *best)) *best = sum / n;
            }
        }
        log_line("SELFTEST latency (control sites, TCP+TLS): with engine %d ms, without %d ms, "
                 "difference %d ms", on, off, (on >= 0 && off >= 0) ? on - off : 0);
    }
    log_line("SELFTEST %s", (g_ui.kind == ST_ACTIVE || g_ui.kind == ST_STANDBY) &&
                            pok[0] + pok[1] + pok[2] == 3 && ex1 > ex0 ? "PASS" : "CHECK");
    app_exit();
}

static int cmd_pending(void) {
    int n;
    EnterCriticalSection(&g_lock);
    n = g_cmdq_count;
    LeaveCriticalSection(&g_lock);
    return n > 0 || g_quitting;
}

static LONG cmd_pop(LONG *arg) {
    LONG c = CMD_NONE;
    EnterCriticalSection(&g_lock);
    if (g_cmdq_count) {
        c = g_cmdq[g_cmdq_head].cmd;
        *arg = g_cmdq[g_cmdq_head].arg;
        g_cmdq_head = (g_cmdq_head + 1) % CMD_QUEUE;
        g_cmdq_count--;
    }
    LeaveCriticalSection(&g_lock);
    return c;
}

/*
 * Smart scan:
 *  1. no internet -> wait (never blame the presets for a dead link)
 *  2. pick a reachable DNS (remembered per network)
 *  3. try presets best-first using what was learned on this network
 *  4. on a new network, after the first full success also measure up to two
 *     untried presets and keep the fastest (one-time learning cost)
 */
static void auto_scan(void) {
    int order[PRESET_COUNT], best = -1, best_score = -1, best_ms = 0;
    int winner = -1, winner_ms = 0, extra = 0;
    int known_before[PRESET_COUNT], learned_before;

    InterlockedExchange(&g_scan_aborted, 0);
    log_line("scan: reason=%s", g_reason_code);
    set_state(ST_SCANNING, L"Bağlantı kontrol ediliyor…", L"");
    /* The running engine (if any) stays on while the connection and the DNS
     * servers are measured: protection is only interrupted when a different
     * preset actually has to be tried (engine_start keeps an identical one). */
    probe_flush_dns_cache();
    smart_network_changed();
    system_checks();

    if (!control_ok()) {
        int had_engine = g_running_preset >= 0;
        engine_stop();   /* never blame the link for what the engine broke */
        if (!had_engine || !control_ok()) {
            set_state(ST_OFFLINE, L"İnternet bağlantısı yok", L"Bağlantı gelince otomatik başlayacak");
            log_line("control sites unreachable, waiting for network");
            return;
        }
    }
    set_state(ST_SCANNING, L"En hızlı DNS ölçülüyor…", L"");
    /* Without hybrid DNS the engine drops every DNS answer it did not ask for,
     * including the probes to the clean server it uses: measured with the
     * engine running, that server would look dead and the choice would flap
     * (and restart the engine) on every scan. */
    if (!g_dns_hybrid) engine_stop();
    pick_dns();
    if (g_dns_hybrid) learn_block_ips_if_stale();
    smart_order(order, PRESET_COUNT);
    for (int i = 0; i < PRESET_COUNT; i++) known_before[i] = smart_known(i);
    learned_before = smart_learned_count();

    for (int k = 0; k < PRESET_COUNT; k++) {
        int p = order[k], score, ms;
        wchar_t st[64];
        if (cmd_pending()) { InterlockedExchange(&g_scan_aborted, 1); return; }
        if (winner >= 0 && (known_before[p] || extra >= 2)) break;
        EnterCriticalSection(&g_lock);
        g_ui.scan_pos = k + 1;
        g_ui.scan_total = PRESET_COUNT;
        LeaveCriticalSection(&g_lock);
        swprintf(st, 64, winner >= 0 ? L"Optimize ediliyor (%d/%d)" : L"Taranıyor (%d/%d)",
                 k + 1, PRESET_COUNT);
        set_state(ST_SCANNING, st, PRESETS[p].name);
        if (!engine_start(p, g_dns)) {
            if (engine_fatal()) return;
            smart_record(p, 0, 0);
            continue;
        }
        score = targets_score(&ms);
        smart_record(p, score >= g_ntargets, ms);
        log_line("preset %d score %d/%d avg %d ms", p, score, g_ntargets, ms);
        if (winner >= 0) extra++;
        if (score >= g_ntargets) {
            if (winner < 0 || (ms > 0 && ms < winner_ms * 8 / 10)) {
                winner = p;
                winner_ms = ms;
            }
            /* Known network and a proven preset works: no need to explore. */
            if (known_before[p] && learned_before > 1 && extra == 0) break;
            continue;
        }
        if (score > best_score) { best = p; best_score = score; best_ms = ms; }
    }

    if (winner >= 0) {
        if (g_running_preset != winner && !engine_start(winner, g_dns)) {
            if (!engine_fatal()) set_state(ST_ERROR, L"Başlatılamadı", PRESETS[winner].name);
            return;
        }
        targets_score(NULL);
        announce(winner, g_ntargets);
        return;
    }
    (void)best_ms;
    if (best >= 0 && best_score > 0 && engine_start(best, g_dns)) {
        targets_score(NULL);
        announce(best, best_score);
        return;
    }
    engine_stop();
    if (targets_score(NULL) >= g_ntargets) {
        set_state(ST_STANDBY, L"Engel algılanmadı", L"Bekleme modunda – engel çıkarsa devreye girer");
        log_line("targets reachable without engine; standing by");
        return;
    }
    {
        int was = g_ui.kind == ST_PARTIAL || g_ui.kind == ST_ERROR;
        if (engine_start(g_preset, g_dns)) {
            g_last_score = targets_score(NULL);   /* the watchdog compares against this */
            set_state(ST_PARTIAL, L"Çalışan profil bulunamadı", PRESETS[g_preset].name);
        } else if (engine_fatal()) {
            return;                                /* it already explained itself */
        } else {
            g_last_score = 0;
            set_state(ST_ERROR, L"Çalışan profil bulunamadı", L"Günlük dosyasına bakın");
        }
        if (!was) notify_state(g_ui.kind, L"TurkishDPI", L"Hiçbir profil tüm test sitelerini açamadı.");
    }
}

/*
 * record = 1: the result goes into this network's statistics. A failure only
 * counts once it was confirmed (see watchdog_check). Successes count once an
 * hour (every 6th check at the default interval): otherwise the watchdog's
 * own runs would make up nearly all of the history and push the running
 * preset's success rate towards 100% just for being the one that runs.
 */
static int g_health_passes = 0;

static void health_record(int s) {
    if (g_running_preset >= 0 && g_ui.kind != ST_PARTIAL && (!s || ++g_health_passes % 6 == 0))
        smart_record(g_running_preset, s, 0);
}

static int health_ok(int record) {
    int score, need, s;
    if (g_running_preset >= 0 && !engine_alive()) return 0;
    score = targets_score(NULL);
    /* In partial mode some target is out of reach for every preset (often an
     * IP block). Only getting worse than that is a reason to rescan. */
    need = (g_ui.kind == ST_PARTIAL && g_last_score >= 0) ? g_last_score : g_ntargets;
    s = score >= need;
    if (record) health_record(s);
    return s;
}

/*
 * Watchdog check with a second strike: one failed probe round (a target site
 * having a bad minute, a lost packet) is not worth a rescan that interrupts
 * every connection. A failure is re-checked after a short pause; only a
 * repeated failure counts. Returns 1 healthy, 0 failed twice, -1 interrupted
 * by a command or exit (the caller simply goes back to its loop).
 */
#define WATCHDOG_RECHECK_MS 20000

static int watchdog_check(void) {
    HANDLE w[2];
    DWORD r;
    if (health_ok(0)) { health_record(1); return 1; }
    if (g_running_preset >= 0 && !engine_alive()) return 0;   /* no point waiting */
    log_line("watchdog: first check failed, re-checking in %d s", WATCHDOG_RECHECK_MS / 1000);
    w[0] = g_quit_event;
    w[1] = g_cmd_event;
    r = WaitForMultipleObjects(2, w, FALSE, WATCHDOG_RECHECK_MS);
    if (r == WAIT_OBJECT_0 + 1) SetEvent(g_cmd_event);   /* auto-reset: give it back */
    if (r != WAIT_TIMEOUT || g_quitting) return -1;
    if (health_ok(0)) {
        log_line("watchdog: transient failure, second check passed");
        health_record(1);
        return 1;
    }
    health_record(0);   /* only a confirmed failure counts against the preset */
    return 0;
}

static void manual_start(int p) {
    int ms, score;
    InterlockedExchange(&g_scan_aborted, 0);
    if (g_dns_hybrid) load_block_ips();
    set_state(ST_SCANNING, L"Başlatılıyor…", PRESETS[p].name);
    if (!engine_start(p, g_dns)) {
        if (!engine_fatal()) set_state(ST_ERROR, L"Başlatılamadı", PRESETS[p].name);
        return;
    }
    score = targets_score(&ms);
    smart_record(p, score >= g_ntargets, ms);
    announce(p, score);
}

/* Extract "host" from what the user typed (URL, host, host/path). */
static void normalize_host(const wchar_t *in, wchar_t *out, size_t cap) {
    const wchar_t *s = in, *e;
    size_t n;
    while (*s == L' ') s++;
    if (!_wcsnicmp(s, L"https://", 8)) s += 8;
    else if (!_wcsnicmp(s, L"http://", 7)) s += 7;
    e = s;
    while (*e && *e != L'/' && *e != L':' && *e != L' ' && *e != L'?') e++;
    n = (size_t)(e - s);
    if (n >= cap) n = cap - 1;
    wcsncpy(out, s, n);
    out[n] = 0;
    _wcslwr(out);
}

static void site_result(const wchar_t *fmt, const wchar_t *host) {
    wchar_t h[81], buf[160];
    wcsncpy(h, host, 80);
    h[80] = 0;
    _snwprintf(buf, 159, fmt, h);
    buf[159] = 0;
    EnterCriticalSection(&g_lock);
    wcscpy(g_ui.site_result, buf);
    LeaveCriticalSection(&g_lock);
    ui_refresh();
}

/* "Site testi": check one site; if blocked, look for a preset that opens it
 * together with the existing targets, then remember the site. */
static void site_test(const wchar_t *raw) {
    wchar_t host[128];
    const wchar_t *h[1];
    int order[PRESET_COUNT], prev = g_running_preset;

    normalize_host(raw, host, 128);
    if (!host[0] || !wcschr(host, L'.')) { site_result(L"Geçersiz adres: %ls", raw); return; }
    h[0] = host;
    site_result(L"%ls test ediliyor…", host);
    probe_flush_dns_cache();
    if (probe_https_many(h, 1, 6000, NULL, NULL) == 1) {
        site_result(L"✓ %ls açılıyor", host);
        if (g_scan_aborted && g_auto) auto_scan();   /* a scan we interrupted must still finish */
        return;
    }
    if (!control_ok()) { site_result(L"İnternet yok, %ls test edilemedi", host); return; }

    /* Why does it not open? Only DNS and TLS/SNI blocks can be fixed by a
     * different preset; trying all of them for an IP block or a name that
     * does not exist would just cut the connection for nothing. */
    {
        diag_facts_t f;
        diag_t d;
        site_result(L"%ls inceleniyor (DNS, bağlantı, TLS)…", host);
        probe_collect_facts(host, DNS_SERVERS[g_dns].v4, DNS_SERVERS[g_dns].port,
                            (const char (*)[16])g_block_ips, g_n_block_ips, 4000, &f);
        d = diag_classify(&f);
        log_line("site test: diagnosis %s (system dns %d/%d forged, clean dns %d, tcp %d/%d)",
                 diag_code(d), f.sys_bogus, f.sys_n, f.clean_n, f.tcp_ok, f.tcp_tried);
        if (!diag_bypassable(d)) {
            /* short: the result line is one line wide */
            site_result(d == DIAG_OK ? L"✓ %ls açılıyor"
                      : d == DIAG_NO_DNS ? L"✗ %ls: alan adı bulunamadı (yazım hatası?)"
                      : L"✗ %ls: IP engeli ya da sunucu kapalı – DPI ile aşılamaz", host);
            if (g_scan_aborted && g_auto) auto_scan();   /* a scan we interrupted must still finish */
            return;
        }
        switch (d) {
        case DIAG_DNS_POISON:
            site_result(L"%ls: DNS engeli – çözüm aranıyor…", host);
            break;
        default:
            site_result(L"%ls: SNI/TLS engeli – çözüm aranıyor…", host);
            break;
        }
    }
    set_state(ST_SCANNING, L"Site için çözüm aranıyor…", host);
    smart_order(order, PRESET_COUNT);
    for (int k = 0; k < PRESET_COUNT; k++) {
        int p = order[k];
        if (cmd_pending()) break;
        if (p == prev) continue;
        if (!engine_start(p, g_dns)) { if (engine_fatal()) return; continue; }
        probe_flush_dns_cache();
        if (probe_https_many(h, 1, 6000, NULL, NULL) == 1 && targets_score(NULL) >= g_ntargets) {
            InterlockedExchange(&g_scan_aborted, 0);
            smart_record(p, 1, 0);
            site_result(targets_add(host)
                        ? L"✓ %ls açıldı – profil değişti ve site hatırlandı"
                        : L"✓ %ls açıldı – profil değişti (izleme listesi dolu)", host);
            announce(p, g_ntargets);
            return;
        }
    }
    site_result(L"✗ %ls: hiçbir profil açamadı (DPI'ın aşamadığı bir engel)", host);
    if (!g_auto)
        manual_start(g_preset);                /* a manual choice stays the user's */
    else if (!g_scan_aborted && prev >= 0 && engine_start(prev, g_dns))
        announce(prev, targets_score(NULL));   /* the real score, not an assumed one */
    else
        auto_scan();
}

/* ---------- controller thread ---------- */
static DWORD WINAPI controller(LPVOID unused) {
    OVERLAPPED ov;
    HANDLE notify_h = NULL;
    int fails = 0;
    DWORD last_crash = 0;
    (void)unused;

    ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = g_net_event;
    NotifyAddrChange(&notify_h, &ov);

    smart_network_changed();
    if (!g_selftest) { autostart_query(); autostart_migrate(); }
    set_reason(WHY_STARTUP);
    if (g_auto) auto_scan(); else manual_start(g_preset);
    if (g_selftest) selftest_finish();

    for (;;) {
        HANDLE w[4], eng;
        DWORD nw = 0, r, timeout;
        LONG cmd, arg = 0;
        int netchg = 0;

        if (g_quitting) return 0;
        w[nw++] = g_cmd_event;
        w[nw++] = g_net_event;
        w[nw++] = g_resume_event;
        EnterCriticalSection(&g_lock);
        eng = g_engine.hProcess;
        LeaveCriticalSection(&g_lock);
        if (eng) w[nw++] = eng;
        timeout = g_paused ? INFINITE : (DWORD)g_watchdog_min * 60000u;
        if (!g_paused && g_ui.kind == ST_OFFLINE) timeout = 30000;

        r = WaitForMultipleObjects(nw, w, FALSE, timeout);
        if (g_quitting) return 0;

        /* The address-change notification is consumed and re-armed exactly
         * once per completion, also while paused (it would spin otherwise). */
        if (r == WAIT_OBJECT_0 + 1) {
            ResetEvent(g_net_event);
            NotifyAddrChange(&notify_h, &ov);
            netchg = 1;
        }
        if (r == WAIT_OBJECT_0 + 2) netchg = 1;   /* resumed from sleep */

        if ((cmd = cmd_pop(&arg)) != CMD_NONE) {
            if (cmd == CMD_TOGGLE_PAUSE) cmd = g_paused ? CMD_RESUME : CMD_PAUSE;
            if (cmd != CMD_TESTSITE) InterlockedExchange(&g_scan_aborted, 0);
            switch (cmd) {
            case CMD_RESCAN:  g_paused = 0; set_reason(WHY_USER_RESCAN); auto_scan(); break;
            case CMD_AUTO:    g_auto = 1; g_paused = 0; config_save();
                              set_reason(WHY_USER_AUTO); auto_scan(); break;
            case CMD_SELECT:  if (arg >= 0 && arg < PRESET_COUNT) {
                                  g_auto = 0; g_paused = 0; g_preset = (int)arg;
                                  set_reason(WHY_USER_PROFILE);
                                  config_save(); manual_start((int)arg);
                              }
                              break;
            case CMD_PAUSE:   g_paused = 1; engine_stop(); probe_flush_dns_cache();
                              set_state(ST_PAUSED, L"Duraklatıldı", L"Açmak için düğmeye basın"); break;
            case CMD_RESTART: /* settings changed: restart what runs, no full rescan */
                              set_reason(WHY_SETTINGS);
                              if (!g_paused && g_running_preset >= 0) {
                                  int p = g_running_preset;
                                  if (engine_start(p, g_dns)) announce(p, targets_score(NULL));
                                  else if (!engine_fatal()) { if (g_auto) auto_scan(); else manual_start(g_preset); }
                                  break;
                              }
                              /* fall through */
            case CMD_RESUME:  g_paused = 0;
                              if (cmd == CMD_RESUME) set_reason(WHY_RESUME_PAUSE);
                              if (g_auto) auto_scan(); else manual_start(g_preset);
                              break;
            case CMD_AUTOSTART: autostart_set((int)arg); break;
            case CMD_TESTSITE: {
                wchar_t h[256];
                EnterCriticalSection(&g_lock);
                wcscpy(h, g_site_req);
                LeaveCriticalSection(&g_lock);
                set_reason(WHY_SITE_TEST);
                if (!g_paused) site_test(h);
                break;
            }
            default: break;
            }
            if (cmd_pending()) SetEvent(g_cmd_event);   /* more queued: come back at once */
            continue;
        }
        if (g_paused || r == WAIT_OBJECT_0) continue;

        if (netchg) {
            /* Let DHCP/DNS settle, and fold a burst of address changes (VPN,
             * Hyper-V/WSL/Docker adapters, IPv4 + IPv6 arriving one by one)
             * into one check: wait until 4 s pass without another change,
             * 20 s at most. Commands queued meanwhile run right after. */
            DWORD settle_start = GetTickCount();
            int changes = 1, wake = r == WAIT_OBJECT_0 + 2;
            for (;;) {
                HANDLE sw[2] = { g_quit_event, g_net_event };
                DWORD left = 20000 - (GetTickCount() - settle_start), sr;
                if ((int)left <= 0) break;
                sr = WaitForMultipleObjects(2, sw, FALSE, left < 4000 ? left : 4000);
                if (sr == WAIT_OBJECT_0 || g_quitting) return 0;
                if (sr != WAIT_OBJECT_0 + 1) break;
                ResetEvent(g_net_event);
                NotifyAddrChange(&notify_h, &ov);
                changes++;
            }
            log_line("network change detected (%d event(s))%s", changes, wake ? ", after sleep" : "");
            set_reason(wake ? WHY_WAKE : WHY_NETWORK);
            smart_network_changed();
            if (g_dns_hybrid) load_block_ips();   /* this network's, not the last one's */
            if (!control_ok()) {
                engine_stop();
                set_state(ST_OFFLINE, L"İnternet bağlantısı yok", L"Bağlantı gelince otomatik başlayacak");
                continue;
            }
            /* still fine: stay quiet (no balloon for every address change) */
            if (!health_ok(1)) { if (g_auto) auto_scan(); else manual_start(g_preset); }
            continue;
        }
        if (eng && r == WAIT_OBJECT_0 + 3) {
            int p = g_running_preset >= 0 ? g_running_preset : g_preset;
            DWORD now = GetTickCount();
            log_line("engine exited unexpectedly");
            set_reason(WHY_CRASH);
            engine_stop();
            /* crashes count only when they come close together */
            fails = (last_crash && now - last_crash < 10u * 60000u) ? fails + 1 : 1;
            last_crash = now;
            if (fails <= 3) manual_start(p);
            else if (g_auto) { fails = 0; auto_scan(); }
            else set_state(ST_ERROR, L"Motor sürekli kapanıyor",
                           L"Günlüğe bakın veya Otomatik moda geçin");
            continue;
        }
        /* watchdog */
        system_checks();
        if (g_ui.kind == ST_OFFLINE) {
            /* offline: only the cheap control check every 30 s */
            if (control_ok()) {
                set_reason(WHY_ONLINE);
                if (g_auto) auto_scan(); else manual_start(g_preset);
            }
            continue;
        }
        {
            int h = watchdog_check();
            if (h < 0) continue;              /* a command or exit came first */
            if (!h) {
                log_line("watchdog: health check failed twice");
                set_reason(WHY_WATCHDOG);
                if (control_ok()) { if (g_auto) auto_scan(); else manual_start(g_preset); }
                else { engine_stop(); set_state(ST_OFFLINE, L"İnternet bağlantısı yok", L""); }
            }
            continue;
        }
    }
    return 0;
}

void post_cmd(LONG cmd, LONG arg) {
    EnterCriticalSection(&g_lock);
    if (g_cmdq_count < CMD_QUEUE) {
        int tail = (g_cmdq_head + g_cmdq_count) % CMD_QUEUE;
        g_cmdq[tail].cmd = cmd;
        g_cmdq[tail].arg = arg;
        g_cmdq_count++;
    }
    LeaveCriticalSection(&g_lock);
    SetEvent(g_cmd_event);
}

void post_site_test(const wchar_t *host) {
    EnterCriticalSection(&g_lock);
    wcsncpy(g_site_req, host, 255);
    g_site_req[255] = 0;
    LeaveCriticalSection(&g_lock);
    post_cmd(CMD_TESTSITE, 0);
}

void set_payment_protect(int on) {
    g_payment_protect = on;
    config_save();
    if (g_running_preset >= 0) post_cmd(CMD_RESTART, 0);
    else ui_refresh();
}

/* ---------- autostart via Task Scheduler (elevated, no UAC prompt) ---------- */
/* Runs "<System32>\\<exe> <args>" hidden; the program is always taken from
 * System32 (an elevated process must not pick up a planted copy). */
static DWORD run_hidden(const wchar_t *exe, const wchar_t *args) {
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    wchar_t buf[2048], path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    DWORD code = 1;
    if (n == 0 || n + wcslen(exe) + 2 >= MAX_PATH) return 1;
    wcscat(path, L"\\");
    wcscat(path, exe);
    _snwprintf(buf, 2047, L"\"%ls\" %ls", path, args);
    buf[2047] = 0;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (CreateProcessW(path, buf, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 15000);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    return code;
}

/* Known from the controller thread (schtasks can take seconds): the UI only
 * reads the cached value. -1 = not known yet. */
static volatile LONG g_autostart_cache = -1;

int autostart_enabled(void) {
    return g_autostart_cache == 1;
}

static void autostart_query(void) {
    LONG v = run_hidden(L"schtasks.exe", L"/Query /TN \"TurkishDPI\"") == 0;
    InterlockedExchange(&g_autostart_cache, v);
    EnterCriticalSection(&g_lock);
    g_ui.autostart = (int)v;
    LeaveCriticalSection(&g_lock);
    ui_refresh();
}

static void xml_escape(const wchar_t *in, wchar_t *out, size_t cap) {
    size_t o = 0;
    for (; *in && o + 7 < cap; in++) {
        const wchar_t *r = NULL;
        if (*in == L'&') r = L"&amp;";
        else if (*in == L'<') r = L"&lt;";
        else if (*in == L'>') r = L"&gt;";
        else if (*in == L'"') r = L"&quot;";
        if (r) { wcscpy(out + o, r); o += wcslen(r); }
        else out[o++] = *in;
    }
    out[o] = 0;
}

/*
 * Logon task defined in XML: schtasks' command-line form inherits the
 * defaults "only on AC power", "stop on battery" and "stop after 3 days",
 * which silently turn protection off on laptops.
 */
static int autostart_register(int on, int quiet) {
    int ok = 0;
    if (on) {
        wchar_t xml_path[MAX_PATH], exe[MAX_PATH * 2], user[256] = L"", args[MAX_PATH + 64];
        wchar_t xml[4096], installed[MAX_PATH];
        DWORD ulen = 256;
        HANDLE f;
        DWORD w;
        /* The task runs elevated without a UAC prompt, so it must never point
         * at a folder ordinary programs can write to (see sys_install_copy). */
        if (!sys_install_copy(g_self_exe, g_exe, installed, MAX_PATH)) {
            log_line("autostart: could not install into Program Files (%lu)", GetLastError());
            if (!quiet)
                notify(L"Windows ile başlat açılamadı",
                       L"TurkishDPI, Program Files\\TurkishDPI klasörüne kopyalanamadı. "
                       L"Güvenlik nedeniyle başlangıç görevi başka bir klasörden çalıştırılmaz.");
            return 0;
        }
        xml_escape(installed, exe, MAX_PATH * 2);
        GetUserNameW(user, &ulen);
        {
            wchar_t dom[128] = L"", uq[300], esc[600];
            GetEnvironmentVariableW(L"USERDOMAIN", dom, 128);
            _snwprintf(uq, 299, L"%ls%ls%ls", dom, dom[0] ? L"\\" : L"", user);
            uq[299] = 0;
            xml_escape(uq, esc, 600);
            _snwprintf(xml, 4095,
                L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
                L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
                L"  <RegistrationInfo><Description>TurkishDPI</Description></RegistrationInfo>\r\n"
                L"  <Triggers><LogonTrigger><Enabled>true</Enabled><UserId>%ls</UserId>"
                L"<Delay>PT10S</Delay></LogonTrigger></Triggers>\r\n"
                L"  <Principals><Principal id=\"Author\"><UserId>%ls</UserId>"
                L"<LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel>"
                L"</Principal></Principals>\r\n"
                L"  <Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
                L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
                L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
                L"<AllowHardTerminate>true</AllowHardTerminate><StartWhenAvailable>true</StartWhenAvailable>"
                L"<RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>"
                L"<IdleSettings><StopOnIdleEnd>false</StopOnIdleEnd><RestartOnIdle>false</RestartOnIdle></IdleSettings>"
                L"<AllowStartOnDemand>true</AllowStartOnDemand><Enabled>true</Enabled><Hidden>false</Hidden>"
                L"<RunOnlyIfIdle>false</RunOnlyIfIdle><WakeToRun>false</WakeToRun>"
                L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Priority>7</Priority></Settings>\r\n"
                L"  <Actions Context=\"Author\"><Exec><Command>%ls</Command><Arguments>--tray</Arguments>"
                L"</Exec></Actions>\r\n"
                L"</Task>\r\n", esc, esc, exe);
            xml[4095] = 0;
        }
        /* written to the admin-only data folder, removed right after use */
        swprintf(xml_path, MAX_PATH, L"%ls\\autostart.xml", g_dir);
        f = CreateFileW(xml_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            static const WCHAR bom = 0xFEFF;
            WriteFile(f, &bom, sizeof(bom), &w, NULL);
            WriteFile(f, xml, (DWORD)(wcslen(xml) * sizeof(wchar_t)), &w, NULL);
            CloseHandle(f);
            _snwprintf(args, MAX_PATH + 63, L"/Create /F /TN \"TurkishDPI\" /XML \"%ls\"", xml_path);
            args[MAX_PATH + 63] = 0;
            ok = run_hidden(L"schtasks.exe", args) == 0;
            DeleteFileW(xml_path);
        }
    } else {
        ok = run_hidden(L"schtasks.exe", L"/Delete /F /TN \"TurkishDPI\"") == 0;
    }
    if (!ok) {
        log_line("autostart: schtasks failed (on=%d)", on);
        /* a balloon, not a message box: this runs on the controller thread */
        if (!quiet) notify(L"TurkishDPI", L"Başlangıç görevi ayarlanamadı.");
    } else if (on) {
        log_line("autostart: logon task runs the Program Files copy");
    }
    autostart_query();
    return ok;
}

void autostart_set(int on) {
    autostart_register(on, 0);
}

/* Started from somewhere else while the logon task exists (a newer zip, or a
 * task made by v1.1.0 that still points at the extraction folder): refresh
 * the protected copy and point the task at it. The user just approved this
 * exe through UAC, so it is trusted as much as running it is. */
static void autostart_migrate(void) {
    wchar_t dir[MAX_PATH], mine[MAX_PATH];
    size_t n;
    if (g_autostart_cache != 1 || !sys_install_dir(dir, MAX_PATH)) return;
    wcscpy(mine, g_exe);
    n = wcslen(dir);
    if (!_wcsnicmp(mine, dir, n) && mine[n] == L'\\' && !wcschr(mine + n + 1, L'\\')) return;
    log_line("autostart: refreshing the protected copy");
    autostart_register(1, 1);
}

/* ---------- conflicting tools ---------- */
static void check_conflicts(void) {
    static const wchar_t *names[] = { L"GoodbyeDPI", L"zapret", L"winws1", L"winws2" };
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        SC_HANDLE s = OpenServiceW(scm, names[i], SERVICE_QUERY_STATUS | SERVICE_STOP | DELETE);
        SERVICE_STATUS st;
        wchar_t msg[512];
        if (!s) continue;
        if (QueryServiceStatus(s, &st) && st.dwCurrentState != SERVICE_STOPPED) {
            swprintf(msg, 512,
                     L"Bilgisayarınızda \"%ls\" hizmeti çalışıyor. TurkishDPI ile aynı anda çalışırsa "
                     L"bağlantı sorunlarına yol açar.\n\nBu hizmet durdurulup kaldırılsın mı?",
                     names[i]);
            if (MessageBoxW(NULL, msg, L"TurkishDPI – çakışma", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                ControlService(s, SERVICE_CONTROL_STOP, &st);
                for (int t = 0; t < 30 && QueryServiceStatus(s, &st) &&
                                st.dwCurrentState != SERVICE_STOPPED; t++)
                    Sleep(200);
                DeleteService(s);
                log_line("removed conflicting service #%d", (int)i);
            }
        }
        CloseServiceHandle(s);
    }
    CloseServiceHandle(scm);
}

void app_exit(void) {
    NOTIFYICONDATAW n;
    ZeroMemory(&n, sizeof(n));
    n.cbSize = sizeof(n);
    n.hWnd = ui_hwnd();
    n.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &n);
    stop_controller(5000);
    engine_stop();
    probe_flush_dns_cache();
    log_line("exit");
    ExitProcess(0);
}

static int app_main(int quiet) {
    HANDLE mtx, t;
    MSG m;
    WSADATA wsa;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jl;
    wchar_t *pd = NULL;

    mtx = CreateMutexW(NULL, TRUE, L"Global\\TurkishDPI_app");
    if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
        /* Bring the running instance's window to front. */
        HWND w = FindWindowW(L"TurkishDPI_main", NULL);
        if (g_selftest) return 3;   /* another instance is running: cannot self-test */
        if (w && !quiet) PostMessageW(w, WM_TRAY, 0, WM_LBUTTONUP);
        return 0;
    }

    InitializeCriticalSection(&g_lock);
    InitializeCriticalSection(&g_log_lock);
    WSAStartup(MAKEWORD(2, 2), &wsa);
    GetModuleFileNameW(NULL, g_exe, MAX_PATH);
    g_self_exe = CreateFileW(g_exe, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_ProgramData, 0, NULL, &pd))) {
        swprintf(g_dir, MAX_PATH, L"%ls\\TurkishDPI", pd);
        CoTaskMemFree(pd);
    } else {
        wcscpy(g_dir, L"C:\\ProgramData\\TurkishDPI");
    }
    /* Admin-only folder (users may read): we run elevated, so a folder that
     * users can write to would let them plant links or files we act on. */
    if (!sys_secure_dir(g_dir)) {
        MessageBoxW(NULL, L"Veri klasörü güvenli hale getirilemedi:\n%ProgramData%\\TurkishDPI\n\n"
                          L"Klasör bir bağlantı (junction) olabilir veya başka bir kullanıcıya ait olabilir. "
                          L"Klasörü silip TurkishDPI'ı yeniden başlatın.", L"TurkishDPI", MB_ICONERROR);
        return 1;
    }
    swprintf(g_ini, MAX_PATH, L"%ls\\TurkishDPI.ini", g_dir);
    swprintf(g_log, MAX_PATH, L"%ls\\TurkishDPI.log", g_dir);
    log_rotate();
    log_line("TurkishDPI " TURKISHDPI_VERSION_A " starting");

    g_ui.kind = ST_STARTING;
    g_ui.preset = -1;
    wcscpy(g_ui.status, L"Başlatılıyor…");
    config_load();
    config_save();
    g_quiet_start = quiet;

    g_job = CreateJobObjectW(NULL, NULL);
    ZeroMemory(&jl, sizeof(jl));
    jl.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &jl, sizeof(jl));

    g_cmd_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_net_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_resume_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_quit_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_engine_active = CreateEventW(NULL, TRUE, FALSE, NULL);

    if (!g_selftest) check_conflicts();
    if (!ui_init(GetModuleHandleW(NULL))) return 1;
    if (!quiet) ui_show();

    g_controller = CreateThread(NULL, 256 * 1024, controller, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION,
                                &g_controller_id);
    if (!g_selftest && ini_int(L"CheckUpdates", 1)) {
        t = CreateThread(NULL, 64 * 1024, update_thread, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
        if (t) CloseHandle(t);
    }

    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(ui_hwnd(), &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    app_exit();
    return 0;
}

void app_open_url(const wchar_t *url) {
    /* explorer.exe hands the URL to the already running, non-elevated shell,
     * so the browser does not start with administrator rights */
    wchar_t ex[MAX_PATH];
    UINT n = GetWindowsDirectoryW(ex, MAX_PATH);
    if (n == 0 || n + 14 >= MAX_PATH) return;
    wcscat(ex, L"\\explorer.exe");
    ShellExecuteW(NULL, L"open", ex, url, NULL, SW_SHOWNORMAL);
}

void app_open_file(const wchar_t *path) {
    wchar_t np[MAX_PATH], arg[MAX_PATH + 2];
    UINT n = GetSystemDirectoryW(np, MAX_PATH);
    if (n == 0 || n + 14 >= MAX_PATH) return;
    wcscat(np, L"\\notepad.exe");
    _snwprintf(arg, MAX_PATH + 1, L"\"%ls\"", path);
    arg[MAX_PATH + 1] = 0;
    ShellExecuteW(NULL, L"open", np, arg, NULL, SW_SHOWNORMAL);
}

/* Fires network re-check from the UI thread (sleep resume). */
void app_network_event(void) {
    /* separate event: never touch the pending NotifyAddrChange request */
    SetEvent(g_resume_event);
}

/* ---------- entry ---------- */
static void attach_console_if_any(void) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((out == NULL || out == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
    setvbuf(stdout, NULL, _IONBF, 0);
}

int main(int argc, char *argv[]) {
    /* First of all: System32-only DLL search for this elevated process, and
     * WinHTTP / IP Helper loaded from there (see sysdll.h). */
    if (!sysdll_init()) {
        MessageBoxW(NULL, L"Windows sistem kitaplıkları (winhttp.dll, iphlpapi.dll) yüklenemedi.",
                    L"TurkishDPI", MB_ICONERROR);
        return 1;
    }
    if (argc >= 2 && !strcmp(argv[1], "--engine")) {
        extern int gdpi_report_stats;
        gdpi_report_stats = 1;   /* "STATS" lines are for the app only */
        attach_console_if_any();
        argv[1] = argv[0];
        return gdpi_main(argc - 1, argv + 1);
    }
    if (argc >= 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h") || !strcmp(argv[1], "/?"))) {
        MessageBoxW(NULL,
            L"TurkishDPI " TURKISHDPI_VERSION_W L"\n\n"
            L"TurkishDPI.exe                 Uygulama (otomatik mod)\n"
            L"TurkishDPI.exe --tray          Sessiz başlat (Windows açılışında)\n"
            L"TurkishDPI.exe --engine [ARG]  Sadece motoru GoodbyeDPI argümanlarıyla çalıştır\n"
            L"TurkishDPI.exe -9 --dns-addr … GoodbyeDPI ile birebir uyumlu kullanım\n\n"
            L"Ayarlar: %ProgramData%\\TurkishDPI\\TurkishDPI.ini",
            L"TurkishDPI", MB_ICONINFORMATION);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "--ui-preview"))
        return ui_preview_main(argc, argv);   /* UI only: no data folder, engine or network */
    if (argc >= 2 && !strcmp(argv[1], "--selftest")) {
        g_selftest = 1;
        return app_main(1);
    }
    if (argc >= 2 && argv[1][0] == '-' && strcmp(argv[1], "--tray") != 0) {
        attach_console_if_any();
        return gdpi_main(argc, argv);
    }
    return app_main(argc >= 2 && !strcmp(argv[1], "--tray"));
}
