# TurkishDPI – project guide

TurkishDPI is a small native Windows program (C, Win32, no frameworks) that gets
around DPI/DNS-based website blocking in Turkey without a VPN. It is a fork of
**GoodbyeDPI** (ValdikSS) / **GoodbyeDPI-Turkey** (cagritaskn), extended with an
automatic, self-learning controller, its own UI and ideas taken from
**Shroudly** (umutxyp, MIT). License: Apache-2.0 (see `LICENSE`, `NOTICE`).

- Repo: https://github.com/farmex441-temp/TurkishDPI (public, account `farmex441-temp`)
- User-facing docs are Turkish (`README.md`, `docs/BENIOKU.txt`, UI strings).
  Code, comments and commit messages are English.

## How it works (one exe, two roles)

`TurkishDPI.exe` (requires admin, `requireAdministrator` manifest):

1. **App** (no args or `--tray`): tray icon + main window. A controller thread
   picks a strategy preset, starts the engine as a child process, probes test
   sites, learns per network, watches the connection and re-tunes.
2. **Engine** (`--engine ARGS`, or any GoodbyeDPI-style first argument like `-9`):
   the GoodbyeDPI packet loop on WinDivert. The app starts it hidden, reads its
   stdout through a pipe and kills it via a job object if the app dies.
   Old GoodbyeDPI `.cmd` scripts and `sc create` services work with it unchanged.

Other modes: `--selftest` (scan, verify payment pass-through, write `SELFTEST …`
lines to the log, exit; exits 3 if another instance runs), `--help`,
`--ui-preview <scenario> [--scale F] [--page status|details|settings]
[--focus N] [--shot out.bmp]` (developer tool, not in `--help`: the window with
mock `g_ui` data, no data folder/engine/network/tray, every action gated in
`do_click`/`command`; own window class so the real app never finds it; runs
without admin via `__COMPAT_LAYER=RunAsInvoker`; scenarios in `preview_fill`).

Engine -> app protocol (stdout lines): `Filter activated…`,
`Error opening filter: <code>`, `STATS packets=… https=… http=… excluded=… dns=…`
(STATS only when `gdpi_report_stats` is set, i.e. in `--engine` mode).

## Layout

```
src/app/            the app (controller, UI, probes, learning)
  app.c             entry point, config/ini, logging, engine process control,
                    auto_scan / manual_start / site_test, controller thread,
                    command queue, autostart (schtasks), conflict + uninstall
  app.h             shared ui_state_t g_ui (guarded by g_lock), commands, prototypes
  ui.c              400x520 dark window, pages Durum / Ayrıntılar / Ayarlar
                    (docs/design/), GDI+ flat API + GDI text; clickable items
                    are owner-drawn native BUTTONs (Tab/Space/Enter, screen
                    readers); palette_t colours; tray icon, --ui-preview;
                    never blocks on the controller
  smart.c           per-network learning: network id = gateway MAC, stats per
                    preset (trials, ok, avg ms) in [net_<id>] of the ini
  probe.c/.h        WinHTTP HTTPS probes (TCP+TLS set-up time via status
                    callback), DNS RTT probes, forged-answer check, block-page
                    IP learning via DnsQuery_A, facts for site diagnosis
  diag.c/.h         site diagnosis verdict (ok / no_dns / dns_poison /
                    tls_block / ip_block); pure C, no Win32, unit tested
  sysdll.c/.h       winhttp.dll + iphlpapi.dll resolved at run time from
                    System32 (not linked), SetDefaultDllDirectories
  system.c/.h       rival tools, broken system proxy repair, GitHub update check,
                    admin-only data folder (sys_secure_dir), protected
                    Program Files copy for autostart (sys_install_copy), uninstall
  presets.h         strategy presets ({DNS} placeholder) and clean DNS servers
  version.h         version + GitHub URLs
  TurkishDPI.rc, .exe.manifest, res/*.ico   resources (icons from tools/make_icons.py)
src/engine/         GoodbyeDPI engine (upstream code + TurkishDPI changes)
  goodbyedpi.c      option parsing (gdpi_main), WinDivert filters, packet loop
  dnsredir.c/.h     DNS redirection conntrack + TurkishDPI hybrid DNS race
  exclude.c/.h      payment/bank "never touch" list (SNI + IPs learned from DNS)
  windivert_dyn.c   loads WinDivert.dll at runtime from the exe's folder only
  fakepackets.c, ttltrack.c, blackwhitelist.c, service.c, utils/   upstream
tests/              unit tests (no admin): exclude_test.c, dns_test.c,
                    diag_test.c, windivert_hash_test.c (DLL + driver pinning, uses a
                    stand-in DLL, never the driver); probe_test.c is a manual
                    network/latency/diagnosis check
third_party/WinDivert/include/windivert.h   header only; DLL/SYS are not in git
tools/make_icons.py generates src/app/res/*.ico and assets/logo.png (Pillow):
                    flat shield; tray shields coloured by state (ok, busy,
                    idle, warn "!", error "x"); uncompressed BMP frames <=128 px
tools/ui_shots.ps1  --ui-preview screenshots (scenario x page x scale) ->
                    docs/design/screenshots/ (tools/bmp_to_png.py converts)
docs/design/        UI audit, design system, screens, preview plan
.github/workflows/build.yml   CI: tests on windows-latest, build x64/x86 on
                    Ubuntu MinGW, package zips, GitHub Release on v* tags
```

