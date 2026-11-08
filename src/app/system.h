#ifndef TURKISHDPI_SYSTEM_H
#define TURKISHDPI_SYSTEM_H
#include <stddef.h>
#include <wchar.h>

int  sys_find_rival(wchar_t *name_out, size_t cap);   /* another bypass/VPN tool running */
int  sys_proxy_broken(void);                           /* manual proxy -> dead local port */
int  sys_proxy_repair(void);
int  sys_check_update(wchar_t *tag_out, size_t cap);  /* newer GitHub release? */
void sys_uninstall(const wchar_t *data_dir);
int  sys_secure_dir(const wchar_t *dir);              /* admin-owned, protected DACL */
/* Protected %ProgramFiles%\TurkishDPI copy used by the elevated logon task. */
int  sys_install_dir(wchar_t *out, size_t cap);
/* self_exe: handle to the running exe file (see g_self_exe in app.c) */
int  sys_install_copy(void *self_exe, const wchar_t *running_exe, wchar_t *installed_exe, size_t cap);
void sys_uninstall_install_dir(const wchar_t *running_exe);

#endif
