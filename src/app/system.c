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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "syslib.h"
#include "sysdll.h"
#include "app.h"
#include "system.h"
#include "probe.h"
#include "updpkg.h"
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
    wi = load_system_dll(L"wininet.dll");
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
/* This build's version packed like a Windows file version (build 0). */
static unsigned long long own_version(void) {
    return (unsigned long long)TURKISHDPI_VER_MAJOR << 48 | (unsigned long long)TURKISHDPI_VER_MINOR << 32 |
           (unsigned long long)TURKISHDPI_VER_PATCH << 16;
}

/* 1 if tag is a strict "vX.Y.Z" (upd_tag_parse) newer than this build. */
static int version_newer(const char *tag) {
    uint64_t v;
    return upd_tag_parse(tag, &v) && v > own_version();
}

int sys_check_update(wchar_t *tag_out, size_t cap) {
    HINTERNET s, c = NULL, r = NULL;
    char buf[8192];
    DWORD total = 0, n;
    int found = 0;
    s = WinHttpOpen(L"TurkishDPI/" TURKISHDPI_VERSION_W, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return 0;
    sysdll_winhttp_tls(s);
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
                if (upd_tag_ok(tag) && version_newer(tag)) {
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
/*
 * Threat: TurkishDPI runs elevated, so every folder it writes to or deletes
 * from must be one that no ordinary process can influence - including
 * processes of the same user (they run with the same user SID, so "owned by
 * the current user" is NOT trusted). Only BUILTIN\Administrators and SYSTEM
 * count as trusted owners. All checks and deletions work on open handles
 * whose real location is verified (GetFinalPathNameByHandle), so a junction
 * or symlink swapped in at the last moment cannot redirect them.
 */
static PSID new_admins_sid(void) {
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID sid = NULL;
    if (!AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                  0, 0, 0, 0, 0, 0, &sid))
        return NULL;
    return sid;
}

static int owner_is_trusted(PSID owner) {
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID admins = new_admins_sid(), system = NULL;
    int ok = 0;
    if (owner && admins) ok |= EqualSid(owner, admins);
    if (owner && AllocateAndInitializeSid(&nt, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &system)) {
        ok |= EqualSid(owner, system);
        FreeSid(system);
    }
    if (admins) FreeSid(admins);
    return ok;
}

/* Everything this process creates is owned by Administrators, whatever the
 * "default owner for objects created by administrators" policy says. */
int sys_set_default_owner(void) {
    HANDLE tok;
    TOKEN_OWNER o;
    int ok = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_DEFAULT | TOKEN_QUERY, &tok)) return 0;
    o.Owner = new_admins_sid();
    if (o.Owner) {
        ok = SetTokenInformation(tok, TokenOwner, &o, sizeof(o)) != 0;
        FreeSid(o.Owner);
    }
    CloseHandle(tok);
    return ok;
}

/*
 * Bounded wide formatting that fails unless the whole result fits with its
 * terminator. _snwprintf/_vsnwprintf (msvcrt and ucrt) return count without
 * terminating when the output is exactly count characters, and -1 when it is
 * longer; a C99-style implementation returns the full length. "n >= 0 &&
 * n < cap" is right for all three, so a name is never silently cut short.
 */
static int wfmt(wchar_t *out, size_t cap, const wchar_t *fmt, ...) {
    va_list ap;
    int n;
    if (!cap) return 0;
    va_start(ap, fmt);
    n = _vsnwprintf(out, cap, fmt, ap);
    va_end(ap);
    out[cap - 1] = 0;
    if (n < 0 || (size_t)n >= cap) { out[0] = 0; return 0; }
    return 1;
}

/* dir\name into out[cap]; 0 (and an empty out) when it does not fit. */
static int path_join(wchar_t *out, size_t cap, const wchar_t *dir, const wchar_t *name) {
    return wfmt(out, cap, L"%ls\\%ls", dir, name);
}