Runtime data: `%ProgramData%\TurkishDPI\` (admin-owned, protected DACL):
`TurkishDPI.ini` (settings + learned data), `TurkishDPI.log` (rotated at 1 MB).

## Key behaviours (what was built and why)

- **Auto scan** (`auto_scan`): no internet -> wait; measure all clean DNS servers
  in parallel and keep the fastest; learn the ISP's block-page IPs (engine must
  be stopped, so at most once per 24 h per network: `bt=` in `[net_<id>]`); try
  presets in learned order; on a new network measure up to two extra working
  presets and keep the fastest. The running engine stays on during the
  measurements and `engine_start` keeps it when the arguments are identical,
  so a re-check that ends on the same preset drops no connection.
  Watchdog every `WatchdogMinutes` (1-240) with a second strike: a failed
  check is repeated after 20 s, only a repeated failure rescans. Network
  changes are debounced (4 s quiet, 20 s max). Partial mode only rescans if
  it gets worse. Watchdog successes count in the learning stats once an hour.
- **Explained decisions**: every scan/start has a reason (`set_reason`,
  `WHY_*`): logged as `scan: reason=…` / `decision: preset N … reason=…` and
  shown after the profile name in the window.
- **Balloons** (`notify_state`): only when the kind of state last told to the
  user changes (error/partial/active); restarts and re-checks are silent.
- **Site test diagnosis**: before trying presets for a site that does not
  open, `probe_collect_facts` + `diag_classify` tell DNS poisoning and TLS/SNI
  blocks (worth a preset search) from IP blocks / dead servers and unknown
  names (reported at once; no preset can fix them).
- **STANDBY** only happens when no preset opens any target but the targets
  open without the engine (the engine itself is the problem). If the test
  sites are simply unblocked, every preset scores full and one stays active.
- **Hybrid DNS** (`--dns-hybrid`, `--dns-block-ip`): each query goes to the
  system resolver *and* the clean one; first trustworthy answer wins; forged
  answers (block pages, bogons, NXDOMAIN/SERVFAIL/REFUSED from the ISP) dropped.
- **Narrow filter**: outbound only HTTP requests (port 80 with payload) and TLS
  handshake records (443, payload starts `16 03`); clean-resolver answers only
  from that server's IP:port. Do not widen this without a reason: every
  diverted packet costs two context switches.
- **Payment protection** (default on, `--no-payment-exclude` / ini
  `PaymentProtection=0` to disable): ~100 bank/payment/3-D Secure domains are
  never modified, matched by SNI/Host and by addresses learned from DNS answers.
- **QUIC** is blocked (`-q`) in every preset so browsers fall back to TCP.
- **No bundled/embedded driver in the exe**: releases are zips with
  `TurkishDPI.exe` + WinDivert's signed `WinDivert.dll`/`WinDivert64.sys`
  (+ `WinDivert32.sys` on x86). The v1.0 single-file build that embedded and
  unpacked the driver was flagged 3/55 on VirusTotal and was removed. Never
  ship WinDivert's official zip/sample tools (Defender flags them). The app
  never changes antivirus settings itself.

## Build & test

Toolchain: MinGW-w64 GCC (CI) or llvm-mingw (local, used during development).

```bash
make ARCH=x86_64          # build/x86_64/TurkishDPI.exe
make ARCH=i686            # build/i686/TurkishDPI.exe
# put WinDivert.dll + WinDivert64.sys (x64) next to the exe to run it
```

Unit tests (run them under sanitizers when touching engine parsing code):

```bash
x86_64-w64-mingw32-gcc -g -O1 -fsanitize=undefined,address -Isrc/engine tests/dns_test.c src/engine/dnsredir.c -lws2_32 -o build/dns_test.exe
x86_64-w64-mingw32-gcc -g -O1 -fsanitize=undefined,address -Isrc/engine tests/exclude_test.c src/engine/exclude.c src/engine/utils/getline.c -o build/exclude_test.exe
```

Without MinGW, MSVC (`cl /fsanitize=address`, add `/Dssize_t=intptr_t`) builds
and runs the unit tests and compile-checks `src/app` + the TurkishDPI engine
files; the upstream engine (`goodbyedpi.c`, `service.c`, `fakepackets.c`)
needs GCC (unistd.h, getopt, `__attribute__`), so the full exe needs MinGW/CI.

Quick checks without admin:
- `__COMPAT_LAYER=RunAsInvoker TurkishDPI.exe --engine <args>`: "Error opening
  filter: 5" means the filter compiled (only the driver needs admin); 87 means
  a filter syntax error.
- `tests/probe_test.c`: DNS RTTs, learned block IPs, per-site TCP+TLS times.

Real end-to-end test (admin, UAC prompt; close any running instance first):
`TurkishDPI.exe --selftest`, then read `SELFTEST` lines in the log.

## Releasing

Bump `src/app/version.h`, commit, push `main`, push a tag `vX.Y.Z` (tags with a
`-`, e.g. `v1.1.0-rc2`, become pre-releases). CI builds and attaches
`TurkishDPI-<tag>-x64.zip`, `-x86.zip`, `SHA256SUMS.txt`. Do not delete or
re-push tags; publish a new version instead. WinDivert is pinned to 2.2.2-A by
SHA-256 in the workflow.

## Conventions and gotchas

- Code must compile with both msvcrt (CI GCC) and ucrt (clang): no `wcstok`
  with 3 args, use `_snwprintf` + explicit termination for bounded wide output,
  `%ls` for wide strings, `%lu` for DWORD.
- Engine buffers are packed bytes: use `memcpy`, not misaligned pointer casts.
- `g_lock` guards `g_ui` and the command queue; file logging uses `g_log_lock`.
  Never hold `g_lock` across process waits, file I/O or message boxes.
- The UI talks to the controller only through `post_cmd`/`post_site_test`.
- Elevated code must not trust user-writable paths:
  - data folder goes through `sys_secure_dir` (handle opened with
    `FILE_FLAG_OPEN_REPARSE_POINT`, owner + protected DACL set on that handle,
    entries owned by other users removed); the app refuses to start if it fails;
  - uninstall never follows reparse points;
  - the exe's own folder is NOT trusted (zips get extracted to Downloads):
    the logon task always runs the protected copy in
    `%ProgramFiles%\TurkishDPI` (`sys_install_copy`, refreshed when started
    from elsewhere, `autostart_migrate`); a failed copy means no task, never a
    task pointing at the extraction folder. The exe is copied from
    `g_self_exe` (the running image's file, opened at start without delete
    sharing), never re-opened by name; WinDivert files only if their hashes match;
  - WinDivert.dll **and the drivers** (`WinDivert64.sys`, `WinDivert32.sys`)
    only from the exe's own folder, and only if their SHA-256 match the ones
    pinned at build time (`WINDIVERT_DLL_SHA256`, `WINDIVERT_SYS64_SHA256`,
    `WINDIVERT_SYS32_SHA256`; Makefile computes them from the checksum-pinned
    archive; CI sets `REQUIRE_WINDIVERT_HASH=1`). Checked on handles that deny
    writes/deletes: the DLL's until it is mapped, the drivers' for the whole
    engine process (WinDivert.dll installs whatever .sys sits next to it as a
    kernel driver - an unpinned driver would allow loading any signed,
    possibly vulnerable driver). Error 1006 otherwise. `windivert_file_ok()`
    guards the Program Files copy the same way;
  - no static imports of non-KnownDLLs (winhttp, iphlpapi, dnsapi, bcrypt):
    `sysdll.c` / `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)`; CI's
    "Check imports" step fails the build otherwise; `schtasks.exe`/`notepad.exe`
    by absolute System32 path (`run_hidden`, `app_open_file`), system DLLs with
    `LOAD_LIBRARY_SEARCH_SYSTEM32`;
  - URLs open through `explorer.exe` (`app_open_url`) so the browser is not elevated.
- Autostart is a Task Scheduler logon task registered from XML
  (`autostart_set`): no "AC power only", no 72 h limit. Its state is queried on
  the controller thread; the UI only reads the cache and posts `CMD_AUTOSTART`.
- The UI window allows `TaskbarCreated`/`WM_TRAY` through UIPI
  (`ChangeWindowMessageFilterEx`), otherwise an elevated tray icon disappears
  for good when Explorer restarts. Never `WM_COMMAND`: any unprivileged
  process could then post menu commands (exit, autostart, profile).
- Exit/uninstall call `stop_controller()` (sets `g_quitting`, joins the
  controller) before touching the engine or files.
- The clean resolver on a custom port is diverted with an exact
  `ip.SrcAddr == X and udp.SrcPort == P` clause (`add_dns_answer_filter`); a
  plain port rule would catch mDNS (5353) or QUIC (443).
- Known limits (be honest in docs/UI): IP-level blocks cannot be bypassed by
  DPI tricks; Discord voice (UDP) and Discord's desktop updater are not handled;
  WinDivert's signed driver itself gets generic "WinDivert" detections on
  VirusTotal (see Status), same as GoodbyeDPI/zapret. Payment protection
  relies on the SNI in the first TLS segment or addresses seen in plain DNS
  answers; with browser DoH and a ClientHello split over two segments it may
  not apply (not measured yet, docs/SECURITY-REVIEW.md S6).
- The log is rotated at 1 MB on write (`log_write`), not only at start.

## Status / history

- v1.0.0/v1.0.1: first public release (single-file exe with embedded driver).
- v1.1.0 (rc2 published as pre-release): hybrid DNS, fastest DNS, narrow
  filter, honest latency metric, payment IP learning, no embedded driver,
  first bug-hunting round (21 findings).
- Branch `feature/hardening-autopilot` (not merged): security hardening and
  autopilot work from docs/DEVELOPMENT-ROADMAP.md items 1, 2, 3 (partly), 5,
  16; see docs/ for the analysis.
- After rc2 (on main, not yet tagged): second bug-hunting round - app layer
  (17 findings: folder-security race, laptop autostart, tray icon after
  Explorer restart, manual mode overridden by site test, watchdog balloon
  loop, validated block IPs, absolute System32 paths, non-elevated browser,
  controller join on exit, DPI sizing) and engine (exact clean-DNS filter
  clause instead of a port rule, payment IPs learned from clean-resolver
  answers too, built-in block IPs independent of argument order).
- v1.1.0-rc3 / v1.1.0: engine STATS from a reporter thread, self-test checks
  payment exclusion and engine latency, AV hygiene (no long sleeps, BMP icons,
  symbols kept).
- Verified on the developer's network (Turkish ISP, DNS block page
  195.175.254.2) with `--selftest`: blocked targets 3/3, TCP+TLS ~150 ms,
  fastest clean DNS OpenDNS 42 ms (Yandex 96 ms), payment connections passed
  untouched (excluded=6), engine overhead ~9 ms (247 vs 238 ms).
- VirusTotal: release zip 3/68, all three are WinDivert-driver labels
  (Elastic Rootkit.WinDivert, Kaspersky not-a-virus RiskTool, Lionic
  Riskware) that every WinDivert-based tool gets. The v1.0 single-file exe was
  3/55 (embedded driver), rc2's exe 3/71 (ML "dropper"); the rc3 changes
  target those heuristics. Remaining options are code signing and
  false-positive reports to the vendors (both need the owner).
