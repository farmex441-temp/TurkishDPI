# TurkishDPI – project guide

TurkishDPI is a small native Windows program (C, Win32, no frameworks) that gets
around DPI/DNS-based website blocking in Turkey without a VPN: an automatic,
self-learning controller with its own UI, and (since 2.0) its own packet engine
whose IPv4 path and helpers come from **Shroudly**'s core (umutxyp, MIT; kept
attributed in file headers, `NOTICE`, `THIRD_PARTY_LICENSES.txt`). License:
Apache-2.0. 1.x used a GoodbyeDPI-derived engine; the owner asked for it to be
removed entirely (trust), so no GoodbyeDPI code or GoodbyeDPI-style command
line remains - do not reintroduce it.

- Repo: https://github.com/farmex441-temp/TurkishDPI (public, account `farmex441-temp`)
- User-facing docs are Turkish (`README.md`, `docs/BENIOKU.txt`, UI strings).
  Code, comments and commit messages are English.

## How it works (one exe, two roles)

`TurkishDPI.exe` (requires admin, `requireAdministrator` manifest):

1. **App** (no args or `--tray`): tray icon + main window. A controller thread
   picks a strategy preset, starts the engine as a child process, probes test
   sites, learns per network, watches the connection and re-tunes.
2. **Engine** (`--engine ARGS`, options in `engine_main`, `--engine --help`):
   the packet loop on WinDivert (src/engine/engine.c). The app starts it
   hidden, reads its stdout through a pipe and kills it via a job object if
   the app dies.

Other modes: `--selftest` (scan, verify payment pass-through, write `SELFTEST …`
lines to the log, exit; exits 3 if another instance runs), `--help`,
`--ui-preview <scenario> [--scale F] [--page status|details|settings]
[--focus N] [--shot out.bmp]` (developer tool, not in `--help`: the window with
mock `g_ui` data, no data folder/engine/network/tray, every action gated in
`do_click`/`command`; own window class so the real app never finds it; runs
without admin via `__COMPAT_LAYER=RunAsInvoker`; scenarios in `preview_fill`).

