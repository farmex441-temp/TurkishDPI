/* MSVC: /DUPD_TEST /Isrc/app /Isrc/engine /Itests tests/update_install_test.c src/app/updpkg.c advapi32.lib shell32.lib ole32.lib */
/*
 * TurkishDPI: the install half of the one-click update (upd_install_files
 * in src/app/system.c) against real files - no elevation, no network, no
 * running TurkishDPI. A dummy "TurkishDPI.exe" in a fresh folder under the
 * current directory plays the running exe: it is locked the way app.c's
 * lock_self_exe does (read, sharing only reads). A hook inside the install
 * plays the other (same-user) process at the risky moments: it squats the
 * exe's name, holds or swaps the new file, holds the old exe. Every case
 * checks what is left under the exe's name, the ".old" files, the temp
 * files, the texts and whether the lock handed back really locks.
 * Also: path_join/wfmt at the exact-fit boundary and the cleanup patterns.
 *
 *   gcc -DUPD_TEST -Isrc/app -Isrc/engine -Itests tests/update_install_test.c src/app/updpkg.c \
 *       -ladvapi32 -lshell32 -lole32 -lws2_32 -o update_install_test
 */
#include "system.c"   /* the code under test, with UPD_TEST hooks */

/* ---------- stand-ins for what system.c links against ---------- */
int probe_tcp_connect_ms(unsigned long ip, unsigned short port, DWORD timeout_ms) {
    (void)ip; (void)port; (void)timeout_ms;
    return -2;
}
void sysdll_winhttp_tls(void *session) { (void)session; }
sd_WinHttpOpen_t sd_WinHttpOpen;
sd_WinHttpCloseHandle_t sd_WinHttpCloseHandle;
sd_WinHttpConnect_t sd_WinHttpConnect;
sd_WinHttpOpenRequest_t sd_WinHttpOpenRequest;
sd_WinHttpQueryHeaders_t sd_WinHttpQueryHeaders;
sd_WinHttpQueryOption_t sd_WinHttpQueryOption;
sd_WinHttpReadData_t sd_WinHttpReadData;
sd_WinHttpReceiveResponse_t sd_WinHttpReceiveResponse;
sd_WinHttpSendRequest_t sd_WinHttpSendRequest;
sd_WinHttpSetOption_t sd_WinHttpSetOption;
sd_WinHttpSetTimeouts_t sd_WinHttpSetTimeouts;
int windivert_file_pinned(const wchar_t *name) { (void)name; return 0; }
int windivert_handle_ok(HANDLE f, const wchar_t *name) { (void)f; (void)name; return 0; }
int sha256_mem(const void *data, size_t len, unsigned char out[32]) {
    (void)data; (void)len; memset(out, 0, 32);
    return 0;
}

/* ---------- helpers ---------- */
static int fails;
#define EXPECT(c, what) do { if (!(c)) { printf("FAIL: %s: %s\n", g_case, what); fails++; } } while (0)

static const char *g_case = "";
static wchar_t g_root[MAX_PATH], g_dir[MAX_PATH], g_exe[MAX_PATH];
static const wchar_t *EXE = L"TurkishDPI.exe";
static const char *OLD_EXE = "MZ old exe 2.0.1", *NEW_EXE = "MZ new exe 2.0.2 (longer)";

static void put_file(const wchar_t *name, const char *bytes) {
    wchar_t p[MAX_PATH];
    DWORD w;
    HANDLE h;
    path_join(p, MAX_PATH, g_dir, name);
    h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("cannot write %ls\n", p); exit(2); }
    WriteFile(h, bytes, (DWORD)strlen(bytes), &w, NULL);
    CloseHandle(h);
}

/* 1 same bytes, 0 different, -1 missing */
static int file_is(const wchar_t *name, const char *bytes) {
    wchar_t p[MAX_PATH];
    char buf[256];
    DWORD got = 0;
    HANDLE h;
    path_join(p, MAX_PATH, g_dir, name);
    h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    ReadFile(h, buf, sizeof(buf), &got, NULL);
    CloseHandle(h);
    return got == strlen(bytes) && !memcmp(buf, bytes, got);
}

/* Number of "<exe>.<digits>.old" entries; *files of them hold bytes (if given). */
static int count_old(const char *bytes, int *matching, int *dirs) {
    wchar_t pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE f;
    int n = 0;
    if (matching) *matching = 0;
    if (dirs) *dirs = 0;
    path_join(pat, MAX_PATH, g_dir, L"*");
    f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!upd_leftover(fd.cFileName, EXE) || wcsstr(fd.cFileName, L".upd-new")) continue;
        n++;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (dirs) (*dirs)++; continue; }
        if (bytes && matching && file_is(fd.cFileName, bytes) == 1) (*matching)++;
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return n;
}

