/*
 * TurkishDPI - load a DLL from System32 only, also on Windows 7.
 *
 * LOAD_LIBRARY_SEARCH_* flags exist on Windows 8+ and on Windows 7 with
 * KB2533623. Without that update LoadLibraryExW rejects them with
 * ERROR_INVALID_PARAMETER, which would leave the app without winhttp,
 * gdiplus, dnsapi, bcrypt and WinDivert.dll. The fallback loads the same
 * file by its absolute path (never by bare name, so the exe folder and the
 * current directory are not searched for it).
 */
#ifndef TURKISHDPI_SYSLIB_H
#define TURKISHDPI_SYSLIB_H

#include <windows.h>
#include <wchar.h>

#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100
#endif
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif

/* Absolute path: its own imports from System32 first. */
static inline HMODULE load_dll_path(const wchar_t *path) {
    HMODULE h = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (h || GetLastError() != ERROR_INVALID_PARAMETER) return h;
    /* Windows 7 without KB2533623: search the DLL's own folder for its imports */
    return LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
}

static inline HMODULE load_system_dll(const wchar_t *name) {
    wchar_t path[MAX_PATH];
    UINT n;
    HMODULE h = LoadLibraryExW(name, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (h || GetLastError() != ERROR_INVALID_PARAMETER) return h;
    n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n + wcslen(name) + 2 > MAX_PATH) return NULL;
    path[n] = L'\\';
    wcscpy(path + n + 1, name);
    return LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
}

#endif