/* Real path of an open handle ("\\?\" prefix removed). */
static int final_path(HANDLE h, wchar_t *out, DWORD cap) {
    DWORD n = GetFinalPathNameByHandleW(h, out, cap, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (n == 0 || n >= cap) return 0;
    if (!wcsncmp(out, L"\\\\?\\UNC\\", 8)) return 0;          /* never a network path */
    if (!wcsncmp(out, L"\\\\?\\", 4)) memmove(out, out + 4, (wcslen(out + 4) + 1) * sizeof(wchar_t));
    return 1;
}

/* 1 if p is root itself (allow_equal) or something inside it. */
static int path_within(const wchar_t *p, const wchar_t *root, int allow_equal) {
    size_t n = wcslen(root);
    if (_wcsnicmp(p, root, n) != 0) return 0;
    return p[n] == L'\\' || (allow_equal && p[n] == 0);
}

static HANDLE open_entry(const wchar_t *path, DWORD access) {
    return CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
}

/*
 * Deletes exactly the object at path - a link itself, never what it points
 * to - after checking that its real location is root or inside root.
 * Returns 1 when it is gone (or was never there).
 */
static int delete_object(const wchar_t *path, const wchar_t *root, int allow_equal) {
    wchar_t real[MAX_PATH * 2];
    FILE_DISPOSITION_INFO di;
    int ok = 0;
    HANDLE h = open_entry(path, DELETE | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND;
    }
    if (final_path(h, real, MAX_PATH * 2) && path_within(real, root, allow_equal)) {
        FILE_BASIC_INFO bi;
        int cleared = 0;
        if (GetFileInformationByHandleEx(h, FileBasicInfo, &bi, sizeof(bi)) &&
            (bi.FileAttributes & FILE_ATTRIBUTE_READONLY)) {
            bi.FileAttributes &= ~(DWORD)FILE_ATTRIBUTE_READONLY;
            cleared = SetFileInformationByHandle(h, FileBasicInfo, &bi, sizeof(bi)) != 0;
        }
        di.DeleteFile = TRUE;
        ok = SetFileInformationByHandle(h, FileDispositionInfo, &di, sizeof(di)) != 0;
        if (!ok && cleared) {   /* not deleted after all: leave the file as it was */
            bi.FileAttributes |= FILE_ATTRIBUTE_READONLY;
            SetFileInformationByHandle(h, FileBasicInfo, &bi, sizeof(bi));
        }
    }
    CloseHandle(h);
    return ok;
}

/*
 * Deletes dir and everything in it, never leaving root. delay_ok: a file in
 * use (the running exe) is moved aside and deleted at the next reboot - only
 * allowed in admin-only folders, where nobody can swap the path meanwhile.
 * Returns 1 when everything is gone or scheduled.
 */
static int delete_tree_in(const wchar_t *dir, const wchar_t *root, int delay_ok) {
    wchar_t pat[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE f;
    int ok = 1, pending = 0;
    if (!path_join(pat, MAX_PATH, dir, L"*")) return 0;
    f = FindFirstFileW(pat, &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            if (!path_join(path, MAX_PATH, dir, fd.cFileName)) { ok = 0; continue; }
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                ok &= delete_object(path, root, 0);         /* the link, never its target */
            } else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                ok &= delete_tree_in(path, root, delay_ok);
            } else if (!delete_object(path, root, 0)) {
                wchar_t aside[MAX_PATH];
                if (!delay_ok) { ok = 0; continue; }
                /* in use: rename, then delete the renamed file at reboot (a
                 * reinstall under the original name survives the reboot) */
                if (wfmt(aside, MAX_PATH, L"%ls.%lu.del", path, GetTickCount()) &&
                    MoveFileExW(path, aside, 0) && MoveFileExW(aside, NULL, MOVEFILE_DELAY_UNTIL_REBOOT))
                    pending = 1;
                else
                    ok = 0;
            }
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    if (!delete_object(dir, root, 1)) {
        /* still holds a file scheduled for deletion: the folder goes at the
         * same reboot - only if it is empty then */
        if (delay_ok && pending) MoveFileExW(dir, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
        else ok = 0;
    }
    return ok;
}

/* Owner of an entry (opened without following links); 1 trusted, 0 not, -1 unknown. */
static int entry_trusted(const wchar_t *path) {
    PSID owner = NULL;
    PSECURITY_DESCRIPTOR sd = NULL;
    int r = -1;
    HANDLE h = open_entry(path, READ_CONTROL | FILE_READ_ATTRIBUTES);
    if (h == INVALID_HANDLE_VALUE) return -1;
    if (GetSecurityInfo(h, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, NULL, NULL, NULL, &sd)
        == ERROR_SUCCESS) {
        r = owner_is_trusted(owner);
        LocalFree(sd);
    }
    CloseHandle(h);
    return r;
}

static int open_dir_checked(const wchar_t *dir, HANDLE *out, PSID *owner_trusted_flag_dummy) {
    (void)owner_trusted_flag_dummy;
    *out = CreateFileW(dir, READ_CONTROL | WRITE_DAC | WRITE_OWNER | FILE_READ_ATTRIBUTES,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (*out == INVALID_HANDLE_VALUE) return 0;
    {
        BY_HANDLE_FILE_INFORMATION info;
        if (!GetFileInformationByHandle(*out, &info) ||
            !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            CloseHandle(*out);
            *out = INVALID_HANDLE_VALUE;
            return 0;
        }
    }
    return 1;
}

static int handle_owner_trusted(HANDLE h) {
    PSID owner = NULL;
    PSECURITY_DESCRIPTOR sd = NULL;
    int r = 0;
    if (GetSecurityInfo(h, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, NULL, NULL, NULL, &sd)
        == ERROR_SUCCESS) {
        r = owner_is_trusted(owner);
        LocalFree(sd);
    }
    return r;
}

/*
 * Creates/locks a folder for this elevated process: owner Administrators,
 * protected DACL with SYSTEM + Administrators full control, Users
 * read/execute only when users_read (the Program Files copy must be runnable;
 * the data folder holds network history and is admin-only), and an Owner
 * Rights entry so an owner gets no implicit right to change permissions.
 *
 * A folder somebody else created first may still be open in their process
 * with rights they got before it was locked: it is moved aside and a fresh
 * one is created. Entries inside that are not owned by Administrators/SYSTEM
 * are removed (handle-checked); if anything cannot be verified or removed the
 * folder is reported unsafe (0) and the caller must not use it.
 */
int sys_secure_dir(const wchar_t *dir, int users_read) {
    static const wchar_t *SDDL_USERS =
        L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;BU)(A;OICI;0x1200a9;;;OW)";
    static const wchar_t *SDDL_ADMIN =
        L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;OW)";
    PSECURITY_DESCRIPTOR sd = NULL;
    PACL dacl = NULL;
    BOOL present = FALSE, def = FALSE;
    PSID admins = NULL;
    HANDLE h;
    wchar_t root[MAX_PATH * 2];
    int ok = 0;

    CreateDirectoryW(dir, NULL);
    if (!open_dir_checked(dir, &h, NULL)) return 0;
    if (!handle_owner_trusted(h)) {
        wchar_t aside[MAX_PATH];
        CloseHandle(h);
        /* fails while the other process holds it open without delete sharing */
        if (!wfmt(aside, MAX_PATH, L"%ls.untrusted-%lu", dir, GetTickCount()) || !MoveFileExW(dir, aside, 0))
            return 0;
        if (!CreateDirectoryW(dir, NULL)) return 0;
        if (!open_dir_checked(dir, &h, NULL)) return 0;
        if (!handle_owner_trusted(h)) { CloseHandle(h); return 0; }
    }
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(users_read ? SDDL_USERS : SDDL_ADMIN,
                                                             SDDL_REVISION_1, &sd, NULL) &&
        GetSecurityDescriptorDacl(sd, &present, &dacl, &def) && (admins = new_admins_sid()) != NULL) {
        ok = SetSecurityInfo(h, SE_FILE_OBJECT,
                             OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
                             PROTECTED_DACL_SECURITY_INFORMATION,
                             admins, NULL, dacl, NULL) == ERROR_SUCCESS;
    }
    if (admins) FreeSid(admins);
    if (sd) LocalFree(sd);
    if (ok) ok = final_path(h, root, MAX_PATH * 2);
    CloseHandle(h);
    if (!ok) return 0;

    {
        wchar_t pat[MAX_PATH], path[MAX_PATH];
        WIN32_FIND_DATAW fd;
        HANDLE f;
        PACL empty = NULL;
        BYTE acl_buf[64];
        if (InitializeAcl((PACL)acl_buf, sizeof(acl_buf), ACL_REVISION)) empty = (PACL)acl_buf;
        if (!path_join(pat, MAX_PATH, root, L"*")) return 0;
        f = FindFirstFileW(pat, &fd);
        if (f != INVALID_HANDLE_VALUE) {
            do {
                int t;
                if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
                if (!path_join(path, MAX_PATH, root, fd.cFileName)) { ok = 0; continue; }
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                    if (!delete_object(path, root, 0)) ok = 0;
                    continue;
                }
                t = entry_trusted(path);
                if (t == 1) {
                    /* ours: make it inherit the folder's (new) permissions */
                    if (empty)
                        SetNamedSecurityInfoW(path, SE_FILE_OBJECT,
                                              DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
                                              NULL, NULL, empty, NULL);
                    continue;
                }
                /* someone else's (or unreadable): remove, never trust it */
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    wchar_t sub[MAX_PATH * 2];
                    HANDLE sh = open_entry(path, FILE_READ_ATTRIBUTES);
                    int inside = 0;
                    if (sh != INVALID_HANDLE_VALUE) {
                        inside = final_path(sh, sub, MAX_PATH * 2) && path_within(sub, root, 0);
                        CloseHandle(sh);
                    }
                    if (!inside || !delete_tree_in(path, sub, 0)) ok = 0;
                } else if (!delete_object(path, root, 0)) {
                    ok = 0;
                }
            } while (FindNextFileW(f, &fd));
            FindClose(f);
        }
    }
    return ok;
}