Engine -> app protocol (stdout lines): `TurkishDPI engine <ver>` and one
settings line, `Filter activated`, `Error opening filter: <code>`,
`STATS packets=… https=… http=… excluded=… dns=…` (STATS only when
`engine_report_stats` is set, i.e. in `--engine` mode; every 2 s if changed).

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
                    readers); painted text is mirrored into invisible STATIC
                    controls (`g_a11y`, empty window region, `a11y_update`) so
                    Narrator/NVDA read status, details and site results;
                    palette_t colours; tray icon (NIM_ADD retried on a timer
                    while Explorer is not ready), --ui-preview; never blocks on
                    the controller
  smart.c           per-network learning: network id = gateway MAC, stats per
                    preset (trials, ok, avg ms) in [net_<id>] of the ini
  probe.c/.h        WinHTTP HTTPS probes (TCP+TLS set-up time via status
                    callback), DNS RTT probes, forged-answer check, block-page
                    IP learning via DnsQuery_A, facts for site diagnosis
  diag.c/.h         site diagnosis verdict (ok / no_dns / dns_poison /
                    tls_block / ip_block); pure C, no Win32, unit tested
  sysdll.c/.h       winhttp.dll + iphlpapi.dll resolved at run time from
                    System32 (not linked), SetDefaultDllDirectories;
                    `sysdll_winhttp_tls` turns on TLS 1.2 on Windows 7/8.0
  system.c/.h       rival tools, broken system proxy repair, GitHub update check,
                    admin-only data folder (sys_secure_dir), protected
                    Program Files copy for autostart (sys_install_copy), uninstall
  presets.h         strategy presets ({DNS} placeholder) and clean DNS servers
  version.h         version + GitHub URLs
  TurkishDPI.rc, .exe.manifest, res/*.ico   resources (icons from tools/make_icons.py)
src/engine/         the packet engine
  engine.c/.h       command line (engine_main), WinDivert filter (build_filter),
                    packet loop: IPv4 decoy/split/seqovl/tlsrec/RST-drop/autottl/
                    QUIC/Discord-voice adapted from Shroudly raw_windows.c; own
                    IPv6 split+decoy (build6/desync6, no extension headers),
                    DNS redirection, payment exclusion, STATS reporter
  core/             from Shroudly core/src (MIT, unchanged except a header
                    line): proto (TLS/HTTP parse, decoy names), tcpdpi (segment
                    builders, hop table, tlsrec seq translation), udpdpi (QUIC
                    Initial, Discord voice), rawscope (local addresses, touched
                    servers for RST drop, no-decoy table), raw_desync.h (config)
  dns.c/.h          own DNS question table + hybrid race verdicts + forged-answer
                    check (block IPs, bogons, error rcodes, malformed = forged)
  exclude.c/.h      payment/bank "never touch" list (SNI + IPs learned from DNS)
  windivert_dyn.c   loads WinDivert.dll at runtime from the exe's folder only;
                    ARM64 Windows is refused with error 50 (no WinDivert driver)
  syslib.h          load_system_dll / load_dll_path: System32-only loading
                    that also works on Windows 7 without KB2533623
tests/              unit tests (no admin): core/test_{proto,tcpdpi,udpdpi,rawscope}.c
                    (Shroudly's, strategy parts removed), exclude_test.c, dns_test.c,
                    diag_test.c, windivert_hash_test.c (DLL + driver pinning, uses a
                    stand-in DLL, never the driver); manual tools:
                    probe_test.c (network/latency/diagnosis), hosts_test.c
                    (DNS answer + TCP+TLS per host, e.g. Discord endpoints),
                    uia_probe.c (native UI Automation dump/invoke/tray rect -
                    what a screen reader sees)
third_party/WinDivert/include/windivert.h   header only; DLL/SYS are not in git
tools/make_icons.py generates src/app/res/*.ico and assets/logo.png (Pillow):
                    flat shield; tray shields coloured by state (ok, busy,
                    idle, warn "!", error "x"); uncompressed BMP frames <=128 px
tools/ui_shots.ps1  --ui-preview screenshots (scenario x page x scale) ->
                    docs/design/screenshots/ (tools/bmp_to_png.py converts)
tools/uia_e2e.ps1   elevated end-to-end check of the real app: --selftest,
                    UIA tree per page, autostart toggle on/off + task check,
                    tray icon, exit through the window (needs uia_probe.exe)
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
- **Narrow filter** (`build_filter`): outbound only HTTP requests (port 80 with
  payload) and TLS records 22/21 on 443 (ClientHello, client alerts); with
  tlsrec every IPv4 443 segment (its sequence numbers move); QUIC Initials;
  DNS port 53 out, answers from 53 (hybrid/learning) and from the clean
  resolver's exact IP:port; inbound RST / SYN-ACK / TLS alerts from 80/443.
  Do not widen this without a reason: every diverted packet costs two
  context switches.
- **Presets** (presets.h, measured on Türk Telekom 2026-11-08): the decoy must
  pass the filter (hop-distance TTL, `--autottl 1`; fixed 3-5 dies before it)
  and real data must not follow the decoy in order (`fake`+in-order split adds
  ~300 ms: the filter drops the real first segment, Windows resends after its
  300 ms minimum RTO). `fakeddisorder` costs nothing, so it is preset 0.
  Learned stats use keys `e<N>` in `[net_<id>]` (the 1.x `p<N>` keys belong to
  the old presets and are ignored); reordering presets invalidates them.
- **Payment protection** (default on, `--no-payment-exclude` / ini
  `PaymentProtection=0` to disable): ~100 bank/payment/3-D Secure domains are
  never modified, matched by SNI/Host and by addresses learned from DNS answers.
- **QUIC** Initials are dropped (engine default, both families) so browsers fall back to TCP.
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
x86_64-w64-mingw32-gcc -g -O1 -fsanitize=undefined,address -Isrc/engine tests/dns_test.c src/engine/dns.c -lws2_32 -o build/dns_test.exe
x86_64-w64-mingw32-gcc -g -O1 -fsanitize=undefined,address -Isrc/engine tests/exclude_test.c src/engine/exclude.c -lws2_32 -o build/exclude_test.exe
for t in proto tcpdpi udpdpi rawscope; do x86_64-w64-mingw32-gcc -g -O1 -fsanitize=undefined,address -Isrc/engine/core tests/core/test_$t.c src/engine/core/{proto,tcpdpi,udpdpi,rawscope}.c -lws2_32 -o build/test_$t.exe && ./build/test_$t.exe; done
```

Without MinGW, MSVC (`cl /fsanitize=address`, add `/Dssize_t=intptr_t`) builds
and runs the unit tests; the release exe is built with MinGW (CI).

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
- Elevated code must not trust user-writable paths. Threat model: unprivileged
  code of the *same* user (same SID - "owned by the current user" is never
  trusted), other local users, the network/ISP. Rules:
  - trusted owners are only BUILTIN\Administrators and SYSTEM
    (`owner_is_trusted`); `sys_set_default_owner()` makes everything the app
    creates Administrators-owned regardless of local policy;
  - `sys_secure_dir(dir, users_read)`: handle opened with
    `FILE_FLAG_OPEN_REPARSE_POINT`; a folder someone else created first is
    moved aside (`.untrusted-<tick>`) and recreated; owner + protected DACL
    (SY/BA full, OW read-only, BU read only for the Program Files copy - the
    data folder is admin-only because it holds network history); entries not
    owned by BA/SY or that are links are removed through handles; if anything
    cannot be verified/removed it returns 0 and the app refuses to start;
  - deletions go through `delete_object` / `delete_tree_in`: open without
    following links, check the real location with `GetFinalPathNameByHandle`
    stays inside the root, delete by handle (FileDispositionInfo). Delete-at-
    reboot only in admin-only folders, after renaming the file aside;
  - the exe's own folder is NOT trusted (zips get extracted to Downloads):
    the logon task always runs the protected copy in `%ProgramFiles%\TurkishDPI`
    (`sys_install_copy`; `autostart_migrate` re-registers on every start when a
    task exists); a failed copy deletes the task. Copy rules: only pinned
    drivers, hash checked on the very handle that is copied
    (`windivert_handle_ok`), single-link plain files only, the driver this OS
    needs must be present. The exe comes from `g_self_exe` (`lock_self_exe`:
    opened without delete sharing, path not truncated, same file as the mapped
    image - otherwise the app refuses to start);
  - WinDivert.dll **and the driver this system uses** only from the exe's own
    folder, and only if their SHA-256 match the build-time pins
    (`WINDIVERT_DLL_SHA256`, `WINDIVERT_SYS64_SHA256`, `WINDIVERT_SYS32_SHA256`;
    Makefile computes them from the checksum-pinned archive and tracks them in
    `obj/wdpin.stamp`; CI sets `REQUIRE_WINDIVERT_HASH=1`). Checked on handles
    that deny writes/deletes (DLL until mapped, driver for the whole engine
    process); a pinned driver must exist (error 2), mismatch 1006, locked or
    quarantined 1920;
  - no static imports of non-KnownDLLs (winhttp, iphlpapi, dnsapi, bcrypt,
    gdiplus - the last is not a KnownDLL on Windows 7/8): `sysdll.c`,
    `gdiplus_load()` in ui.c, `load_system_dll()` from syslib.h (never a bare
    `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)`: unpatched Windows 7
    rejects the flag with error 87); CI's "Check imports" step fails the
    build otherwise;
  - never `ShellExecute` from the elevated process (it resolves file types via
    the user-writable HKCU\Software\Classes): `launch()` = `CreateProcessW`
    with an absolute path (`explorer.exe` for URLs so the browser is not
    elevated, System32 `notepad.exe` for log/ini, `run_hidden` for schtasks);
  - single instance: mutex `TurkishDPI\app` in a private namespace bounded by
    the Administrators SID (`instance_mutex`), so it cannot be squatted;
  - window messages: only `TaskbarCreated` and `WM_TRAY` pass UIPI; `WM_TRAY`
    is acted on only when the cursor is on our icon (`tray_click_genuine`) and
    never re-entered while a menu is open; never allow `WM_COMMAND`;
  - logon task XML: `<UserId>` is DOMAIN\user resolved from the token user's
    SID (`LookupAccountSidW`, not environment variables); the XML is written
    to the admin-only data folder and **closed before schtasks runs** -
    schtasks opens it without FILE_SHARE_WRITE, so any handle of ours with
    write access makes /Create fail with exit code 1 (the v1.2.0 bug); the
    file is deleted right after;
  - DNS learning for payment exclusion trusts only answers that match a
    tracked query (txid, port, the server it went to) or come from the clean
    resolver (`dns_trusted`); malformed answers count as forged.
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
  DPI tricks; no Windows service mode (logon task instead - a service would
  need an IPC channel between session 0 and the UI, new attack surface);
  Windows only (WinDivert), no ARM64 (no WinDivert ARM64 driver). Discord was
  measured on 2026-11-08: web, gateway, CDN and updater (updates.discord.com)
  are DNS/SNI-blocked and open with the engine; voice (*.discord.media, UDP)
  is not blocked in Turkey and connects;
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
- v1.2.0: merge of feature/hardening-autopilot + feature/ui-redesign, then a
  /code-review round (driver pinning, TCP-first diagnosis), a bug hunt
  (10 findings) and a full security audit (14 findings: same-user attacker
  model for the data folder, handle-based deletes, mandatory pinned driver,
  self-exe lock check, no ShellExecute, private-namespace mutex, WM_TRAY
  guard, trusted-only DNS learning, gdiplus from System32, SID task user,
  persist-credentials: false in CI).
- v1.2.1: "start with Windows" fixed (the XML was still open for writing
  while schtasks read it -> exit code 1 on every toggle); screen-reader text
  for the painted pages and a name for the site box (checked with native UIA
  via tests/uia_probe.c); tray icon retried until Explorer accepts it;
  Windows 7 without KB2533623 (syslib.h) and TLS 1.2 for WinHTTP on 7/8.0;
  clear ARM64 message. Verified elevated with tools/uia_e2e.ps1: selftest
  PASS, task created/removed, tray icon on the taskbar, clean exit.
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
  target those heuristics. v1.2.0 (2026-11-08): x64 zip 3/68 (the same three
  WinDivert labels), `TurkishDPI.exe` alone 1/71 (Elastic ML "Malicious (high
  Confidence)"; Microsoft, Kaspersky, ESET, BitDefender etc. clean), local
  Defender scan clean. v1.2.1: x64 zip 3/68 (same WinDivert labels), exe
  alone 2/71: Elastic ML plus Microsoft cloud ML `Trojan:Win32/Wacatac.B!ml`
  (a generic label for new unsigned binaries; local Defender without cloud
  protection is clean). It blocks the download in Edge and Chrome (Chrome
  hands downloads to Defender; Google Safe Browsing itself reports the URL
  clean). False-positive report sent 2026-11-08, submission ID
  89a9c5f0-2095-46ea-b5b0-f9f4a55ae401 (status at https://www.microsoft.com/en-us/wdsi/submission/89a9c5f0-2095-46ea-b5b0-f9f4a55ae401;
  cloud label then `Program:Win32/Wacapew.A!ml`, determination pending). Remaining options are code signing and false-positive
  reports to the vendors (both need the owner); do not try to dodge ML
  detectors with packing/obfuscation.
