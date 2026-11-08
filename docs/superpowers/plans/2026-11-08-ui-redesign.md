# TurkishDPI UI Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the single crowded 440×734 window with a calm 400×520 three-tab window (Durum | Ayrıntılar | Ayarlar), keyboard-accessible, with a privilege-free `--ui-preview` mode for screenshots.

**Architecture:** All UI work stays in `src/app/ui.c` (plus a 4-line branch in `main()` and two prototypes in `app.h`). Focusable controls become native `BUTTON`s with `BS_OWNERDRAW`, so `IsDialogMessageW` (already in the app's message loop) gives Tab/Enter/Space and screen-reader names. Colors move to a `palette_t` table; headlines derive from `g_ui.kind`.

**Tech Stack:** C (gnu11), Win32, GDI+ flat API, GDI ClearType text; Pillow for icons/screenshot conversion; PowerShell for the screenshot script.

**Spec:** `docs/design/DESIGN-SYSTEM.md`, `docs/design/SCREENS.md` (Layout A + "Kararlar"), `docs/design/PREVIEW-PLAN.md`, `docs/design/UI-AUDIT.md`.

## Global Constraints

- No new UI framework; only Win32 + GDI/GDI+. No timers or painting while hidden.
- Every coordinate goes through `S()`/`SF()`; works at 100/125/150/200 % and on `WM_DPICHANGED`.
- Client area 400 × 520 logical px.
- Text/background contrast ≥ 4.5:1 using DESIGN-SYSTEM §4 tokens; every state has glyph + text, not color only.
- Every GDI/GDI+ object created in a paint is freed in that paint.
- UI never blocks; actions only via `post_cmd` / `post_site_test` / existing `app_*` calls.
- msvcrt + ucrt: `_snwprintf` + explicit termination, `%ls`, `%lu`.
- Icons: uncompressed BMP frames, max 128 px.
- Payment-protection indicator and "Kaldır ve temizle…" stay reachable.
- Every number shown comes from a real `g_ui`/global value; unknown → "—".
- Do not touch controller/engine logic (`app.c` beyond `main()` branch, `smart.c`, `probe.c`, `system.c`, `src/engine/`).

## Review Focus

1. Preview mode reaching a privileged call (ini write, schtasks, uninstall, URL/notepad) — every `command()`/`click()` id must hit the preview gate.
2. Real app running while preview runs — preview must not be found by `FindWindowW(L"TurkishDPI_main")`.
3. 8 targets with long host names and long network names — no overlap or overflow on Ayrıntılar.
4. DPI change while on Ayrıntılar/Ayarlar — child buttons and EDIT must be re-laid out and fonts re-assigned.
5. Hidden-window behaviour — spinner timer must stop on hide and on leaving scanning state.

---

### Task 1: `--ui-preview` mode on the current UI

**Files:** Modify `src/app/app.c` (`main()` only), `src/app/app.h`, `src/app/ui.c`.

**Interfaces — Produces:**
- `int ui_preview_main(int argc, char **argv);` (ui.c) — returns process exit code.
- `extern int g_ui_preview;` (ui.c) — 1 only in preview.
- Args: `--ui-preview <scenario> [--scale F] [--page status|details|settings] [--shot PATH.bmp]`.

- [ ] Add branch in `main()` before `--selftest`: `if (argc >= 2 && !strcmp(argv[1], "--ui-preview")) return ui_preview_main(argc, argv);`
- [ ] Implement: init `g_lock`, `preview_fill(name)` (scenario table from PREVIEW-PLAN §4; unknown name → exit 2), window class `TurkishDPI_preview`, no tray, no message filter, scale override, gate at top of `click()`/`command()` (`preview_action` writes `site_result`), `WM_CLOSE` → `PostQuitMessage`, `WM_POWERBROADCAST`/`WM_ENDSESSION` ignored.
- [ ] `--shot`: window at (-32000,-32000) with `WS_EX_TOOLWINDOW`, `PrintWindow(PW_CLIENTONLY|PW_RENDERFULLCONTENT)` into 32-bit DIB, write BMP by hand, exit 0.
- [ ] Verify: build x64; `$env:__COMPAT_LAYER='RunAsInvoker'; .\TurkishDPI.exe --ui-preview active --scale 1.5 --shot a.bmp` → exit 0, BMP width = round(BASE_W×1.5). Unknown scenario → exit 2. `%ProgramData%\TurkishDPI` mtimes unchanged.
- [ ] Commit `UI: --ui-preview mode with mock state and screenshots`.

### Task 2: Screenshot script + "before" set

**Files:** Create `tools/ui_shots.ps1`, `tools/bmp_to_png.py`; output `docs/design/screenshots/before/`.

- [ ] Script params `-Exe`, `-Out`, `-Scales 1,1.25,1.5,2`, `-Scenarios`, `-Pages`; names `<scenario>[-<page>]@<pct>.png`; converts with `python -I tools/bmp_to_png.py in out`; fails if any run exits non-zero.
- [ ] Capture all scenarios at 100 % and `active` at all scales into `before/`.
- [ ] Commit.

### Task 3: Tokens and typography

**Files:** `src/app/ui.c`.

- [ ] Replace `C_*`/`A_*` with `static const palette_t PAL_DARK` (DESIGN-SYSTEM §4.1/4.2 values) and `static const palette_t *P`; COLORREF/ARGB helpers.
- [ ] Fonts: `f_display 24 SB`, `f_title 16 SB`, `f_body 14`, `f_caption 12`, `f_label 12 SB`; prefer "Segoe UI Variable Text" when installed.
- [ ] Verify build; commit with Task 4 (no visible value alone).

### Task 4: Layout A shell + Durum page

**Files:** `src/app/ui.c`.

**Interfaces — Produces:** `enum page { PG_STATUS, PG_DETAILS, PG_SETTINGS }`, `static int g_page`; control ids `ID_TAB0..2 = 200..202`, `ID_POWER = 210`, `ID_ACTION = 211`, `ID_BANNER = 212`; `static const wchar_t *kind_headline(state_kind_t)`, `kind_sub(...)`, `speed_word(int ms)` (<250 "iyi", <600 "orta", else "yavaş").

- [ ] BASE 400×520; owner-draw buttons created in `WM_CREATE`, shown/hidden per page in `show_page(int)`; `WM_DRAWITEM` draws tabs, ring with state glyph (§6), buttons, focus ring (`ODS_FOCUS`, 2 px `focus`).
- [ ] Durum page per SCREENS A1 table; banner shows highest-priority notice and "+N" when more.
- [ ] Window text of each button = accessible name ("Duraklat"/"Devam et", banner text); `NotifyWinEvent(EVENT_OBJECT_NAMECHANGE)` on change.
- [ ] Verify via preview shots for all scenarios at 100 % and `active` at 4 scales; Tab cycles tabs→ring→action.
- [ ] Commit.

### Task 5: Ayrıntılar page

- [ ] Rows per SCREENS A2 (Yöntem + "Değiştir ▾" → existing `profile_menu`, Mod, DNS, Bağlantı, Öğrenilen), 8 target rows at 24 px with ✓/✕/— glyph + text, site EDIT + "Kontrol et" button, result (2 lines), counters, "Yeniden tara".
- [ ] Verify `active-long` (8 targets, long names) at 100/200 %: no overlap.
- [ ] Commit.

### Task 6: Ayarlar page

- [ ] Three toggles (owner-draw buttons, names "…: açık/kapalı") → `IDM_AUTO`, `IDM_AUTOSTART`, `IDM_PAYMENT` via existing `command()`; link row (`IDM_LOG`, `IDM_INI`, `IDM_AVHELP`, `IDM_GITHUB`); version line; danger section "Kaldır ve temizle…" separate from "Çıkış". Remove settings native menu.
- [ ] Payment status line remains on Durum page bottom.
- [ ] Verify shots `manual`, `payment-off`; commit.

### Task 7: Tray T1 + flat app icon

**Files:** `tools/make_icons.py`, `src/app/res/*.ico`, `assets/logo.png`, `src/app/ui.c` (`tray_icon_index`, tooltip).

- [ ] Drop gloss/shadow; tray variants fill the shield with status colors (ok/warn/busy/idle/error from §4.2 dark); keep BMP frames ≤ 64 px for tray, ≤ 128 app.
- [ ] Index mapping: ACTIVE→ok, PARTIAL→warn, STARTING/SCANNING→busy, STANDBY/PAUSED/OFFLINE→idle, ERROR→error. Needs a 5th tray icon (`15 ICON res/tray_warn.ico` in .rc).
- [ ] Tooltip "TurkishDPI · <headline> · <network>".
- [ ] Commit.

### Task 8: Verification and docs

- [ ] Build x64 and x86 with `-Wall -Wextra` clean.
- [ ] Full screenshot set into `docs/design/screenshots/after/`.
- [ ] Memory: preview working set/private bytes + GDI/USER counts, before vs after.
- [ ] Update CLAUDE.md (ui.c line, `--ui-preview`, icon rule); commit.
