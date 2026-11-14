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
                    Program Files copy for autostart (sys_install_copy, never
                    replaces a newer copy), uninstall, one-click update
                    (sys_update_install = upd_fetch + upd_install_files,
                    http_get with a total deadline, sys_update_cleanup)
  updpkg.c/.h       update package reader: SHA256SUMS.txt lookup, zip (stored
                    + own inflate, CRC-32), tag parse, PE machine and PE file
                    version; pure C, unit tested
  presets.h         strategy presets ({DNS} placeholder) and clean DNS servers
  version.h         version + GitHub URLs
  TurkishDPI.rc, .exe.manifest, res/*.ico   resources (icons from tools/make_icons.py)
src/engine/         the packet engine
  engine.c/.h       command line (engine_main), packet loop, sending what
                    desync.c composed, RST-drop/autottl/QUIC/Discord-voice
                    adapted from Shroudly raw_windows.c, DNS redirection,
                    payment exclusion, STATS reporter
  desync.c/.h       pure: composes the finished packets for one first request
                    (decoy, cut, fakedsplit/fakeddisorder, seqovl, tlsrec,
                    IPv4 and IPv6 without extension headers); a captured
                    packet up to 1500 bytes gives exactly the 2.0.1 output, a
                    larger one is sent in pieces of <= 1200 payload bytes
  policy.c/.h       pure: the WinDivert filter text (policy_build_filter, with
                    a fit check) and the payment-exclusion precedence
                    (policy_excluded, ECH public names)
  core/             from Shroudly core/src (MIT): proto (TLS/HTTP parse, decoy
                    names), tcpdpi (segment builders, hop table, tlsrec seq
                    translation), udpdpi (QUIC Initial, Discord voice), rawscope
                    (local addresses, touched servers for RST drop, no-decoy
                    table), raw_desync.h (config). Unchanged except a header
                    line, apart from tcpdpi.c/.h: TurkishDPI added
                    `tcpdpi_xlate_fin` and the `fins`/`dead_at` fields (retire a
                    tlsrec translation entry after both FINs) and a high-water
                    mark so the per-packet lookup scans only used slots (marked
                    `TurkishDPI:` in the source)
  dns.c/.h          own DNS question table + hybrid race verdicts + forged-answer
                    check (block IPs, bogons, error rcodes, malformed = forged;
                    LAN addresses allowed from a local resolver)
  exclude.c/.h      payment/bank "never touch" list (names + IPs learned from
                    DNS, expiring table, list-file loader)
  windivert_dyn.c   loads WinDivert.dll at runtime from the exe's folder only;
                    ARM64 Windows is refused with error 50 (no WinDivert driver)
  syslib.h          load_system_dll / load_dll_path: System32-only loading
                    that also works on Windows 7 without KB2533623
tests/              unit tests (no admin), listed in .github/scripts/build-tests.sh
                    (13): core/test_{proto,tcpdpi,udpdpi,rawscope}.c (Shroudly's,
                    strategy parts removed), exclude_test.c, dns_test.c,
                    diag_test.c, updpkg_test.c (+ updpkg_vectors.h from Python's
                    zlib; `updpkg_test pkg.zip name file` checks a real zip;
                    `--pe-version exe X.Y.Z` is what CI runs on the release
                    exes), xlate_test.c, upd_swap_test.c (the updater's install
                    half against real files), pieces_test.c, policy_test.c,
                    desync_test.c. Not in the list: windivert_hash_test.c (DLL +
                    driver pinning, stand-in DLL, never the driver; run by its own
                    CI step); manual tools:
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
.github/workflows/build.yml   CI: tests (native Windows gcc, MSVC ASan for dns +
                    exclude, Ubuntu ASan/UBSan), build x64/x86 on Ubuntu MinGW
                    (tag guard, import allowlist, package zips), "smoke" job
                    (runs the cross-built tests and the shipped exes), release
                    job on v* tags (draft, verify, publish)
.github/scripts/    build-tests.sh (the one list of unit tests, used by every
                    job), smoke.ps1 (zip checks + --ui-preview scenarios)
```

Runtime data: `%ProgramData%\TurkishDPI\` (admin-owned, protected DACL):
`TurkishDPI.ini` (settings + learned data), `TurkishDPI.log` (rotated at 1 MB).

## Key behaviours (what was built and why)

- **Auto scan** (`auto_scan`): no internet -> wait; measure all clean DNS servers
  in parallel and keep the fastest; with hybrid DNS on, learn the ISP's
  block-page IPs (engine must be stopped; `bt=` in `[net_<id>]` holds the
  time: a result is reused for 24 h per network, an empty result is retried
  after 1 h - `block_ips_stale`/`learn_block_ips_if_stale`); try
  presets in learned order; after the first preset that opens every target,
  try up to two more presets that were not known on this network (`extra`
  counts every preset whose engine started after the winner, working or not;
  a non-fatal start failure is skipped before it counts) and keep the
  fastest (a later preset replaces the winner only if it is more than 20%
  faster); the walk stops at a preset already known on this network, and on a
  network with more than one learned preset a known preset that works ends it
  at once. A driver error from the engine (any code except 87, a filter
  syntax error) is fatal after one attempt: `engine_fatal` shows the window
  text from `windivert_error_text` (unknown codes get a generic text that
  contains the code), tries no other preset and records no failure for any.
  The running engine stays on during the
  measurements and `engine_start` keeps it when the arguments are identical,
  so a re-check that ends on the same preset drops no connection.
  Watchdog every `WatchdogMinutes` (1-240) with a second strike: a failed
  check is repeated after 20 s, only a repeated failure rescans. Network
  changes are debounced (4 s quiet, 20 s max). Partial mode only rescans if
  it gets worse. Only every 6th successful check counts in the learning
  stats (`health_record`; one counter shared by the watchdog and the
  post-network-change check `health_ok(1)` in the controller loop), i.e.
  hourly only at the default 10 minutes with no network changes. A confirmed
  watchdog failure always counts (outside partial mode); the post-network-
  change check has no second strike, so a single failure there counts at once.
- **One-click update** (2.0.1): the update thread finds a newer
  `releases/latest` tag (digits and dots only); the banner's "Güncelle →"
  posts `CMD_UPDATE`; the controller (`update_install` -> `sys_update_install`:
  `upd_fetch`, then `upd_install_files`) downloads `SHA256SUMS.txt` (30 s total)
  and `TurkishDPI-<tag>-<x64|x86>.zip` (3 min total) from github.com with
  `http_get`, which follows redirects itself (each target must be https on
  github.com or *.githubusercontent.com, at most 3) and bounds every blocking
  WinHTTP call by the deadline; failures carry a short reason (`why`, logged as
  `update: failed (...)`). It checks the zip's SHA-256, extracts the exe (+
  licence/read-me texts) and requires the exe's PE file version to equal the tag
  and be newer than the running build (`upd_pe_file_version`, CI checks the
  release exes with the same parser). Every WinDivert file present next to the
  exe must be in the zip with identical bytes, otherwise -1 (opens the release
  page). In the exe's own folder (often user-writable):
  new files are created CREATE_NEW without following links, checked inside the
  folder, and renamed relative to the folder handle with
  `NtSetInformationFile` (SetFileInformationByHandle rejects a bare name with
  error 123); the running exe (lock handle closed first) is renamed to
  `<name>.<tick>.old`, the new one put in place, re-opened read-only without
  delete sharing and compared byte by byte. The licence/read-me texts are
  replaced only after the exe is in place. On failure the running exe is
  renamed back and re-locked (our DELETE handle must be closed first) and the
  banner says "Güncelleme yapılamadı"; only when it cannot be put back or locked
  again (-2) the app shows "Güncelleme tamamlanamadı…", opens the releases page
  and exits (the file at the exe's name is not trusted). On success the UI thread
  (`app_update_done`) stops controller + engine, releases the instance mutex,
  starts the new exe suspended, resumes it only if its image is the locked
  file (volume + file ID), and exits. The next start deletes after 20 s
  (`sys_update_cleanup`) only `<name>.<digits>.old` and `<known file>.upd-new`.
  Not code-signed: protects against broken/altered downloads, not against a
  compromised GitHub account (say so in docs).
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
  Exception (2.0.2): an answer from the resolver the question went to may carry
  RFC 1918 / 169.254 addresses when that resolver is itself local (RFC 1918,
  169.254, loopback, IPv6 ULA/link-local; not carrier CGNAT 100.64/10), so
  router/NAS/intranet names resolve; 0/8, loopback, multicast, block-list
  addresses and error rcodes stay forged for every resolver. Residual risk: a
  home router forwards its ISP's answers, so a censor answering with a private
  address passes unless that address is on the block list.
- **Narrow filter** (`build_filter`): outbound every port-80 segment that
  carries payload (not only requests: the parser decides) and TLS records
  22/21 on 443 (ClientHello, client alerts); with tlsrec every outbound IPv4
  443 segment (its sequence numbers move) and, inbound, every IPv4 segment
  from 443 (their ACKs are translated); QUIC Initials; DNS port 53 out,
  answers from 53 (hybrid/learning) and from the clean resolver's exact
  IP:port; inbound RST / SYN-ACK / TLS alerts from 80/443.
  Do not widen this without a reason: every diverted packet costs two
  context switches.
- **Presets** (presets.h, measured on Türk Telekom 2026-11-08, 5 runs x 4
  sites per technique): cutting the first request after byte 2
  (`--split-pos 2`) and sending the pieces in reverse order opens every
  blocked site every time at no extra latency (~150 ms) - preset 0, no decoy.
  Cutting inside the site name (Shroudly's default) was unreliable (roblox
  1/5); a hop-distance-TTL decoy followed by real data costs ~300 ms (the
  filter drops the real first segment, Windows resends after its 300 ms
  minimum RTO); a cut after byte 1 fails; fixed-TTL 5 decoys with the
  byte-2 cut work. Learned stats use keys `e<N>` in `[net_<id>]` (1.x `p<N>`
  keys are ignored); reordering presets invalidates them.
  Large first segments (2.0.2): when segmentation offload hands the engine one
  segment over 1500 bytes (e.g. a ~1.8 KB ClientHello), it is sent in pieces of
  <= 1200 payload bytes (a decoy carries the first 1200); packets up to 1500
  bytes are byte-identical to 2.0.1 (`desync_test`, `pieces_test`). The engine
  prints `Large first segment: ...` (it ends up in the log). A TLS ClientHello
  whose name is not in the segment gets the byte-2 cut instead of no cut
  (`TCPDPI_FALLBACK_CUT`), also for the name-based and tlsrec presets.
- **Payment protection** (default on, `--no-payment-exclude` / ini
  `PaymentProtection=0` to disable): ~100 bank/payment/3-D Secure domains are
  never modified. `policy_excluded` (policy.c): the visible SNI/Host name decides
  if there is one; an address learned from DNS (`exclude_check_ip`) counts only
  when the segment carries no name or the name is a known ECH public name
  (`cloudflare-ech.com`) - before 2.0.2 a blocked site sharing a CDN address with
  a payment site was left alone for up to 2 hours. Learned addresses expire after
  2 h, at most 16 per DNS answer, a full table replaces the entry that expires
  soonest; the built-in names always fit. `--exclude` files load after the
  built-in list (LF/CRLF/CR, UTF-8 BOM, `#` comments; NUL or over-long lines are
  skipped).
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

Unit tests: the one list of tests lives in `.github/scripts/build-tests.sh`
(13 tests; `<exe>|<sources>|<flags>|<libs>` per line, `-Wall -Wextra -Werror`,
the same script for the native, cross (-static, x64/x86) and sanitizer builds):

```bash
bash .github/scripts/build-tests.sh gcc build/tests          # native
bash .github/scripts/build-tests.sh x86_64-w64-mingw32-gcc build/t64 -static
# sanitizers (Linux or MinGW with ASan; tests that include Windows headers need
# TEST_ONLY=...): -g -O1 -fsanitize=address,undefined
```

Run them under sanitizers when touching engine parsing code. dns and exclude
need winsock: CI runs them under MSVC ASan (`cl /fsanitize=address`).

Without MinGW, MSVC (`cl /fsanitize=address`, add `/Dssize_t=intptr_t`) can build
and run every unit test (desync_test, pieces_test, policy_test and upd_swap_test print
their MSVC command line in the header comment) - good for a check, the release exe is built with MinGW (CI). Release
exes link with `-Wl,-S` (drops only the DWARF debug info that the static MinGW
libraries bring, so the exe is smaller); never `-s`: the symbol table stays on purpose (AV hygiene, see
the v1.1.0-rc3 and VirusTotal notes in Status).

C runtime: a local llvm-mingw build links ucrt, the CI release build uses
Ubuntu's `gcc-mingw-w64` and links msvcrt (the Makefile does not choose; the
toolchain does). The same source must work with both - see Conventions. To use a
WinDivert other than the pinned official one: `make WINDIVERT=<folder>` (layout
of the official archive: `include/`, `x64/`, `x86/`); the Makefile computes the
DLL/driver SHA-256 pins from that folder.

Quick checks without admin:
- `__COMPAT_LAYER=RunAsInvoker TurkishDPI.exe --engine <args>`: "Error opening
  filter: 5" means the filter compiled (only the driver needs admin); 87 means
  a filter syntax error.
- `tests/probe_test.c`: DNS RTTs, learned block IPs, per-site TCP+TLS times.

Real end-to-end test (admin, UAC prompt; close any running instance first):
`TurkishDPI.exe --selftest`, then read `SELFTEST` lines in the log.

## Releasing

Bump `src/app/version.h` (string macros AND the three numeric
`TURKISHDPI_VER_*` macros: the updater compares the numbers, the exe's file
version comes from them), commit, push `main`, push a tag `vX.Y.Z` (tags with a
`-`, e.g. `v1.1.0-rc2`, become pre-releases). CI refuses a tag that differs from
version.h (a `-suffix` of the tag is ignored for the comparison), builds, runs
the tests and the shipped exes ("smoke"), and the release job creates the
GitHub Release as a DRAFT with `TurkishDPI-<tag>-x64.zip`, `-x86.zip`,
`SHA256SUMS.txt`, verifies exactly those three assets (names, sizes, digests,
sums) and only then publishes it (`make_latest` for normal tags, never for
pre-releases): the one-click updater cannot see a half-uploaded release. A
release that is already published is never touched: the job refuses; "Re-run
jobs" or `workflow_dispatch` on a tag does not run the release job. Never
delete or re-push a tag; publish a new version instead. WinDivert is pinned to
2.2.2-A by SHA-256 in the workflow. Text files in the zip get CRLF line endings,
BENIOKU.txt a UTF-8 BOM (done in the Package step; the repo files stay LF).

Local builds use clang (llvm-mingw) while CI uses GCC and MSVC, so GCC-only
warnings (e.g. `-Wmisleading-indentation` under `-Werror`) and MSVC's default C
mode (`/std:c17` is needed for C11) only show up in CI: push the branch and let
CI run before trusting "warning-free".

## Conventions and gotchas

- Code must compile with both msvcrt (CI GCC) and ucrt (clang): no `wcstok`
  with 3 args, use `_snwprintf` + explicit termination for bounded wide output,
  `%ls` for wide strings, `%lu` for DWORD.
- Test executables must not have "update", "install", "setup" or "patch" in
  their name: an unmanifested 32-bit exe with such a name is treated as an
  installer and demands elevation (hence `upd_swap_test`). build-tests.sh says
  so too.
- Engine stdout lines (all but `STATS`) are copied into the log by
  `engine_reader`: `Error opening filter: N`, `Large first segment: ...`.
- Pure engine logic (desync.c, policy.c, parts of dns.c/exclude.c) stays free of
  WinDivert/Win32 so it can be unit tested; keep new decisions there, not in
  engine.c's loop.
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
  relies on the SNI in the first TLS segment (or, without a name / with an ECH
  public name, on addresses seen in plain DNS answers); with browser DoH and a ClientHello split over two segments it may
  not apply (not measured yet, docs/SECURITY-REVIEW.md S6).
- The log is rotated at 1 MB on write (`log_write`), not only at start.

## Status / history

- v1.0.0/v1.0.1: first public release (single-file exe with embedded driver).
- v1.1.0 (rc2 published as pre-release): hybrid DNS, fastest DNS, narrow
  filter, honest latency metric, payment IP learning, no embedded driver,
  first bug-hunting round (21 findings).
- Branch `feature/hardening-autopilot` (merged into main, first released in
  v1.2.0): security hardening and autopilot work from
  docs/DEVELOPMENT-ROADMAP.md items 1, 2, 3 (partly), 5, 16. docs/ARCHITECTURE,
  PROJECT-ANALYSIS, SECURITY-REVIEW, DEVELOPMENT-ROADMAP and docs/design/UI-AUDIT,
  PREVIEW-PLAN are historical v1.1.0 snapshots (each says so at the top).
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
- v2.0.0: own packet engine - GoodbyeDPI code removed; Shroudly core (MIT)
  for the IPv4 path, own DNS/IPv6/payment/CLI; an independent review found
  8 issues (clean NXDOMAIN dropped in the hybrid race, tlsrec close/reuse,
  option-parse crash, ...), all fixed; `--split-pos 2` reverse cut measured
  20/20 at ~150 ms on Türk Telekom and made preset 0. Verified elevated:
  selftest x64/x86 PASS (latency +6/-1 ms), 3 consecutive scans on preset 0,
  Discord endpoints, NXDOMAIN fast, tlsrec downloads close cleanly, UI e2e.
- v2.0.1: one-click update (see Key behaviours), exe version info no longer
  names GoodbyeDPI. Verified elevated with a build labelled 1.9.9 against the
  real v2.0.0 release: x64 and x86 replaced themselves and restarted as the
  release exe (byte-identical), Program Files copy not downgraded, a forced
  failure (exe held open without delete sharing) leaves the app running with
  the "failed" banner and the exe unchanged, 2.0.1 removes the `.old` file.
  The parser extracts every file of the real v2.0.0 zips (Info-ZIP deflate),
  120 Python-made zips and a .NET-made zip identically. (Windows PowerShell
  5.1's Compress-Archive writes backslashes in names; those zips are refused
  by design - releases are made by Info-ZIP in CI.) Released 2026-11-08;
  then a 1.9.9 build updated itself to the published v2.0.1 (byte-identical
  release exe, `.old` removed, Program Files copy refreshed to v2.0.1).
  v2.0.0 users must download 2.0.1 by hand once (no updater in 2.0.0).
  Elevated test scripts must stop the app before its engine: killing the
  engine first makes the app start a new one that is orphaned (suspended,
  outside the job) when the app is killed right after.
- v2.0.2 (this branch `release/v2.0.2`; merge of seven work packages, each
  reviewed independently by a second reviewer before merging): engine
  correctness (large first segments, nameless ClientHello cut, payment
  precedence with ECH public names, never-valid decoy checksum, `--exclude`
  after the built-in list), hybrid DNS accepting LAN addresses from local
  resolvers, payment list loader/table hardening, controller/UI fixes (second
  start raises the window, pause survives settings, one exit path for
  interrupted scans and site tests, driver errors fatal after one attempt,
  idempotent pause/resume, windows hidden first on exit/uninstall/update),
  updater hardening (exe version must equal the tag and be newer, total
  download deadlines, own redirect handling, specific failure reasons, WinDivert
  files must match, texts replaced after the exe, exact leftover cleanup),
  build/CI (smaller exe, draft-then-publish release, tag guard, smoke job,
  one list of unit tests) and these docs.
  Verification: unit tests on x64/x86 and under ASan pass (13 tests).
  Elevated, 2026-11-14, developer's machine (Windows 11, Türk Telekom line,
  Ethernet), local llvm-mingw builds of the merged branch (the release exe is
  built by CI):
  - `--selftest` x64: PASS, blocked targets 3/3, average 164 ms, payment
    pass-through excluded=3, latency with/without engine -1 ms. x86: PASS, 3/3,
    109 ms, excluded=3, +2 ms.
  - Engine alone (preset 0) with Chromium 156 opening discord.com, roblox.com,
    wattpad.com, wikipedia.org, github.com: all opened; 109 ClientHellos seen;
    the largest first-segment payload was 1412 bytes (STATS maxpay). So no
    segment larger than one packet reached the engine: the large-segment path
    was NOT exercised on the real line; it is covered by unit tests and an
    independent 700,000-case invariant check only.
  - App: paused, then payment protection switched on -> "settings changed while
    paused: applied on resume", stayed paused, next start used the new setting;
    a second start brought the running window forward (from the tray and from
    minimized); a `--tray` second start stayed silent; exit hid the window first
    ("exit: window hidden, stopping"); a copy without WinDivert64.sys made one
    engine start, logged "driver error 2: stopping, no preset is tried or
    recorded", showed the text naming WinDivert64.sys and left the learned
    stats in the ini unchanged.
  - One-click update: a build of this branch labelled 1.9.9 updated itself to
    the published v2.0.1 in about 2 s through the new download code (manual
    redirect, version check): exe byte-identical to the release exe, licence and
    read-me texts installed, the moved-aside exe removed by the next start.
  - Reviews: every package was reviewed by a second engineer-agent; the engine's
    output for packets up to 1500 bytes was compared byte for byte with the
    released engine in 376,236 cases (no difference except the intended cut for
    ClientHellos without a usable name); the PE version reader agreed with
    Windows on 7,603 real binaries; the new download code fetched the real
    release files with a matching SHA-256.
  - NOT verified on a real system: double-click on pause/resume (code review
    only), the site-check interruption paths, unknown driver error codes, update
    failure/recovery paths (covered without elevation by tests/upd_swap_test.c),
    the download deadline on a throttled line, IPv6, other ISPs, Windows 7/8.1.
  - Known issue, not fixed: starting a copy that lacks the driver file while
    "Windows açılışında başlat" is on removes the logon task (`autostart_migrate`
    -> `sys_install_copy` fails -> task deleted by design), although the task
    already pointed at an intact Program Files copy.
  - Released 2026-11-14 (PR 5 merged as e864d33, tag v2.0.2). The first CI run
    of the reworked workflow failed on two toolchain differences that local
    builds could not show (GCC's -Wmisleading-indentation in
    tests/core/test_tcpdpi.c under -Werror; MSVC needing /std:c17 for a C11
    _Static_assert in exclude.c); after the fix all jobs passed, and the tag run
    created the draft, verified the three assets and published it.
  - Release exe built by CI (GNU ld, msvcrt): x64 1,842,861 bytes, x86
    1,834,600 bytes (v2.0.1 x64: 2,159,187); zips 392,039 (x64) and 438,314
    (x86) bytes. About 0.9 MB of the exe is still a zero-filled `.data` section
    (objdump: .data 0xdb980, .bss 0xc40) - GNU ld with -fdata-sections; left for
    a later version.
  - Verified with the PUBLISHED files on the developer's machine, elevated:
    `--selftest` of the release exes PASS for x64 (3/3, 155 ms, latency -10 ms)
    and x86 (3/3, 110 ms, -5 ms); the installed v2.0.1 (Program Files copy
    started by the logon task) updated itself to v2.0.2 with one click in about
    2 s - exe byte-identical to the release exe, texts replaced (BENIOKU.txt now
    with BOM), the moved-aside exe removed, logon task intact, protection active
    afterwards. Assets match SHA256SUMS.txt; WinDivert files identical to the
    pinned ones.
  - Antivirus: local Windows Defender scan (signatures of 2026-11-14, real-time
    protection on) of both zips and both exes: no threats. VirusTotal was NOT
    checked for this release (its page required a reCAPTCHA); no numbers carried
    over from earlier releases.
- Size and memory of the released v2.0.1 (measured 2026-11-14, idle): exe
  2.16 MB, x64 zip about 0.57 MB; app process 16 MB working set / 7 MB private,
  engine process 3 MB / 2 MB (README quotes these; do not quote older figures).
- After rc2 (released in v1.1.0-rc3 and v1.1.0): second bug-hunting round - app layer
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
  cloud label then `Program:Win32/Wacapew.A!ml`, determination pending).
  v2.0.0 (own engine): x64 zip 3/68 (the WinDivert labels), exe alone 1/71
  (Elastic ML only; Microsoft clean), local Defender clean. Remaining options are code signing and false-positive
  reports to the vendors (both need the owner); do not try to dodge ML
  detectors with packing/obfuscation.
