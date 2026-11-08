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
#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "sysdll.h"
#include "app.h"
#include "system.h"
#include "probe.h"
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
    struct in_addr a;
    int ms;
    if (inet_pton(AF_INET, host, &a) != 1) return 1;   /* cannot tell: assume fine */
    ms = probe_tcp_connect_ms(a.s_addr, port, 400);
    return ms >= 0 || ms == -2;                        /* no socket: assume fine */
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

/* ---------- folder security ---------- */
static int owner_is_trusted(PSID owner) {
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID admins = NULL, system = NULL;
    int ok = 0;
    if (!owner) return 0;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                 0, 0, 0, 0, 0, 0, &admins)) {
        ok |= EqualSid(owner, admins);
        FreeSid(admins);
    }
    if (AllocateAndInitializeSid(&nt, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &system)) {
        ok |= EqualSid(owner, system);
        FreeSid(system);
    }
    if (!ok) {
        /* Files we created ourselves: on default Windows settings an elevated
         * administrator's files are owned by the user account, not the group. */
        HANDLE tok;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
            BYTE buf[256];
            DWORD len = 0;
            if (GetTokenInformation(tok, TokenUser, buf, sizeof(buf), &len))
                ok = EqualSid(owner, ((TOKEN_USER *)buf)->User.Sid);
            CloseHandle(tok);
        }
    }
    return ok;
}

static void remove_tree_safe(const wchar_t *dir);

/*
 * Creates/locks a folder for an elevated process: owner Administrators,
 * protected DACL (SYSTEM + Administrators full, Users read/execute).
 * Works on an open handle (FILE_FLAG_OPEN_REPARSE_POINT), so the folder that
 * was checked is the one that gets the new security - a junction swapped in
 * between cannot redirect it. Entries left in it by other users are removed.
 */
int sys_secure_dir(const wchar_t *dir) {
    PSECURITY_DESCRIPTOR sd = NULL;
    PACL dacl = NULL;
    BOOL present = FALSE, def = FALSE;
    PSID admins = NULL;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    BY_HANDLE_FILE_INFORMATION info;
    HANDLE h;
    int ok = 0;

    CreateDirectoryW(dir, NULL);
    h = CreateFileW(dir, READ_CONTROL | WRITE_DAC | WRITE_OWNER | FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    if (!GetFileInformationByHandle(h, &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        CloseHandle(h);
        return 0;
    }
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;BU)", SDDL_REVISION_1, &sd, NULL) &&
        GetSecurityDescriptorDacl(sd, &present, &dacl, &def) &&
        AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                 0, 0, 0, 0, 0, 0, &admins)) {
        ok = SetSecurityInfo(h, SE_FILE_OBJECT,
                             OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
                             PROTECTED_DACL_SECURITY_INFORMATION,
                             admins, NULL, dacl, NULL) == ERROR_SUCCESS;
        FreeSid(admins);
    }
    if (sd) LocalFree(sd);
    CloseHandle(h);
    if (!ok) return 0;

    /* Anything inside that an ordinary user created (and so still controls
     * through ownership) is removed; it is recreated by us when needed. */
    {
        wchar_t pat[MAX_PATH], path[MAX_PATH];
        WIN32_FIND_DATAW fd;
        HANDLE f;
        swprintf(pat, MAX_PATH, L"%ls\\*", dir);
        f = FindFirstFileW(pat, &fd);
        if (f != INVALID_HANDLE_VALUE) {
            do {
                PSID owner = NULL;
                PSECURITY_DESCRIPTOR osd = NULL;
                if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
                swprintf(path, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(path);
                    else DeleteFileW(path);
                    continue;
                }
                if (GetNamedSecurityInfoW(path, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                                          &owner, NULL, NULL, NULL, &osd) == ERROR_SUCCESS) {
                    if (!owner_is_trusted(owner)) {
                        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree_safe(path);
                        else DeleteFileW(path);
                    }
                    LocalFree(osd);
                }
            } while (FindNextFileW(f, &fd));
            FindClose(f);
        }
    }
    return 1;
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
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                /* junction/symlink: remove the link itself, never follow it */
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(path);
                else DeleteFileW(path);
            }
            else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) delete_tree(path);
            else if (!DeleteFileW(path)) MoveFileExW(path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (!RemoveDirectoryW(dir)) MoveFileExW(dir, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
}

static void remove_tree_safe(const wchar_t *dir) {
    delete_tree(dir);
}

/* ---------- protected install folder (autostart runs from there) ---------- */
int sys_install_dir(wchar_t *out, size_t cap) {
    wchar_t *pf = NULL;
    int ok = 0;
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_ProgramFiles, 0, NULL, &pf))) {
        ok = _snwprintf(out, cap, L"%ls\\TurkishDPI", pf) > 0;
        out[cap - 1] = 0;
        CoTaskMemFree(pf);
    }
    return ok;
}

