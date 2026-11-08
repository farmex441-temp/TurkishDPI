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
        if (GetFileInformationByHandleEx(h, FileBasicInfo, &bi, sizeof(bi)) &&
            (bi.FileAttributes & FILE_ATTRIBUTE_READONLY)) {
            bi.FileAttributes &= ~(DWORD)FILE_ATTRIBUTE_READONLY;
            SetFileInformationByHandle(h, FileBasicInfo, &bi, sizeof(bi));
        }
        di.DeleteFile = TRUE;
        ok = SetFileInformationByHandle(h, FileDispositionInfo, &di, sizeof(di)) != 0;
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
    if (_snwprintf(pat, MAX_PATH - 1, L"%ls\\*", dir) < 0) return 0;
    pat[MAX_PATH - 1] = 0;
    f = FindFirstFileW(pat, &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            if (_snwprintf(path, MAX_PATH - 1, L"%ls\\%ls", dir, fd.cFileName) < 0) { ok = 0; continue; }
            path[MAX_PATH - 1] = 0;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                ok &= delete_object(path, root, 0);         /* the link, never its target */
            } else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                ok &= delete_tree_in(path, root, delay_ok);
            } else if (!delete_object(path, root, 0)) {
                wchar_t aside[MAX_PATH];
                if (!delay_ok) { ok = 0; continue; }
                /* in use: rename, then delete the renamed file at reboot (a
                 * reinstall under the original name survives the reboot) */
                _snwprintf(aside, MAX_PATH - 1, L"%ls.%lu.del", path, GetTickCount());
                aside[MAX_PATH - 1] = 0;
                if (MoveFileExW(path, aside, 0) && MoveFileExW(aside, NULL, MOVEFILE_DELAY_UNTIL_REBOOT))
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
        _snwprintf(aside, MAX_PATH - 1, L"%ls.untrusted-%lu", dir, GetTickCount());
        aside[MAX_PATH - 1] = 0;
        /* fails while the other process holds it open without delete sharing */
        if (!MoveFileExW(dir, aside, 0)) return 0;
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
        _snwprintf(pat, MAX_PATH - 1, L"%ls\\*", root);
        pat[MAX_PATH - 1] = 0;
        f = FindFirstFileW(pat, &fd);
        if (f != INVALID_HANDLE_VALUE) {
            do {
                int t;
                if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
                _snwprintf(path, MAX_PATH - 1, L"%ls\\%ls", root, fd.cFileName);
                path[MAX_PATH - 1] = 0;
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
        { L"LICENSE", 0 }, { L"NOTICE", 0 }, { L"WinDivert-LICENSE.txt", 0 }, { L"BENIOKU.txt", 0 },
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
    if (_snwprintf(installed_exe, cap, L"%ls\\TurkishDPI.exe", dst_dir) < 0) return 0;
    installed_exe[cap - 1] = 0;
    if (!_wcsicmp(src_dir, dst_dir)) return 1;        /* already running from there */
    if (!sys_secure_dir(dst_dir, 1)) return 0;         /* admin-owned, runnable by users */

    for (size_t i = 0; i < sizeof(FILES) / sizeof(FILES[0]); i++) {
        HANDLE h;
        int ok, is_driver = wcsstr(FILES[i].name, L".sys") != NULL;
        BY_HANDLE_FILE_INFORMATION fi;
        /* only drivers whose hash is pinned in this build are ever installed */
        if (is_driver && !windivert_file_pinned(FILES[i].name)) continue;
        _snwprintf(src, MAX_PATH, L"%ls\\%ls", src_dir, FILES[i].name);  src[MAX_PATH - 1] = 0;
        _snwprintf(dst, MAX_PATH, L"%ls\\%ls", dst_dir, FILES[i].name);  dst[MAX_PATH - 1] = 0;
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
    _snwprintf(dst, MAX_PATH, L"%ls\\TurkishDPI.exe", dst_dir);  dst[MAX_PATH - 1] = 0;
    return have_driver && install_one(self_exe, dst);
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
