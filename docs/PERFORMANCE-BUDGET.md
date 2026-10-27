# Startup and session resource budget

The priority is a responsive application on a Windows work laptop already under
pressure from endpoint agents. The initial targets are 50 MiB for the application
and approximately 2 MiB of additional application memory for each ordinary live
tab. The rebuilt release does not meet those targets. OpenSSH,
ConPTY hosts, other child programs, and GPU allocations require separate accounting.

## Changes in the first implementation pass

* Settings, session editing, route management, key management, macro management,
  folder connection editing, and About are instantiated on their first request.
  They retain their state after closing. The local/SFTP browser is instantiated
  when first shown and retains per-tab navigation after hiding.
* Terminal font resolution uses Qt's fallback resolver and a bounded shared
  cache. It no longer enumerates every installed font for every new terminal.
  Installing/removing fonts invalidates the resolution cache.
* Scrollback stores variable-length glyph records and runs of shared attributes
  and colors. Unicode combining characters, wide-cell continuations, palette
  indexes, default-color flags, and terminal attributes remain part of each row.
  Screen reflow reconstructs ordinary libvterm cells. The terminal parser is
  unchanged.
* Retained history is limited by both the configured line count and a 1 MiB
  accounting budget. Accounting includes vector capacities, row objects, and
  conservative overhead. Allocator bookkeeping and the deque map are not exact
  process-memory measurements. Oldest rows are evicted first. Session logging
  remains independent of history retention. Settings explains the byte limit.
* Windows output backlog is limited to 256 KiB of queued bytes rather than
  1 MiB. Each GUI callback consumes at most 64 KiB rather than 256 KiB so input
  and other tabs can be serviced between batches. Backpressure preserves output;
  data is not dropped. Input retains its previous 1 MiB limit to preserve paste
  and reviewed-command behavior. Qt array capacity and an in-flight batch are
  additional allocations.
* A saved tab carries its managed `identityKey` through opening, duplication,
  terminal launch, and the active file/monitoring profile. The previous UI path
  dropped that selection and could fall back to the default key.

These changes do not establish a 2 MiB worst-case tab limit. The existing raster
renderer still has a visible-viewport cache of up to 16 MiB, Qt has its own backing
resources, large screen grids cost more, and search/paste operations have transient
allocations. Hidden tabs release the application viewport cache as before. Further
renderer/session separation needs further measurements and terminal regression
evidence before replacing that path.

## Measurement commands

Build in a normal MSVC/Qt development environment following `WINDOWS.md`:

```powershell
./scripts/build-windows.ps1 -QtRoot ./build/tooling/qt-sdk -Jobs 4
ctest --test-dir build/windows-release --output-on-failure
python scripts/measure-performance.py --self-test
python scripts/measure-performance.py --binary build/windows-release/bitshell.exe --fixture build/windows-release/windows_terminal_fixture.exe --output build/performance-idle.json
python scripts/measure-performance.py --binary build/windows-release/bitshell.exe --fixture build/windows-release/windows_terminal_fixture.exe --native-window --renderer default --output build/performance-native-idle.json
python scripts/measure-performance.py --binary build/windows-release/bitshell.exe --fixture build/windows-release/windows_terminal_fixture.exe --native-window --renderer default --workload history --seconds 10 --output build/performance-history.json
```

The script runs fresh processes with 0, 1, 5, and 20 local tabs, three times each,
using disposable configuration beside the result file. The fixture does not
connect to any server. Logging is disabled in that disposable profile. The app
exits after the requested observation period; session job objects clean up its
fixture processes. No personal catalogs, keys, commands, or terminal output are
included in the numeric reports.

The application exposes opt-in `--performance-report PATH`,
`--performance-tabs COUNT`, and `--performance-duration MILLISECONDS` switches.
It records main-entry-to-engine-ready and main-entry-to-first-frame timings, tab
counts, local process counts, window dimensions, and per-terminal history and
viewport-cache accounting. Probe sessions force logging off. The history workload
checks a completion marker in every terminal; a result with output still loading
is rejected. A missing first frame is
reported as -1. Normal startup continues to open one local tab. Zero tabs lets
the probe separate UI/base cost from live-session cost.
Main-entry timings exclude dynamic-loader and endpoint-agent delays before
`main`; they must not be presented as click-to-window timings.

The wrapper records main-process private commit and working set, sampled private
peaks, OS-reported working-set peaks, steady CPU as a percentage of one core, and
the sum of sampled process descendants. Incomplete descendant access is flagged;
process-tree totals are not complete GPU/system accounting. Child working sets
can double-count shared pages. The report identifies the measured executable and
fixture by SHA-256 so an old binary cannot be mistaken for the changed source.