static int count_upd_new(void) {
    wchar_t pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE f;
    int n = 0;
    path_join(pat, MAX_PATH, g_dir, L"*.upd-new");
    f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return 0;
    do n++; while (FindNextFileW(f, &fd));
    FindClose(f);
    return n;
}

static void rm_tree(const wchar_t *dir) {
    wchar_t pat[MAX_PATH], p[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE f;
    path_join(pat, MAX_PATH, dir, L"*");
    f = FindFirstFileW(pat, &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            path_join(p, MAX_PATH, dir, fd.cFileName);
            SetFileAttributesW(p, FILE_ATTRIBUTE_NORMAL);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                rm_tree(p);
            else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                RemoveDirectoryW(p);
            else
                DeleteFileW(p);
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    RemoveDirectoryW(dir);
}

/* A fresh folder holding the "running" exe (+ WinDivert.dll when wd), and
 * its lock like app.c's lock_self_exe (with_delete: also DELETE access). */
static HANDLE setup(const char *name, const char *wd, int with_delete) {
    wchar_t wname[64];
    HANDLE h;
    g_case = name;
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 64);
    path_join(g_dir, MAX_PATH, g_root, wname);
    rm_tree(g_dir);
    CreateDirectoryW(g_dir, NULL);
    put_file(EXE, OLD_EXE);
    put_file(L"LICENSE", "old licence");
    if (wd) put_file(L"WinDivert.dll", wd);
    path_join(g_exe, MAX_PATH, g_dir, EXE);
    h = CreateFileW(g_exe, GENERIC_READ | (with_delete ? DELETE : 0), FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("cannot lock %ls\n", g_exe); exit(2); }
    return h;
}

/* 1 if h is a valid handle to the file at path that keeps others from
 * writing, renaming or deleting it. */
static int locks(HANDLE h, const wchar_t *path) {
    HANDLE w, d;
    int ok;
    if (h == INVALID_HANDLE_VALUE || h == NULL) return 0;
    w = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    d = CreateFileW(path, DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    ok = w == INVALID_HANDLE_VALUE && d == INVALID_HANDLE_VALUE;
    if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
    if (d != INVALID_HANDLE_VALUE) CloseHandle(d);
    {   /* and it is that file */
        BY_HANDLE_FILE_INFORMATION a, b;
        HANDLE r = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, 0, NULL);
        ok = ok && r != INVALID_HANDLE_VALUE && GetFileInformationByHandle(r, &a) &&
             GetFileInformationByHandle(h, &b) && same_file(&a, &b);
        if (r != INVALID_HANDLE_VALUE) CloseHandle(r);
    }
    return ok;
}

/* The package: exe, optional WinDivert.dll, a new licence. */
static unsigned char *g_data[UPD_NITEMS];
static size_t g_len[UPD_NITEMS];
static void package(const char *wd) {
    memset(g_data, 0, sizeof(g_data));
    memset(g_len, 0, sizeof(g_len));
    g_data[0] = (unsigned char *)NEW_EXE;  g_len[0] = strlen(NEW_EXE);
    if (wd) { g_data[1] = (unsigned char *)wd; g_len[1] = strlen(wd); }
    g_data[4] = (unsigned char *)"new licence";  g_len[4] = 11;
}

/* ---------- the other process ---------- */
static void (*g_hook)(int step, const wchar_t *real, const wchar_t *base);
static HANDLE volatile g_held = INVALID_HANDLE_VALUE;
static int g_folder_renamed = -1;

void upd_test_hook(int step, const wchar_t *real, const wchar_t *base) {
    if (g_hook) g_hook(step, real, base);
}

static void release_held(void) {   /* also called from release_later's thread */
    HANDLE h = (HANDLE)InterlockedExchangePointer((PVOID volatile *)&g_held, INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

static void hook_squat_file(int step, const wchar_t *real, const wchar_t *base) {
    (void)real; (void)base;
    if (step == UPD_STEP_MOVED_ASIDE) put_file(EXE, "squatter");
}
static void hook_squat_dir(int step, const wchar_t *real, const wchar_t *base) {
    (void)real; (void)base;
    if (step == UPD_STEP_MOVED_ASIDE) CreateDirectoryW(g_exe, NULL);
}
static void hook_hold_new_sharing_delete(int step, const wchar_t *real, const wchar_t *base) {
    (void)real; (void)base;
    if (step == UPD_STEP_NEW_IN_PLACE)
        g_held = CreateFileW(g_exe, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             NULL, OPEN_EXISTING, 0, NULL);
}
static void hook_hold_new_no_delete(int step, const wchar_t *real, const wchar_t *base) {
    (void)real; (void)base;
    if (step == UPD_STEP_NEW_IN_PLACE)
        g_held = CreateFileW(g_exe, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                             OPEN_EXISTING, 0, NULL);
}
static void hook_modify_new(int step, const wchar_t *real, const wchar_t *base) {
    (void)real; (void)base;
    if (step == UPD_STEP_NEW_IN_PLACE) {
        DWORD w;
        HANDLE h = CreateFileW(g_exe, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) { WriteFile(h, "X", 1, &w, NULL); CloseHandle(h); }
    }
}
static void hook_swap_new(int step, const wchar_t *real, const wchar_t *base) {
    (void)real; (void)base;
    if (step == UPD_STEP_NEW_IN_PLACE) {
        wchar_t other[MAX_PATH];
        path_join(other, MAX_PATH, g_dir, L"other.bin");
        MoveFileExW(g_exe, other, 0);
        put_file(EXE, NEW_EXE);   /* same bytes, another file */
    }
}
static void hook_hold_old(int step, const wchar_t *real, const wchar_t *base) {
    (void)real; (void)base;
    if (step == UPD_STEP_UNLOCKED)
        g_held = CreateFileW(g_exe, DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                             OPEN_EXISTING, 0, NULL);
}
static DWORD WINAPI release_later(LPVOID unused) {
    (void)unused;
    Sleep(300);
    release_held();
    return 0;
}
static void hook_hold_old_briefly(int step, const wchar_t *real, const wchar_t *base) {
    hook_hold_old(step, real, base);
    if (step == UPD_STEP_UNLOCKED) CloseHandle(CreateThread(NULL, 0, release_later, NULL, 0, NULL));
}
static void hook_rename_folder(int step, const wchar_t *real, const wchar_t *base) {
    (void)base;
    if (step == UPD_STEP_MOVED_ASIDE) {
        wchar_t to[MAX_PATH];
        wfmt(to, MAX_PATH, L"%ls-moved", real);
        g_folder_renamed = MoveFileExW(real, to, 0) != 0;
        if (g_folder_renamed) MoveFileExW(to, real, 0);
    }
}

static int run(HANDLE *self, HANDLE *lock, const char **why) {
    int rc = upd_install_files(self, g_exe, g_data, g_len, lock, why);
    g_hook = NULL;
    return rc;
}

static void finish(HANDLE self, HANDLE lock) {
    release_held();
    if (self != INVALID_HANDLE_VALUE) CloseHandle(self);
    if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
    rm_tree(g_dir);
}

int main(void) {
    HANDLE self, lock;
    const char *why;
    int rc, match, dirs;
    DWORD n = GetCurrentDirectoryW(MAX_PATH, g_root);
    if (!n || n >= MAX_PATH - 40) return 2;
    wcscat(g_root, L"\\upd_install_test");
    rm_tree(g_root);
    CreateDirectoryW(g_root, NULL);

    /* ---- pure helpers ---- */
    {
        wchar_t b[10];
        g_case = "path_join";
        EXPECT(path_join(b, 10, L"abc", L"defgh") && !wcscmp(b, L"abc\\defgh"), "9 characters fit in 10");
        EXPECT(!path_join(b, 10, L"abc", L"defghi") && b[0] == 0, "exactly 10 characters do not fit");
        EXPECT(!path_join(b, 10, L"abc", L"defghijklmnop") && b[0] == 0, "longer does not fit");
        EXPECT(wfmt(b, 10, L"%ls.%lu", L"a", 1234567ul) && !wcscmp(b, L"a.1234567"), "wfmt fits");
        EXPECT(!wfmt(b, 9, L"%ls.%lu", L"a", 1234567ul), "wfmt exact count refused");
        EXPECT(!wfmt(b, 0, L"x"), "zero capacity");
    }
    {
        static const wchar_t *yes[] = {
            L"TurkishDPI.exe.123.old", L"turkishdpi.EXE.0.OLD", L"TurkishDPI.exe.4294967295.old",
            L"TurkishDPI.exe.upd-new", L"LICENSE.upd-new", L"WinDivert64.sys.upd-new", L"benioku.TXT.upd-new",
        };
        static const wchar_t *no[] = {
            L"TurkishDPI.exe.old", L"TurkishDPI.exe..old", L"TurkishDPI.exe.12a.old", L"TurkishDPI.exe.12345678901.old",
            L"TurkishDPI.exe.backup.old", L"notes.old", L"TurkishDPI.exe.123.old.txt", L"Other.exe.123.old",
            L"photo.jpg.upd-new", L".upd-new", L"TurkishDPI.exe.123.upd-new", L"LICENSEX.upd-new", L"xTurkishDPI.exe.1.old",
        };
        g_case = "cleanup patterns";
        for (size_t i = 0; i < sizeof(yes) / sizeof(yes[0]); i++) EXPECT(upd_leftover(yes[i], EXE), "ours");
        for (size_t i = 0; i < sizeof(no) / sizeof(no[0]); i++) {
            if (upd_leftover(no[i], EXE)) printf("  wrongly ours: %ls\n", no[i]);
            EXPECT(!upd_leftover(no[i], EXE), "not ours");
        }
    }

    /* ---- installs ---- */
    for (int del = 0; del < 2; del++) {
        self = setup(del ? "success, lock with DELETE" : "success", "WD", del);
        package("WD");
        rc = run(&self, &lock, &why);
        EXPECT(rc == 1, why);
        EXPECT(file_is(EXE, NEW_EXE) == 1, "new exe under the name");
        EXPECT(count_old(OLD_EXE, &match, NULL) == 1 && match == 1, "old exe kept as <name>.<n>.old");
        EXPECT(count_upd_new() == 0, "no temp files");
        EXPECT(file_is(L"LICENSE", "new licence") == 1, "texts replaced after success");
        EXPECT(self == INVALID_HANDLE_VALUE, "old lock given up");
        EXPECT(locks(lock, g_exe), "new exe locked");
        if (!del) {   /* cleanup removes exactly our leftovers */
            put_file(L"notes.old", "x");
            put_file(L"TurkishDPI.exe.backup.old", "x");
            put_file(L"photo.jpg.upd-new", "x");
            put_file(L"LICENSE.upd-new", "x");
            put_file(L"TurkishDPI.exe.upd-new", "x");
            CloseHandle(lock);
            lock = INVALID_HANDLE_VALUE;
            sys_update_cleanup(g_exe);
            EXPECT(count_old(NULL, NULL, NULL) == 0, "cleanup removed <name>.<n>.old");
            EXPECT(file_is(L"LICENSE.upd-new", "x") == -1 && file_is(L"TurkishDPI.exe.upd-new", "x") == -1,
                   "cleanup removed our .upd-new files");
            EXPECT(file_is(L"notes.old", "x") == 1 && file_is(L"TurkishDPI.exe.backup.old", "x") == 1 &&
                   file_is(L"photo.jpg.upd-new", "x") == 1, "cleanup left the user's files alone");
            EXPECT(file_is(EXE, NEW_EXE) == 1, "cleanup kept the exe");
        }
        finish(self, lock);
    }

    self = setup("WinDivert in folder differs", "WD", 0);
    package("WD-NEW");
    rc = run(&self, &lock, &why);
    EXPECT(rc == -1 && file_is(EXE, OLD_EXE) == 1 && count_old(NULL, NULL, NULL) == 0, "refused, nothing moved");
    EXPECT(file_is(L"LICENSE", "old licence") == 1 && count_upd_new() == 0, "texts untouched, no temp files");
    EXPECT(locks(self, g_exe) && lock == INVALID_HANDLE_VALUE, "still locked");
    finish(self, lock);

    self = setup("WinDivert in folder, missing in zip", "WD", 0);
    package(NULL);
    rc = run(&self, &lock, &why);
    EXPECT(rc == -1 && file_is(EXE, OLD_EXE) == 1 && locks(self, g_exe), "refused");
    finish(self, lock);

    self = setup("WinDivert only in zip", NULL, 0);
    package("WD");
    rc = run(&self, &lock, &why);
    EXPECT(rc == 1 && file_is(EXE, NEW_EXE) == 1, why);
    EXPECT(file_is(L"WinDivert.dll", "WD") == -1, "not installed");
    finish(self, lock);

    self = setup("folder cannot be renamed during the update", "WD", 0);
    package("WD");
    g_hook = hook_rename_folder;
    rc = run(&self, &lock, &why);
    EXPECT(g_folder_renamed == 0, "folder rename refused");
    EXPECT(rc == 1 && file_is(EXE, NEW_EXE) == 1, why);
    finish(self, lock);

    /* someone creates the exe's name between the two renames */
    self = setup("squatter file after the move", "WD", 0);
    package("WD");
    g_hook = hook_squat_file;
    rc = run(&self, &lock, &why);
    EXPECT(rc == 0, why);
    EXPECT(file_is(EXE, OLD_EXE) == 1, "original back under its name");
    EXPECT(count_old(NULL, NULL, NULL) == 0 && count_upd_new() == 0, "nothing left over");
    EXPECT(file_is(L"LICENSE", "old licence") == 1, "texts untouched");
    EXPECT(locks(self, g_exe) && lock == INVALID_HANDLE_VALUE, "original locked again");
    finish(self, lock);

    self = setup("squatter directory after the move", "WD", 0);
    package("WD");
    g_hook = hook_squat_dir;
    rc = run(&self, &lock, &why);
    EXPECT(rc == 0 && file_is(EXE, OLD_EXE) == 1, "original back under its name");
    EXPECT(count_old(NULL, NULL, &dirs) == 1 && dirs == 1 && count_upd_new() == 0, "directory moved aside");
    EXPECT(locks(self, g_exe), "original locked again");
    finish(self, lock);

    /* the new file is held open (with delete sharing) before the lock */
    self = setup("new exe held open", "WD", 0);
    package("WD");
    g_hook = hook_hold_new_sharing_delete;
    rc = run(&self, &lock, &why);
    EXPECT(rc == 0 && file_is(EXE, OLD_EXE) == 1, "original back under its name");
    EXPECT(count_old(NEW_EXE, &match, NULL) == 1 && match == 1 && count_upd_new() == 0, "held file moved aside");
    EXPECT(locks(self, g_exe) && file_is(L"LICENSE", "old licence") == 1, "locked, texts untouched");
    finish(self, lock);

    self = setup("new exe changed before the lock", "WD", 0);
    package("WD");
    g_hook = hook_modify_new;
    rc = run(&self, &lock, &why);
    EXPECT(rc == 0 && file_is(EXE, OLD_EXE) == 1 && count_old(NULL, NULL, NULL) == 0, "original back, changed file gone");
    EXPECT(locks(self, g_exe), "original locked again");
    finish(self, lock);

    self = setup("new exe swapped before the lock", "WD", 0);
    package("WD");
    g_hook = hook_swap_new;
    rc = run(&self, &lock, &why);
    EXPECT(rc == 0 && file_is(EXE, OLD_EXE) == 1, "original back (same bytes, other file refused)");
    EXPECT(locks(self, g_exe) && count_upd_new() == 0, "original locked again");
    finish(self, lock);

    /* the only state the updater cannot repair: the name is held without
     * delete sharing - -2, and the old exe is still there as .old */
    self = setup("new exe held without delete sharing", "WD", 0);
    package("WD");
    g_hook = hook_hold_new_no_delete;
    rc = run(&self, &lock, &why);
    EXPECT(rc == -2 && strstr(why, "could not be put back"), why);
    EXPECT(self == INVALID_HANDLE_VALUE && lock == INVALID_HANDLE_VALUE, "no handles handed back");
    release_held();
    EXPECT(count_old(OLD_EXE, &match, NULL) == 1 && match == 1 && count_upd_new() == 0, "old exe kept as .old");
    finish(self, lock);

    /* with app.c's read-only lock the running exe is unlocked for a moment */
    self = setup("running exe held briefly while unlocked", "WD", 0);
    package("WD");
    g_hook = hook_hold_old_briefly;
    rc = run(&self, &lock, &why);
    EXPECT(rc == 1 && file_is(EXE, NEW_EXE) == 1 && locks(lock, g_exe), why);
    finish(self, lock);

    self = setup("running exe held while unlocked", "WD", 0);
    package("WD");
    g_hook = hook_hold_old;
    rc = run(&self, &lock, &why);
    EXPECT(rc == -2 && self == INVALID_HANDLE_VALUE, "lock lost: -2");
    release_held();
    EXPECT(file_is(EXE, OLD_EXE) == 1 && count_old(NULL, NULL, NULL) == 0 && count_upd_new() == 0, "nothing moved");
    finish(self, lock);

    rm_tree(g_root);
    g_case = "";
    printf(fails ? "%d failures\n" : "all passed\n", fails);
    return fails != 0;
}