/* ---------- protected install folder (autostart runs from there) ---------- */
int sys_install_dir(wchar_t *out, size_t cap) {
    wchar_t *pf = NULL;
    int ok = 0;
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_ProgramFiles, 0, NULL, &pf))) {
        ok = path_join(out, cap, pf, L"TurkishDPI");
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
    if (!wfmt(tmp, MAX_PATH, L"%ls.new", dst)) return 0;
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

/* File version from the VERSIONINFO resource (major<<48 | minor<<32 |
 * patch<<16 | build), 0 when it cannot be read. version.dll from System32. */
typedef DWORD (WINAPI *gfvis_t)(LPCWSTR, LPDWORD);
typedef BOOL (WINAPI *gfvi_t)(LPCWSTR, DWORD, DWORD, LPVOID);
typedef BOOL (WINAPI *vqv_t)(LPCVOID, LPCWSTR, LPVOID *, PUINT);

static unsigned long long file_version(const wchar_t *path) {
    HMODULE v = load_system_dll(L"version.dll");
    unsigned long long ver = 0;
    if (v) {
        gfvis_t size_fn = (gfvis_t)(void *)GetProcAddress(v, "GetFileVersionInfoSizeW");
        gfvi_t info_fn = (gfvi_t)(void *)GetProcAddress(v, "GetFileVersionInfoW");
        vqv_t query_fn = (vqv_t)(void *)GetProcAddress(v, "VerQueryValueW");
        DWORD dummy = 0, n = size_fn ? size_fn(path, &dummy) : 0;
        void *buf = n ? malloc(n) : NULL;
        VS_FIXEDFILEINFO *ffi = NULL;
        UINT len = 0;
        if (buf && info_fn && query_fn && info_fn(path, 0, n, buf) &&
            query_fn(buf, L"\\", (LPVOID *)&ffi, &len) && ffi && len >= sizeof(*ffi) &&
            ffi->dwSignature == 0xFEEF04BDu)
            ver = (unsigned long long)ffi->dwFileVersionMS << 32 | ffi->dwFileVersionLS;
        free(buf);
        FreeLibrary(v);
    }
    return ver;
}

/* src/engine/windivert_dyn.c */
int windivert_file_pinned(const wchar_t *name);
int windivert_handle_ok(HANDLE f, const wchar_t *name);

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
        { L"LICENSE", 0 }, { L"NOTICE", 0 }, { L"THIRD_PARTY_LICENSES.txt", 0 },
        { L"WinDivert-LICENSE.txt", 0 }, { L"BENIOKU.txt", 0 },
    };
    wchar_t dst_dir[MAX_PATH], src_dir[MAX_PATH], src[MAX_PATH], dst[MAX_PATH];
    wchar_t *slash;
    int have_driver = 0;
    /* the driver WinDivert loads on this system must be part of the copy */
#ifdef _WIN64
    const wchar_t *needed_driver = L"WinDivert64.sys";