Offscreen/software rendering is the default for repeatability. Also run with
`--native-window --renderer default` on the work laptop to include the actual
display backend. Use the same resolution, DPI, catalog size, logging choices,
history workload, and endpoint-agent configuration for comparisons. Record first
launch after login separately from warm launches; repeated launches are not all
cold starts. Do not use screenshot completion as first-frame timing.

Evaluate the 50 MiB/2 MiB targets against steady private commit and also inspect
working set and startup peaks. Use the difference from the zero-tab case to
estimate additional session cost; shared/lazy allocations make individual-tab
increments non-linear. A live process count is not authentication completion or
an SSH-ready prompt. DNS, TCP, trust/key checks, CyberArk, and MFA latency require
separate real-server measurements. The initial idle CPU target is below 0.5% of
one core after activity settles; history-loading CPU is a separate workload.

## Evidence available on 2026-10-14

The installed toolchain is Visual Studio 2022 Build Tools 17.14.42, MSVC
19.44.35229, CMake 3.31.6-msvc6, Ninja 1.12.1, and the supplied Qt 6.11.2 MSVC
x64 SDK. A fresh Release build completed all 321 build steps. The copied old
build cache was not reused. Previously fetched, pinned dependency sources were
reused without a new network download; the PuTTYgen provenance check passed.

The current local build is `build/windows-performance`. Its private runnable
installation, including Qt DLLs/plugins, is
`build/performance-runtime/bin/bitshell.exe`. The compiler runtime already
installed on this machine was used; compiler runtime DLLs were not bundled.
CMake's compiler check was configured with
`-DCMAKE_TRY_COMPILE_CONFIGURATION=Release` after the default Debug check hit
C1902 in this restricted host. This did not disable application hardening.

All observations below use that rebuilt executable, a 1280x760 window, Windows
fonts, a disposable configuration, logging off, and local sleeping fixture
processes. They ran serially, with no concurrent build or test workload. Native
software figures are medians of three launches per case; other figures are one
launch per case. Settling durations were five seconds for native idle and three
seconds for offscreen idle. The steady sample is the final second of each run.
These are restricted-session observations, not cold-start distributions or
measurements on an endpoint-managed work laptop.

| Display / renderer | Tabs | App private commit | App working set | First frame from main | Idle CPU, one core |
| --- | ---: | ---: | ---: | ---: | ---: |
| Offscreen / software | 0 | 45.59 MiB | 63.26 MiB | 898 ms | 3.25% |
| Offscreen / software | 1 | 54.43 MiB | 72.89 MiB | 442 ms | 1.63% |
| Offscreen / software | 5 | 80.14 MiB | 98.46 MiB | 549 ms | 6.50% |
| Offscreen / software | 20 | 178.23 MiB | 195.59 MiB | 998 ms | 11.34% |
| Native / default | 0 | 139.28 MiB | 155.13 MiB | 1,513 ms | 8.11% |
| Native / default | 1 | 159.23 MiB | 179.09 MiB | 1,518 ms | 11.38% |
| Native / default | 5 | 178.65 MiB | 196.83 MiB | 1,568 ms | 11.37% |
| Native / default | 20 | 243.08 MiB | 260.49 MiB | 2,100 ms | 21.01% |
| Native / software | 0 | 69.44 MiB | 128.23 MiB | 1,099 ms | 4.88% |
| Native / software | 1 | 78.70 MiB | 138.51 MiB | 1,168 ms | 4.88% |
| Native / software | 5 | 104.86 MiB | 163.98 MiB | 1,321 ms | 9.74% |
| Native / software | 20 | 203.25 MiB | 262.04 MiB | 1,918 ms | 12.94% |

The native default backend costs substantially more than offscreen rendering in
this host. Software rendering lowers native private commit here, but these runs
do not establish its throughput or behavior on other graphics hardware. The
application's renderer default was not changed. The native default twenty-tab
case averages about 5.19 MiB of additional private commit per tab above the
zero-tab case; the first-tab shared costs make that average non-linear. The
50 MiB base, 2 MiB tab increment, and 0.5% idle CPU targets all remain unmet.

One native/default history run per case used a ten-second observation period.
Every nonzero case received the full 20,000-line fixture in every terminal.

