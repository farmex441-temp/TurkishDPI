#ifndef TURKISHDPI_SYSTEM_H
#define TURKISHDPI_SYSTEM_H
#include <stddef.h>
#include <wchar.h>

int  sys_find_rival(wchar_t *name_out, size_t cap);   /* another bypass/VPN tool running */
int  sys_proxy_broken(void);                           /* manual proxy -> dead local port */
int  sys_proxy_repair(void);
int  sys_check_update(wchar_t *tag_out, size_t cap);  /* newer GitHub release? */
void sys_uninstall(const wchar_t *data_dir);
/* admin-owned, protected DACL; users_read for folders users must run from */
int  sys_secure_dir(const wchar_t *dir, int users_read);
/* objects this process creates are owned by Administrators */
int  sys_set_default_owner(void);
/* Protected %ProgramFiles%\TurkishDPI copy used by the elevated logon task. */
int  sys_install_dir(wchar_t *out, size_t cap);
/* self_exe: handle to the running exe file (see g_self_exe in app.c) */
int  sys_install_copy(void *self_exe, const wchar_t *running_exe, wchar_t *installed_exe, size_t cap);
void sys_uninstall_install_dir(const wchar_t *running_exe);
/*
 * One-click update to release tag (e.g. L"v2.0.2") in the exe's own folder.
 * self_exe: the running exe's lock handle (read, no write/delete sharing; if
 * it also has DELETE access the move needs no unlocked moment). It is given
 * up on success; on 0/-1 *self_exe is again a valid lock of the running exe
 * (possibly a different handle).
 * Returns 1 with *lock_out = the new exe opened read-only without write/delete
 * sharing (start it, keep the handle until exit); 0 failed, the running exe
 * is (back) under its name and nothing else changed; -1 refused (the release
 * changes WinDivert: update by hand); -2 the running exe could not be put
 * back under its name or locked again (another process holds the name): the
 * app must not go on (restart it by hand; the old exe may be <name>.<n>.old).
 * Downloads are bounded (30 s checksums, 3 min package).
 * why: short English reason for the log.
 */
int  sys_update_install(const wchar_t *tag, void **self_exe, const wchar_t *running_exe,
                        void **lock_out, const char **why);
void sys_update_cleanup(const wchar_t *running_exe);

#endif