#else
    BOOL wow = FALSE;
    const wchar_t *needed_driver;
    IsWow64Process(GetCurrentProcess(), &wow);
    needed_driver = wow ? L"WinDivert64.sys" : L"WinDivert32.sys";
#endif

    if (self_exe == NULL || self_exe == INVALID_HANDLE_VALUE) return 0;
    if (!sys_install_dir(dst_dir, MAX_PATH)) return 0;
    if (!GetFullPathNameW(running_exe, MAX_PATH, src_dir, NULL)) return 0;
    if (!(slash = wcsrchr(src_dir, L'\\'))) return 0;
    *slash = 0;
    if (!path_join(installed_exe, cap, dst_dir, L"TurkishDPI.exe")) return 0;
    if (!_wcsicmp(src_dir, dst_dir)) return 1;        /* already running from there */
    if (!sys_secure_dir(dst_dir, 1)) return 0;         /* admin-owned, runnable by users */
    /* That copy may have been updated in place (one-click update while the
     * logon task ran it): an older exe started from elsewhere never
     * replaces it. The task keeps pointing at the newer copy. */
    if (file_version(installed_exe) > own_version()) return 1;

    for (size_t i = 0; i < sizeof(FILES) / sizeof(FILES[0]); i++) {
        HANDLE h;
        int ok, is_driver = wcsstr(FILES[i].name, L".sys") != NULL;
        BY_HANDLE_FILE_INFORMATION fi;
        /* only drivers whose hash is pinned in this build are ever installed */
        if (is_driver && !windivert_file_pinned(FILES[i].name)) continue;
        if (!path_join(src, MAX_PATH, src_dir, FILES[i].name) || !path_join(dst, MAX_PATH, dst_dir, FILES[i].name))
            return 0;
        h = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            if (FILES[i].required || (is_driver && !_wcsicmp(FILES[i].name, needed_driver))) return 0;
            continue;
        }
        /* a plain file with one name: no link, and no hard link that could
         * make us copy some other (protected) file into a readable folder */
        if (!GetFileInformationByHandle(h, &fi) || fi.nNumberOfLinks != 1 ||
            (fi.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
            CloseHandle(h);
            if (FILES[i].required || (is_driver && !_wcsicmp(FILES[i].name, needed_driver))) return 0;
            continue;
        }
        /* never install a foreign DLL or driver: hash checked on this handle */
        if (!windivert_handle_ok(h, FILES[i].name)) {
            CloseHandle(h);
            return 0;
        }
        if (is_driver && !_wcsicmp(FILES[i].name, needed_driver)) have_driver = 1;
        ok = install_one(h, dst);
        CloseHandle(h);
        if (!ok) return 0;
    }
    /* the exe last: a task only ever points at a complete set of files */
    return have_driver && path_join(dst, MAX_PATH, dst_dir, L"TurkishDPI.exe") && install_one(self_exe, dst);
}

/* Deletes one of our own (admin-only) folders: only an admin-owned folder,
 * never anything outside it. */
static void sys_delete_dir(const wchar_t *dir) {
    HANDLE h;
    wchar_t root[MAX_PATH * 2];
    int ours;
    if (!open_dir_checked(dir, &h, NULL)) return;
    ours = handle_owner_trusted(h) && final_path(h, root, MAX_PATH * 2);
    CloseHandle(h);
    if (ours) delete_tree_in(root, root, 1);
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
        sys_delete_dir(dir);
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
        sys_delete_dir(data_dir);
}

/* ---------- one-click update ---------- */
/*
 * Installs a newer release over the running copy, in the folder it runs
 * from (the extracted zip, or the Program Files copy the logon task uses).
 *
 * What is trusted: the release files come over HTTPS from github.com (and
 * the *.githubusercontent.com host GitHub redirects downloads to); the zip
 * must match its line in the same release's SHA256SUMS.txt. The files are not
 * code-signed, so this guards against a broken or altered download on the
 * way, not against someone who controls the GitHub repository itself.
 *
 * The folder may be writable by ordinary programs of the user (Downloads),
 * while this process is elevated. So every file is written under a new name
 * created without following links, checked to be inside the folder, moved
 * into place by a handle-relative rename, and the new exe is re-opened
 * denying writes and deletes and compared byte by byte before it is started.
 * WinDivert files are never replaced: if the release ships different ones,
 * the update is refused and the release page is opened instead.
 */
int sha256_mem(const void *data, size_t len, unsigned char out[32]);   /* windivert_dyn.c */

#ifdef _WIN64
#define UPD_ARCH "x64"
#define UPD_MACHINE 0x8664u
#else
#define UPD_ARCH "x86"
#define UPD_MACHINE 0x14cu
#endif

/* https on github.com or *.githubusercontent.com (release downloads redirect there). */
static int url_allowed(const wchar_t *url) {
    static const wchar_t sfx[] = L".githubusercontent.com";
    const size_t sl = sizeof(sfx) / sizeof(sfx[0]) - 1;
    const wchar_t *h, *end;
    size_t n;
    if (_wcsnicmp(url, L"https://", 8)) return 0;
    h = url + 8;
    end = h + wcscspn(h, L"/:?#@\\");
    if (*end != L'/') return 0;               /* no port, user info or odd separators */
    n = (size_t)(end - h);
    if (n == 10 && !_wcsnicmp(h, L"github.com", 10)) return 1;
    return n > sl && !_wcsnicmp(end - sl, sfx, sl);
}

