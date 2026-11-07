/*
 * TurkishDPI - checks of the rest of the system (ideas from Shroudly's
 * autopilot): other bypass tools fighting over the same connections, a system
 * proxy left pointing at a program that is gone, update availability, and a
 * clean uninstall.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <tlhelp32.h>
#define INTERNET_OPTION_REFRESH 37
#define INTERNET_OPTION_SETTINGS_CHANGED 39
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "app.h"
#include "system.h"
#include "version.h"

/* ---------- other bypass / VPN tools ---------- */
static const struct { const wchar_t *exe, *name; } RIVALS[] = {
    { L"goodbyedpi.exe",          L"GoodbyeDPI" },
    { L"winws.exe",               L"zapret (winws)" },
    { L"ciadpi.exe",              L"ByeDPI" },
    { L"byedpi.exe",              L"ByeDPI" },
    { L"spoofdpi.exe",            L"SpoofDPI" },
    { L"Shroudly.exe",            L"Shroudly" },
    { L"shroudly-core.exe",       L"Shroudly" },
    { L"shroudly-core-adv.exe",   L"Shroudly" },
    { L"SplitWire.exe",           L"SplitWire" },
    { L"warp-svc.exe",            L"Cloudflare WARP" },
    { L"Cloudflare WARP.exe",     L"Cloudflare WARP" },
    { L"psiphon3.exe",            L"Psiphon" },
    { L"v2rayN.exe",              L"v2rayN" },
    { L"xray.exe",                L"Xray" },
    { L"sing-box.exe",            L"sing-box" },
    { L"Hiddify.exe",             L"Hiddify" },
};

int sys_find_rival(wchar_t *name_out, size_t cap) {
    PROCESSENTRY32W pe;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    int found = 0;
    if (snap == INVALID_HANDLE_VALUE) return 0;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            for (size_t i = 0; i < sizeof(RIVALS) / sizeof(RIVALS[0]) && !found; i++) {
                if (!_wcsicmp(pe.szExeFile, RIVALS[i].exe)) {
                    wcsncpy(name_out, RIVALS[i].name, cap - 1);
                    name_out[cap - 1] = 0;
                    found = 1;
                }
            }
        } while (!found && Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

/* ---------- system proxy pointing at nothing ---------- */
static const wchar_t *INET_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings";

static int port_listening(const char *host, unsigned short port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in sa;
    u_long nb = 1;
    fd_set w;
    struct timeval tv = { 0, 400000 };
    int ok = 0;
    if (s == INVALID_SOCKET) return 1;   /* cannot tell: assume fine */
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, host, &sa.sin_addr);
    ioctlsocket(s, FIONBIO, &nb);
    connect(s, (struct sockaddr *)&sa, sizeof(sa));
    FD_ZERO(&w);
    FD_SET(s, &w);
    if (select(0, NULL, &w, NULL, &tv) == 1) {
        int err = 0, l = sizeof(err);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &l);
        ok = err == 0;
    }
    closesocket(s);
    return ok;
}

/* 1 if the user's manual proxy points at this computer and nothing listens there. */
int sys_proxy_broken(void) {
    HKEY k;
    DWORD enable = 0, sz = sizeof(enable);
    wchar_t server[512];
    DWORD ssz = sizeof(server);
    int broken = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, INET_KEY, 0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
    if (RegQueryValueExW(k, L"ProxyEnable", NULL, NULL, (BYTE *)&enable, &sz) == ERROR_SUCCESS &&
        enable &&
        RegQueryValueExW(k, L"ProxyServer", NULL, NULL, (BYTE *)server, &ssz) == ERROR_SUCCESS) {
        /* forms: "127.0.0.1:1080", "http=127.0.0.1:8080;https=...", "localhost:10808" */
        wchar_t *p = server, *hostp, *colon;
        server[511] = 0;
        if ((hostp = wcsstr(p, L"https=")) != NULL) p = hostp + 6;
        else if ((hostp = wcschr(p, L'=')) != NULL) p = hostp + 1;
        if (!_wcsnicmp(p, L"127.0.0.1:", 10) || !_wcsnicmp(p, L"localhost:", 10)) {
            colon = wcschr(p, L':');
            if (colon) {
                int port = _wtoi(colon + 1);
                if (port > 0 && port < 65536 && !port_listening("127.0.0.1", (unsigned short)port))
                    broken = 1;
            }
        }
    }
    RegCloseKey(k);
    return broken;
}