/* 1 if both open files have identical contents (a read error counts as different). */
static int same_contents(HANDLE fa, HANDLE fb) {
    static unsigned char ba[65536], bb[65536];   /* controller thread only */
    LARGE_INTEGER sa, sb, zero;
    DWORD ra, rb;
    zero.QuadPart = 0;
    if (!GetFileSizeEx(fa, &sa) || !GetFileSizeEx(fb, &sb) || sa.QuadPart != sb.QuadPart) return 0;
    if (!SetFilePointerEx(fa, zero, NULL, FILE_BEGIN) || !SetFilePointerEx(fb, zero, NULL, FILE_BEGIN))
        return 0;
    for (;;) {
        if (!ReadFile(fa, ba, sizeof(ba), &ra, NULL) || !ReadFile(fb, bb, sizeof(bb), &rb, NULL))
            return 0;
        if (ra != rb || memcmp(ba, bb, ra) != 0) return 0;
        if (!ra) return 1;
    }
}

/* Writes the whole contents of an open file to dst (via dst.new, swapped in). */
static int copy_from_handle(HANDLE src, const wchar_t *dst) {
    static unsigned char buf[65536];
    wchar_t tmp[MAX_PATH];
    LARGE_INTEGER zero;
    HANDLE out;
    DWORD r, w;
    int ok = 1;
    zero.QuadPart = 0;
    if (!SetFilePointerEx(src, zero, NULL, FILE_BEGIN)) return 0;
    _snwprintf(tmp, MAX_PATH, L"%ls.new", dst);
    tmp[MAX_PATH - 1] = 0;
    out = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == INVALID_HANDLE_VALUE) return 0;
    while (ok) {
        if (!ReadFile(src, buf, sizeof(buf), &r, NULL)) { ok = 0; break; }
        if (!r) break;
        ok = WriteFile(out, buf, r, &w, NULL) && w == r;
    }
    ok = ok && FlushFileBuffers(out);
    CloseHandle(out);
    if (ok && MoveFileExW(tmp, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return 1;
    DeleteFileW(tmp);
    return 0;
}

/* Copies src (an open file) to dst unless dst already holds the same bytes. */
static int install_one(HANDLE src, const wchar_t *dst) {
    HANDLE cur = CreateFileW(dst, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    int same = 0;
    if (cur != INVALID_HANDLE_VALUE) {
        same = same_contents(src, cur);
        CloseHandle(cur);
    }
    return same || copy_from_handle(src, dst);   /* a loaded driver file cannot be replaced */
}

int windivert_file_ok(const wchar_t *path);   /* src/engine/windivert_dyn.c */

/*
 * Copies the program (exe, WinDivert DLL + driver, licences) into the
 * admin-only %ProgramFiles%\TurkishDPI and returns the exe path there.
 *
 * Why: the logon task starts TurkishDPI elevated without a UAC prompt. If it
 * pointed at the folder the zip was extracted to (usually Downloads, which
 * any program of the user can write), replacing TurkishDPI.exe or planting a
 * DLL there would give that program administrator rights at the next logon.
 *
 * The exe is copied from self_exe: a handle to the running image's file
 * opened at start-up without delete sharing, so the file cannot be renamed
 * away and replaced by another "TurkishDPI.exe" before it is copied. Every
 * other file is opened without write/delete sharing and copied from that
 * handle; WinDivert.dll must match the SHA-256 pinned at build time.
 */
int sys_install_copy(HANDLE self_exe, const wchar_t *running_exe, wchar_t *installed_exe, size_t cap) {
    static const struct { const wchar_t *name; int required; } FILES[] = {
        { L"WinDivert.dll", 1 }, { L"WinDivert64.sys", 0 }, { L"WinDivert32.sys", 0 },
        { L"LICENSE", 0 }, { L"NOTICE", 0 }, { L"WinDivert-LICENSE.txt", 0 }, { L"BENIOKU.txt", 0 },
    };
    wchar_t dst_dir[MAX_PATH], src_dir[MAX_PATH], src[MAX_PATH], dst[MAX_PATH];
    wchar_t *slash;
    int drivers = 0;

    if (self_exe == NULL || self_exe == INVALID_HANDLE_VALUE) return 0;
    if (!sys_install_dir(dst_dir, MAX_PATH)) return 0;
    if (!GetFullPathNameW(running_exe, MAX_PATH, src_dir, NULL)) return 0;
    if (!(slash = wcsrchr(src_dir, L'\\'))) return 0;
    *slash = 0;
    if (_snwprintf(installed_exe, cap, L"%ls\\TurkishDPI.exe", dst_dir) < 0) return 0;
    installed_exe[cap - 1] = 0;
    if (!_wcsicmp(src_dir, dst_dir)) return 1;        /* already running from there */
    if (!sys_secure_dir(dst_dir)) return 0;            /* admin-owned, no junctions */

    for (size_t i = 0; i < sizeof(FILES) / sizeof(FILES[0]); i++) {
        HANDLE h;
        int ok;
        _snwprintf(src, MAX_PATH, L"%ls\\%ls", src_dir, FILES[i].name);  src[MAX_PATH - 1] = 0;
        _snwprintf(dst, MAX_PATH, L"%ls\\%ls", dst_dir, FILES[i].name);  dst[MAX_PATH - 1] = 0;
        h = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            if (FILES[i].required) return 0;
            continue;
        }
        {
            BY_HANDLE_FILE_INFORMATION fi;
            if (!GetFileInformationByHandle(h, &fi) ||
                (fi.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
                CloseHandle(h);
                if (FILES[i].required) return 0;
                continue;
            }
        }
        /* never install a foreign DLL or driver (pinned at build time) */
        if ((i == 0 || wcsstr(FILES[i].name, L".sys")) && !windivert_file_ok(src)) {
            CloseHandle(h);
            return 0;
        }
        if (wcsstr(FILES[i].name, L".sys")) drivers++;
        ok = install_one(h, dst);
        CloseHandle(h);
        if (!ok) return 0;
    }
    /* the exe last: a task only ever points at a complete set of files */
    _snwprintf(dst, MAX_PATH, L"%ls\\TurkishDPI.exe", dst_dir);  dst[MAX_PATH - 1] = 0;
    return drivers > 0 && install_one(self_exe, dst);
}

void sys_uninstall_install_dir(const wchar_t *running_exe) {
    wchar_t dir[MAX_PATH];
    DWORD attr;
    (void)running_exe;
    if (!sys_install_dir(dir, MAX_PATH)) return;
    attr = GetFileAttributesW(dir);
    /* the running exe (if it is this copy) goes at the next reboot */
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) &&
        !(attr & FILE_ATTRIBUTE_REPARSE_POINT))
        delete_tree(dir);
}

void sys_uninstall(const wchar_t *data_dir) {
    DWORD attr = GetFileAttributesW(data_dir);
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
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) &&
        !(attr & FILE_ATTRIBUTE_REPARSE_POINT))
        delete_tree(data_dir);
}