/* GET https://github.com<path> (redirects followed, final host checked);
 * at most cap bytes into a malloc'ed buffer.
 *
 * total_ms bounds the whole download: it runs on the controller thread, and
 * the per-operation WinHTTP timeouts alone let a throttled connection that
 * sends a little every few seconds block the controller for hours. Each read
 * waits at most the time that is left (and at most 30 s), and the deadline
 * is checked after every step, so the overrun is at most one connect/send
 * timeout. Returns HG_OK, HG_FAIL, HG_TIMEOUT or HG_TOO_BIG. */
enum { HG_OK = 1, HG_FAIL = 0, HG_TIMEOUT = -1, HG_TOO_BIG = -2 };

static int http_get(const char *path_a, size_t cap, DWORD total_ms, unsigned char **out, size_t *len) {
    wchar_t path[512], url[4096];
    DWORD status = 0, sz = sizeof(status), usz = sizeof(url);
    HINTERNET s, c = NULL, r = NULL;
    unsigned char *buf = NULL;
    size_t n = 0, have = 0;
    const size_t limit = cap + 1;   /* room for the read that returns 0 after exactly cap bytes */
    const ULONGLONG deadline = GetTickCount64() + total_ms;
    int rc = HG_FAIL;
    *out = NULL;
    *len = 0;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path_a, -1, path, 512)) return HG_FAIL;
    s = WinHttpOpen(L"TurkishDPI/" TURKISHDPI_VERSION_W, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return HG_FAIL;
    sysdll_winhttp_tls(s);
    {
        int rt = total_ms < 30000 ? (int)total_ms : 30000;
        WinHttpSetTimeouts(s, 10000, 15000, rt, rt);
    }
    c = WinHttpConnect(s, L"github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (c) r = WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(r, NULL) &&
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX) &&
        status == 200 && WinHttpQueryOption(r, WINHTTP_OPTION_URL, url, &usz) && url_allowed(url)) {
        rc = HG_OK;
        for (;;) {
            DWORD got = 0, left;
            ULONGLONG now = GetTickCount64();
            if (now >= deadline) { rc = HG_TIMEOUT; break; }
            left = (DWORD)(deadline - now);
            if (left > 30000) left = 30000;
            WinHttpSetOption(r, WINHTTP_OPTION_RECEIVE_TIMEOUT, &left, sizeof(left));
            if (n == have) {
                size_t nc = have ? have * 2 : 256 * 1024;
                unsigned char *nb;
                if (have >= limit) { rc = HG_TOO_BIG; break; }   /* larger than any real package */
                if (nc > limit) nc = limit;
                nb = (unsigned char *)realloc(buf, nc);
                if (!nb) { rc = HG_FAIL; break; }
                buf = nb;
                have = nc;
            }
            if (!WinHttpReadData(r, buf + n, (DWORD)(have - n), &got)) {
                rc = GetTickCount64() >= deadline ? HG_TIMEOUT : HG_FAIL;
                break;
            }
            if (!got) break;
            n += got;
        }
        if (rc == HG_OK && n > cap) rc = HG_TOO_BIG;
    } else if (GetTickCount64() >= deadline) {
        rc = HG_TIMEOUT;
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    if (rc != HG_OK) { free(buf); return rc; }
    *out = buf;
    *len = n;
    return HG_OK;
}

/* The folder the exe runs from, opened without following a link; its real
 * path in real. Files are renamed relative to this handle. */