| Tabs | App private commit | App working set | App + descendants private commit | Retained history per terminal |
| --- | ---: | ---: | ---: | ---: |
| 0 | 140.48 MiB | 161.49 MiB | 140.48 MiB | none |
| 1 | 158.23 MiB | 178.05 MiB | 160.52 MiB | 1,048,344 bytes / 1,444 lines |
| 5 | 179.53 MiB | 198.38 MiB | 190.99 MiB | 1,048,344 bytes / 1,444 lines |
| 20 | 263.56 MiB | 282.57 MiB | 309.43 MiB | 1,048,344 bytes / 1,444 lines |

The 1,048,576-byte history accounting limit held in every session. A byte cap
retains fewer rows than the requested line limit when wide/output-heavy rows
fill it. Only the visible terminal retained the application viewport cache
(2,305,720 bytes at this size); hidden terminals reported zero for that cache.
This does not measure Qt's internal paint textures, backing stores, or all
per-session allocations. GPU memory is excluded throughout.

Full numeric reports, executable hashes, per-session diagnostics, and validation
metadata are retained in [the measurement record](performance/2026-10-14-windows.json).
Earlier screenshot/prototype feasibility figures used different conditions and
are not a before/after comparison. No connection-speed or overall startup-speed
improvement over the old release is claimed by these measurements. Real SSH,
CyberArk, gateway approval, and MFA readiness remain unmeasured.

Validation on the rebuilt source:

* All 16 CTest targets outside the three restricted Windows fixture suites pass,
  including compact-history round trips, combining/wide characters, resizing,
  search after eviction, the byte cap, parser hostile-input regressions, settings,
  routes, and full UI interactions. One UI SFTP test skips because the system
  `sftp-server` fixture is unavailable.
* The separate Windows PTY run passes 13 QtTest cases/data rows, including literal
  Unicode arguments, independent working directories, control input, input and
  resizing, bounded stop, short-lived final output, flooding, and restart.
  The reader now preserves final queued output during natural process exit;
  manual stop still drains/discards after the owner is cleared.
* The complete 19-target run cannot be called green: native `ssh-keygen` fails
  creating its public-key fixture, a Windows hard-link fixture cannot be created,
  and the inherited-handle probe exits with `STATUS_INVALID_HANDLE` in this
  sandbox. The old PTY binary reproduces the inherited-handle failure. Those
  protections were not weakened, and those fixtures require a normal Windows
  user-session rerun before release packaging.
* QML lint/compilation, Python compilation, the probe counter self-test, and
  release-infrastructure checks pass. AddressSanitizer instrumentation was
  installed but an instrumented application build was not run. Linux validation
  remains outstanding because WSL is inaccessible here.

Reproduce this exact local runtime's probes with:

```powershell
python scripts/measure-performance.py --binary build/performance-runtime/bin/bitshell.exe --fixture build/windows-performance/windows_terminal_fixture.exe --native-window --renderer default --output build/performance-native-idle.json
python scripts/measure-performance.py --binary build/performance-runtime/bin/bitshell.exe --fixture build/windows-performance/windows_terminal_fixture.exe --native-window --renderer default --workload history --seconds 10 --output build/performance-native-history.json
```

## Rust decision

Rust is a candidate for a memory-safe terminal/session core and a small native UI.
It does not require a garbage collector. A Rust core behind the same Qt UI would
still carry Qt's UI/rendering/startup costs; changing language alone does not
establish a speed or memory improvement.

The current native Qt build does not fit the requested budget. The next useful
comparison is a native Windows renderer/window prototype against the Qt
implementation with the same real session and workload. A useful
Rust boundary is the session state, history, input handling, and eventually a
terminal parser, while keeping reviewed connection policy and OS/process calls
explicit. FFI and any retained C parser remain memory-safety boundaries. No Rust
rewrite or Rust toolchain installation is included in this implementation pass.

A replacement must preserve literal argument launching, strict known-host checks,
no agent forwarding, local gateway approval, private-key/ACL protections,
Unicode/ANSI behavior, resizing, safe paste/broadcast confirmation, complete
logging, readable YAML compatibility, SFTP routing, and cleanup. Retain the
existing implementation as a correctness reference until the replacement passes
these contracts. Dependency vendoring/locking and untrusted-output tests remain
necessary in a Rust build.

Primary background:
[Rust and C interoperation](https://blog.rust-lang.org/2015/04/24/Rust-Once-Run-Everywhere/),
[Rust FFI safety boundaries](https://doc.rust-lang.org/nomicon/ffi.html), and
[Qt deferred loading and performance](https://doc.qt.io/qt-6.8/qtquick-performance.html).