int sys_proxy_repair(void) {
    HKEY k;
    DWORD zero = 0;
    typedef BOOL (WINAPI *set_opt_fn)(HINTERNET, DWORD, LPVOID, DWORD);
    HMODULE wi;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, INET_KEY, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return 0;
    RegSetValueExW(k, L"ProxyEnable", 0, REG_DWORD, (const BYTE *)&zero, sizeof(zero));
    RegCloseKey(k);
    /* Tell running programs the settings changed (address is kept for the user). */
    wi = LoadLibraryExW(L"wininet.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (wi) {
        set_opt_fn f = (set_opt_fn)(void *)GetProcAddress(wi, "InternetSetOptionW");
        if (f) {
            f(NULL, INTERNET_OPTION_SETTINGS_CHANGED, NULL, 0);
            f(NULL, INTERNET_OPTION_REFRESH, NULL, 0);
        }
    }
    return 1;
}

/* ---------- updates ---------- */
static int version_newer(const char *tag) {
    int a = 0, b = 0, c = 0;
    if (*tag == 'v' || *tag == 'V') tag++;
    if (sscanf(tag, "%d.%d.%d", &a, &b, &c) < 2) return 0;
    if (a != TURKISHDPI_VER_MAJOR) return a > TURKISHDPI_VER_MAJOR;
    if (b != TURKISHDPI_VER_MINOR) return b > TURKISHDPI_VER_MINOR;
    return c > TURKISHDPI_VER_PATCH;
}

int sys_check_update(wchar_t *tag_out, size_t cap) {
    HINTERNET s, c = NULL, r = NULL;
    char buf[8192];
    DWORD total = 0, n;
    int found = 0;
    s = WinHttpOpen(L"TurkishDPI/" TURKISHDPI_VERSION_W, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return 0;
    WinHttpSetTimeouts(s, 8000, 8000, 8000, 8000);
    c = WinHttpConnect(s, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (c) r = WinHttpOpenRequest(c, L"GET", TURKISHDPI_RELEASE_API, NULL, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (r && WinHttpSendRequest(r, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(r, NULL)) {
        while (total < sizeof(buf) - 1 && WinHttpReadData(r, buf + total, sizeof(buf) - 1 - total, &n) && n)
            total += n;
        buf[total] = 0;
        {
            char *t = strstr(buf, "\"tag_name\"");
            if (t && (t = strchr(t + 10, '"')) != NULL) {
                char tag[32];
                int i = 0;
                t++;
                while (*t && *t != '"' && i < 31) tag[i++] = *t++;
                tag[i] = 0;
                if (version_newer(tag)) {
                    MultiByteToWideChar(CP_UTF8, 0, tag, -1, tag_out, (int)cap);
                    found = 1;
                }
            }
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return found;
}

/* ---------- uninstall / clean-up ---------- */
static void delete_tree(const wchar_t *dir) {
    wchar_t pat[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    swprintf(pat, MAX_PATH, L"%ls\\*", dir);
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            swprintf(path, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) delete_tree(path);
            else if (!DeleteFileW(path)) MoveFileExW(path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (!RemoveDirectoryW(dir)) MoveFileExW(dir, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
}

void sys_uninstall(const wchar_t *data_dir) {
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm) {
        static const wchar_t *svcs[] = { L"WinDivert", L"WinDivert14", L"TurkishDPI" };
        for (size_t i = 0; i < sizeof(svcs) / sizeof(svcs[0]); i++) {
            SC_HANDLE s = OpenServiceW(scm, svcs[i], SERVICE_STOP | DELETE);
            SERVICE_STATUS st;
            if (s) {
                ControlService(s, SERVICE_CONTROL_STOP, &st);
                DeleteService(s);
                CloseServiceHandle(s);
            }
        }
        CloseServiceHandle(scm);
    }
    delete_tree(data_dir);
}