static HANDLE open_update_dir(const wchar_t *running_exe, wchar_t *real, DWORD cap) {
    wchar_t dir[MAX_PATH], *slash;
    BY_HANDLE_FILE_INFORMATION fi;
    HANDLE h;
    if (!GetFullPathNameW(running_exe, MAX_PATH, dir, NULL) || !(slash = wcsrchr(dir, L'\\')))
        return INVALID_HANDLE_VALUE;
    *slash = 0;
    h = CreateFileW(dir, FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h == INVALID_HANDLE_VALUE) return h;
    if (!GetFileInformationByHandle(h, &fi) || !(fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (fi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !final_path(h, real, cap)) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

/*
 * Renames the open file f to dir\name: name only, resolved relative to the
 * dir handle, so the target is the folder we opened whatever its path is
 * now. NtSetInformationFile, because SetFileInformationByHandle treats the
 * name as a full Win32 path and rejects a bare name with ERROR_INVALID_NAME
 * (123) even when RootDirectory is set. ntdll is in every process already.
 */
typedef struct { union { LONG Status; PVOID Pointer; } u; ULONG_PTR Information; } upd_iosb_t;
typedef LONG (NTAPI *nt_set_info_t)(HANDLE, upd_iosb_t *, PVOID, ULONG, int);
#define UPD_FILE_RENAME_INFORMATION 10

static int rename_in(HANDLE f, HANDLE dirh, const wchar_t *name, int replace) {
    union { FILE_RENAME_INFO ri; BYTE b[sizeof(FILE_RENAME_INFO) + MAX_PATH * sizeof(wchar_t)]; } u;
    static nt_set_info_t set_info;
    upd_iosb_t io;
    size_t n = wcslen(name);
    if (n == 0 || n >= MAX_PATH || wcschr(name, L'\\') || wcschr(name, L'/') || wcschr(name, L':')) return 0;
    if (!set_info) {
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        set_info = nt ? (nt_set_info_t)(void *)GetProcAddress(nt, "NtSetInformationFile") : NULL;
        if (!set_info) return 0;
    }
    ZeroMemory(&u, sizeof(u));
    u.ri.ReplaceIfExists = replace ? TRUE : FALSE;
    u.ri.RootDirectory = dirh;
    u.ri.FileNameLength = (DWORD)(n * sizeof(wchar_t));
    memcpy(u.ri.FileName, name, n * sizeof(wchar_t));
    return set_info(f, &io, &u.ri, (ULONG)(sizeof(FILE_RENAME_INFO) + n * sizeof(wchar_t)),
                    UPD_FILE_RENAME_INFORMATION) >= 0;
}

/* 1 if the open file holds exactly data[0..len) (read from the start). */
static int handle_equals(HANDLE h, const unsigned char *data, size_t len) {
    static unsigned char buf[65536];   /* controller thread only */
    LARGE_INTEGER size, zero;
    size_t pos = 0;
    zero.QuadPart = 0;
    if (!GetFileSizeEx(h, &size) || (unsigned long long)size.QuadPart != len ||
        !SetFilePointerEx(h, zero, NULL, FILE_BEGIN))
        return 0;
    while (pos < len) {
        DWORD want = (DWORD)(len - pos > sizeof(buf) ? sizeof(buf) : len - pos), got = 0;
        if (!ReadFile(h, buf, want, &got, NULL) || got != want || memcmp(buf, data + pos, got)) return 0;
        pos += got;
    }
    return 1;
}

/* 1 if dir\name is a plain file (no link) with exactly these bytes. */
static int file_equals(const wchar_t *real, const wchar_t *name, const unsigned char *data, size_t len) {
    wchar_t path[MAX_PATH];
    BY_HANDLE_FILE_INFORMATION fi;
    HANDLE h;
    int same;
    if (!path_join(path, MAX_PATH, real, name)) return 0;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    same = GetFileInformationByHandle(h, &fi) && !(fi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
           handle_equals(h, data, len);
    CloseHandle(h);
    return same;
}

static void discard(HANDLE h) {
    FILE_DISPOSITION_INFO di;
    di.DeleteFile = TRUE;
    SetFileInformationByHandle(h, FileDispositionInfo, &di, sizeof(di));
    CloseHandle(h);
}

/* Writes data to a new file dir\name (a leftover of that name, or a link,
 * is removed first) and returns its handle (write + delete access, no
 * sharing), or INVALID_HANDLE_VALUE. */
static HANDLE write_new(const wchar_t *real, const wchar_t *name, const unsigned char *data, size_t len) {
    wchar_t path[MAX_PATH], fp[MAX_PATH * 2];
    size_t pos = 0;
    HANDLE h;
    if (!path_join(path, MAX_PATH, real, name)) return INVALID_HANDLE_VALUE;
    if (!delete_object(path, real, 0)) return INVALID_HANDLE_VALUE;
    h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE | DELETE, 0, NULL, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h == INVALID_HANDLE_VALUE) return h;
    if (!final_path(h, fp, MAX_PATH * 2) || !path_within(fp, real, 0)) { discard(h); return INVALID_HANDLE_VALUE; }
    while (pos < len) {
        DWORD chunk = (DWORD)(len - pos > (1u << 20) ? (1u << 20) : len - pos), w = 0;
        if (!WriteFile(h, data + pos, chunk, &w, NULL) || w != chunk) { discard(h); return INVALID_HANDLE_VALUE; }
        pos += w;
    }
    if (!FlushFileBuffers(h)) { discard(h); return INVALID_HANDLE_VALUE; }
    return h;
}

/* Replaces dir\name with data unless it already holds those bytes. */
static int replace_file(HANDLE dirh, const wchar_t *real, const wchar_t *name,
                        const unsigned char *data, size_t len) {
    wchar_t tmp[MAX_PATH];
    HANDLE h;
    if (file_equals(real, name, data, len)) return 1;
    if (!wfmt(tmp, MAX_PATH, L"%ls.upd-new", name)) return 0;
    if ((h = write_new(real, tmp, data, len)) == INVALID_HANDLE_VALUE) return 0;
    if (!rename_in(h, dirh, name, 1)) { discard(h); return 0; }
    CloseHandle(h);
    return 1;
}

typedef struct { const char *name; int kind; } upd_item_t;   /* kind: 0 exe, 1 WinDivert, 2 text */
static const upd_item_t UPD_ITEMS[] = {
    { "TurkishDPI.exe", 0 }, { "WinDivert.dll", 1 }, { "WinDivert64.sys", 1 }, { "WinDivert32.sys", 1 },
    { "LICENSE", 2 }, { "NOTICE", 2 }, { "THIRD_PARTY_LICENSES.txt", 2 }, { "WinDivert-LICENSE.txt", 2 },
    { "BENIOKU.txt", 2 },
};
#define UPD_NITEMS (sizeof(UPD_ITEMS) / sizeof(UPD_ITEMS[0]))

static int same_file(const BY_HANDLE_FILE_INFORMATION *a, const BY_HANDLE_FILE_INFORMATION *b) {
    return a->dwVolumeSerialNumber == b->dwVolumeSerialNumber && a->nFileIndexHigh == b->nFileIndexHigh &&
           a->nFileIndexLow == b->nFileIndexLow;
}

/* Re-opens the running exe like lock_self_exe (read, no write/delete
 * sharing) and checks it is the same file as before. */
static HANDLE relock_self(const wchar_t *running_exe, const BY_HANDLE_FILE_INFORMATION *was) {
    BY_HANDLE_FILE_INFORMATION fi;
    HANDLE h = CreateFileW(running_exe, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return h;
    if (!GetFileInformationByHandle(h, &fi) || !same_file(&fi, was)) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

int sys_update_install(const wchar_t *tag_w, void **self_exe_p, const wchar_t *running_exe,
                       void **lock_out, const char **why) {
    HANDLE *self_exe = (HANDLE *)self_exe_p;
    char tag[32], path[256], asset[96];
    unsigned char *sums = NULL, *zip = NULL, *data[UPD_NITEMS];
    size_t sums_len = 0, zip_len = 0, dlen[UPD_NITEMS];
    unsigned char want[32], got[32];
    wchar_t real[MAX_PATH * 2], base[MAX_PATH], tmp[MAX_PATH], old[MAX_PATH], cur[MAX_PATH];
    wchar_t wname[64], fp[MAX_PATH * 2];
    const wchar_t *slash;
    BY_HANDLE_FILE_INFORMATION self_fi, fi;
    HANDLE dirh = INVALID_HANDLE_VALUE, hn = INVALID_HANDLE_VALUE, ho = INVALID_HANDLE_VALUE, lock;
    int rc = 0, r;

    *lock_out = INVALID_HANDLE_VALUE;
    *why = "";
    memset(data, 0, sizeof(data));
    memset(dlen, 0, sizeof(dlen));
    if (!WideCharToMultiByte(CP_UTF8, 0, tag_w, -1, tag, sizeof(tag), NULL, NULL) || !upd_tag_ok(tag)) {
        *why = "bad tag";
        return 0;
    }
    snprintf(asset, sizeof(asset), "TurkishDPI-%s-" UPD_ARCH ".zip", tag);

    /* 1. checksums, then the package, checked against them */
    snprintf(path, sizeof(path), TURKISHDPI_REPO_PATH_A "/releases/download/%s/SHA256SUMS.txt", tag);
    if ((r = http_get(path, 64 * 1024, 30 * 1000, &sums, &sums_len)) != HG_OK) {
        *why = r == HG_TIMEOUT ? "download SHA256SUMS.txt: took longer than 30 s"
             : r == HG_TOO_BIG ? "download SHA256SUMS.txt: too large" : "download SHA256SUMS.txt";
        goto out;
    }
    if (!upd_sums_find((const char *)sums, sums_len, asset, want)) { *why = "zip not in SHA256SUMS.txt"; goto out; }
    snprintf(path, sizeof(path), TURKISHDPI_REPO_PATH_A "/releases/download/%s/%s", tag, asset);
    if ((r = http_get(path, 64u * 1024 * 1024, 3 * 60 * 1000, &zip, &zip_len)) != HG_OK) {
        *why = r == HG_TIMEOUT ? "download zip: took longer than 3 minutes"
             : r == HG_TOO_BIG ? "download zip: too large" : "download zip";
        goto out;
    }
    if (!sha256_mem(zip, zip_len, got) || memcmp(want, got, 32) != 0) { *why = "zip SHA-256 mismatch"; goto out; }

    /* 2. the files we take from it */
    for (size_t i = 0; i < UPD_NITEMS; i++) {
        int r = upd_zip_extract(zip, zip_len, UPD_ITEMS[i].name, &data[i], &dlen[i]);
        if (r < 0) { *why = "broken zip"; goto out; }
        if (r == 0 && UPD_ITEMS[i].kind == 0) { *why = "no exe in zip"; goto out; }
    }
    if (upd_pe_machine(data[0], dlen[0]) != UPD_MACHINE) { *why = "exe for another CPU"; goto out; }
    /* the exe itself must be the tagged version and newer than this one: a
     * release built from an un-bumped version.h would otherwise update every
     * user in a loop, and an older exe under a newer tag would be a downgrade */
    {
        uint64_t tag_ver = 0, exe_ver = upd_pe_file_version(data[0], dlen[0]);
        if (!upd_tag_parse(tag, &tag_ver) || (exe_ver & ~0xFFFFull) != tag_ver) {
            *why = "exe version differs from the tag";
            goto out;
        }
        if (tag_ver <= own_version()) { *why = "release is not newer than this version"; goto out; }
    }

    /* 3. where we run from: the folder, and the exe's real (long) name in it
     * taken from its locked handle - the start path may use 8.3 names */
    if ((dirh = open_update_dir(running_exe, real, MAX_PATH * 2)) == INVALID_HANDLE_VALUE) {
        *why = "open folder";
        goto out;
    }
    if (!final_path(*self_exe, fp, MAX_PATH * 2) || !path_within(fp, real, 0) ||
        !(slash = wcsrchr(fp, L'\\')) || slash != fp + wcslen(real) || wcslen(slash + 1) + 24 >= MAX_PATH) {
        *why = "exe path";
        goto out;
    }
    wcscpy(base, slash + 1);
    if (!path_join(cur, MAX_PATH, real, base)) { *why = "exe path"; goto out; }

    /* 4. WinDivert must stay as it is: the running build pins these files */
    for (size_t i = 0; i < UPD_NITEMS; i++) {
        wchar_t p[MAX_PATH];
        DWORD attr;
        if (UPD_ITEMS[i].kind != 1) continue;
        MultiByteToWideChar(CP_UTF8, 0, UPD_ITEMS[i].name, -1, wname, 64);
        if (!path_join(p, MAX_PATH, real, wname)) { *why = "exe path"; goto out; }
        attr = GetFileAttributesW(p);
        if (attr == INVALID_FILE_ATTRIBUTES &&
            (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND))
            continue;   /* not used here: not installed either */
        /* present (or not readable): the zip must hold exactly these bytes */
        if (!data[i] || !file_equals(real, wname, data[i], dlen[i])) {
            *why = "new WinDivert files (manual update needed)";
            rc = -1;
            goto out;
        }
    }

    /* 5. texts (licences, read-me): best effort */
    for (size_t i = 0; i < UPD_NITEMS; i++) {
        if (UPD_ITEMS[i].kind != 2 || !data[i]) continue;
        MultiByteToWideChar(CP_UTF8, 0, UPD_ITEMS[i].name, -1, wname, 64);
        replace_file(dirh, real, wname, data[i], dlen[i]);
    }

    /* 6. the exe: new file ready, running one moved aside, new one in place */
    if (!wfmt(tmp, MAX_PATH, L"%ls.upd-new", base) || !wfmt(old, MAX_PATH, L"%ls.%lu.old", base, GetTickCount())) {
        *why = "exe path";
        goto out;
    }
    if ((hn = write_new(real, tmp, data[0], dlen[0])) == INVALID_HANDLE_VALUE) { *why = "write exe"; goto out; }
    if (!GetFileInformationByHandle(*self_exe, &self_fi)) { *why = "self info"; goto out; }
    CloseHandle(*self_exe);       /* it denies delete sharing, which the rename needs */
    *self_exe = INVALID_HANDLE_VALUE;
    ho = CreateFileW(cur, DELETE | SYNCHRONIZE | FILE_READ_ATTRIBUTES,
                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                     FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (ho == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(ho, &fi) || !same_file(&fi, &self_fi)) {
        *why = "running exe not found in its folder";
        goto relock;
    }
    if (!rename_in(ho, dirh, old, 0)) { *why = "move running exe aside"; goto relock; }
    if (!rename_in(hn, dirh, base, 0)) {
        rename_in(ho, dirh, base, 0);   /* put the running one back */
        *why = "move new exe in place";
        goto relock;
    }
    CloseHandle(hn);
    hn = INVALID_HANDLE_VALUE;
    CloseHandle(ho);
    ho = INVALID_HANDLE_VALUE;

    /* 7. the file that will be started: locked against changes, verified */
    lock = CreateFileW(cur, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (lock == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(lock, &fi) || fi.nNumberOfLinks != 1 ||
        (fi.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        !final_path(lock, fp, MAX_PATH * 2) || _wcsicmp(fp, cur) != 0 || !handle_equals(lock, data[0], dlen[0])) {
        if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
        *why = "new exe changed after writing";
        rc = -2;    /* the running exe is already moved aside */
        goto out;
    }
    *lock_out = lock;
    rc = 1;
    goto out;

relock:
    /* our own DELETE handle on the running exe would make the re-lock
     * (which denies delete sharing) fail */
    if (ho != INVALID_HANDLE_VALUE) { CloseHandle(ho); ho = INVALID_HANDLE_VALUE; }
    *self_exe = relock_self(running_exe, &self_fi);
    if (*self_exe == INVALID_HANDLE_VALUE) rc = -2;
out:
    if (hn != INVALID_HANDLE_VALUE) discard(hn);
    if (ho != INVALID_HANDLE_VALUE) CloseHandle(ho);
    if (dirh != INVALID_HANDLE_VALUE) CloseHandle(dirh);
    for (size_t i = 0; i < UPD_NITEMS; i++) free(data[i]);
    free(zip);
    free(sums);
    return rc;
}

/* 1 if w[0..n) is one of the UPD_ITEMS names (ASCII, case-insensitive). */
static int upd_item_name(const wchar_t *w, size_t n) {
    for (size_t i = 0; i < UPD_NITEMS; i++) {
        const char *a = UPD_ITEMS[i].name;
        size_t k = 0;
        for (; k < n && a[k]; k++) {
            wchar_t x = w[k], y = (wchar_t)(unsigned char)a[k];
            if (x >= L'A' && x <= L'Z') x = (wchar_t)(x - L'A' + L'a');
            if (y >= L'A' && y <= L'Z') y = (wchar_t)(y - L'A' + L'a');
            if (x != y) break;
        }
        if (k == n && !a[k]) return 1;
    }
    return 0;
}

/*
 * 1 if name is something an update of the exe called base leaves behind:
 * "<base>.<digits>.old" (the previous exe, or whatever squatted the exe's
 * name during a failed update) or "<n>.upd-new" where n is base or one of
 * the package's file names. Nothing else: the folder may be Downloads.
 */
static int upd_leftover(const wchar_t *name, const wchar_t *base) {
    size_t n = wcslen(name), bl = wcslen(base), d;
    if (n > 8 && !_wcsicmp(name + n - 8, L".upd-new"))
        return (n - 8 == bl && !_wcsnicmp(name, base, bl)) || upd_item_name(name, n - 8);
    if (n < bl + 1 + 1 + 4 || _wcsnicmp(name, base, bl) || name[bl] != L'.' || _wcsicmp(name + n - 4, L".old"))
        return 0;
    for (d = bl + 1; d < n - 4; d++)
        if (name[d] < L'0' || name[d] > L'9') return 0;
    return d - (bl + 1) <= 10;   /* a DWORD tick count */
}

/* Removes what an update left in the exe's folder: the previous exe (moved
 * aside while it ran) and unfinished writes. Only our own exact names
 * (upd_leftover), each deleted through a handle that does not follow links. */
void sys_update_cleanup(const wchar_t *running_exe) {
    wchar_t real[MAX_PATH * 2], pat[MAX_PATH], path[MAX_PATH], longp[MAX_PATH];
    const wchar_t *slash;
    WIN32_FIND_DATAW fd;
    HANDLE dirh, f;
    /* the long file name: the start path may use 8.3 names */
    if (!GetLongPathNameW(running_exe, longp, MAX_PATH) || !(slash = wcsrchr(longp, L'\\'))) return;
    if ((dirh = open_update_dir(running_exe, real, MAX_PATH * 2)) == INVALID_HANDLE_VALUE) return;
    if (path_join(pat, MAX_PATH, real, L"*") && (f = FindFirstFileW(pat, &fd)) != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (!upd_leftover(fd.cFileName, slash + 1)) continue;
            if (!path_join(path, MAX_PATH, real, fd.cFileName)) continue;
            delete_object(path, real, 0);
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    CloseHandle(dirh);
}
