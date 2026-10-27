# 8bitShell performance implementation plan

Recorded: 2026-10-17. Status: implementation plan; the replacement architectures
below have not been benchmarked or selected. This document records the agreed
direction and the next work. Existing implementation evidence remains in
[PERFORMANCE-BUDGET.md](PERFORMANCE-BUDGET.md) and the
[2026-10-14 Windows measurements](performance/2026-10-14-windows.json).

The goal is a fast, responsive Windows/Linux terminal and remote-access
application with the complete existing feature set. Improve the amount of work
the application performs, its data layout, and its scheduling. Changing language
or UI framework is allowed when the evidence supports it.

**Priorities and constraints**

| Priority | What we want | How we will judge it |
| --- | --- | --- |
| 1. Speed | Responsive typing, scrolling, resizing, tab switching, startup, and sustained terminal output | Measure interaction latency, including slow outliers, alongside throughput and startup |
| 2. Security and correctness | Preserve the existing trust, process, key, and terminal behavior | Required acceptance gates for every candidate, regardless of speed |
| 3. Resource efficiency | Lower idle CPU, memory growth, copying, allocations, and unnecessary rendering | Measure application, child processes, and GPU resources separately |
| 4. Maintainability | A shared core and clear platform boundaries | Keep changes reviewable and compare the cost of one UI against two frontends |

Speed is the main optimization objective. Security and correctness are mandatory
constraints, not optional tradeoffs. A faster result with missing output or
weaker host-key verification is not acceptable.

The earlier 50 MiB application / approximately 2 MiB additional ordinary-tab
memory targets remain aspirations, not established capabilities or universal
limits. Define the grid size, retained history, visible panes, and enabled
features before judging them. Do not select a slower implementation solely to
hit a memory number.

Preferred implementation languages are C, C++, Go, and Rust. Java, C#, and
equivalent managed-VM application stacks are outside the proposed direction.
Go remains explicitly eligible. The existing no-GTK/libadwaita UI constraint
also remains in effect.

**What exists already**

The current application uses C++23, Qt Quick, the pinned and locally patched
libvterm parser, Windows ConPTY, Linux forkpty, and system OpenSSH. This is the
reference implementation for behavior and compatibility.

The existing performance work already includes:

- First-use creation of optional editors and the local/SFTP browser, retaining
  their state after closing or hiding.
- Shared bounded font-resolution caching, compact scrollback storage, dirty-row
  caching, and release of the application viewport image when hidden.
- Bounded Windows output queues and smaller GUI drain batches with backpressure.
- Catalog indexing/virtualization, incremental history search, and reduced work
  for hidden panels.
- A Windows measurement script and per-terminal memory diagnostics.

Do not count these as new work without identifying an additional improvement.
The 2026-10-14 report records native/default private commit of 159.23 MiB with one
idle tab and 243.08 MiB with twenty. Those observations used local fixtures,
logging off, and short settling periods. They are historical evidence, not a new
baseline or a conclusion that Qt itself is the dominant bottleneck.

Two specific implementation details deserve attention:

- [TerminalView](../src/terminal/include/bitshell/terminal/TerminalView.h)
  currently owns the process, logger, parser/screen, history, and presentation
  state. Their lifetimes are coupled to a Qt Quick item.
- [TerminalView.cpp](../src/terminal/src/TerminalView.cpp) has dirty-row tracking,
  but cursor blinking and several damage paths still request an unrestricted
  `QQuickPaintedItem::update()`. Cached row drawing does not by itself establish
  that later painting and texture work are limited to those rows.

The current scrollback also has a 1 MiB accounting cap in addition to its
configured line limit. This can retain fewer lines than requested. Reducing that
cap or accepting shorter retained history must not be credited as a performance
improvement. Comparisons must verify the actual retained content and capacity;
the new storage design should preserve the requested line capacity rather than
silently truncate it to meet a memory target. If capacity is not equivalent,
report that difference explicitly and do not claim feature parity.

**Target architecture**

| Component | Owns | Boundary |
| --- | --- | --- |
| Terminal/session core | Session lifecycle, ordered input/output, parser, screen, history, logging, and terminal search data | Independent of a visible widget or Qt Quick item |
| Platform adapters | ConPTY/forkpty, process launch, executable resolution, cleanup, and platform file protections | Preserve the existing reviewed OS behavior |
| Terminal presentation | Viewport, selection/scroll position, cursor presentation, font/glyph caches, and damage tracking | Read explicit changes or bounded snapshots; avoid copying all history per frame |
| Application UI | Catalog, tabs, splits, settings, macros, file panels, and monitoring controls | Bind to persistent models rather than own live terminal processes |

Start by extracting ownership in C++ while preserving the existing QML-facing
API. Qt Core types may remain in the first extraction; a fully toolkit-neutral
ABI is a separate step if a second frontend or Rust core needs it. A language
boundary should exchange batches, not make one FFI call per character or cell.

Keep the established routing/policy and key-selection paths shared. Hiding or
replacing a view must not restart a session, lose final output, stop enabled
logging, or reset per-tab file-browser state. Logical separation is not process
isolation; future isolation work in [ARCHITECTURE.md](ARCHITECTURE.md) remains a
separate security boundary.

**Implementation order**

1. **Refresh measurements and establish equivalent workloads.** Use the current
   Release source and record binary hashes. Separate startup, PTY I/O, parsing,
   history, search/highlighting, logging, and rendering costs. Profile CPU,
   allocations, and graphics resources before choosing the most expensive work
   to change. Use at least 60 seconds of settled idle observation. Keep the
   existing script's logging-off probe as a clearly labeled diagnostic and add
   representative logging-on/monitoring-on cases.

2. **Separate the session from the view.** Introduce a session object behind the
   current UI interface. Move process/parser/history/logger ownership in small
   steps. Preserve resize ordering, callbacks, final output draining, tab closure,
   managed identity propagation, and current search/selection behavior. Retain
   the existing renderer as the correctness reference during the extraction.

3. **Reduce work in the current implementation.** Carry damage rectangles through
   the paint path and invalidate old/new cursor bounds correctly. Reuse buffers,
   rows, and history pages; avoid repeated decoding or copying of unchanged
   content. Keep caches bounded and share compatible resources between views.
   Preserve idle/event-driven behavior. Investigate time-budgeted output draining
   and fair scheduling across busy tabs. Respect ConPTY's channel requirements;
   do not replace its I/O servicing with an unverified threading shortcut.

4. **Optimize measured CPU hot spots.** Evaluate bulk ASCII/UTF-8 processing,
   Unicode-width lookup tables, style interning, and reusable allocation pools
   where the profile justifies them. Retain the patched parser until a replacement
   passes the same behavioral and hostile-input corpus. SIMD needs CPU dispatch
   and a correct fallback. Background parsing requires a clear single-writer
   ownership model and measured synchronization costs. Incremental search,
   virtualized lists, cancellation of stale work, and batched logging should
   preserve results and the existing flush/error behavior.

5. **Compare renderer/frontend candidates using the same core.** Build a narrow
   runnable terminal prototype for each shortlisted option before porting the
   full application UI. Keep the font, terminal grid, actual history, output
   replay, and visible panes equivalent. A prototype win is provisional until
   catalog, SFTP, monitoring, and other application features are included.

6. **Choose the production stack and complete validation.** Record the decision
   and measurements in an ADR. Port remaining UI only if the measured benefit
   warrants it. Evaluate compiler LTO/PGO using representative workloads after
   the architectural changes. Keep the reference implementation available until
   compatibility and performance gates pass on both Windows and Linux.

**Language and framework decision**

C++ is the first implementation path because it lets us measure improvements
against the existing application without simultaneously replacing the language,
parser, and UI. Rust is the preferred candidate if we decide to write a new core:
memory safety without a garbage collector is valuable, but performance still
depends on allocation, layout, and synchronization. Retained C code and FFI do
not acquire Rust's memory-safety guarantees automatically.

C is suitable for focused low-level components. Go/Gio is an eligible alternative,
with allocation and garbage-collector costs included in its measurements. No
language has been declared the performance winner.

| Candidate | Scope | Decision criterion |
| --- | --- | --- |
| Optimized C++ / Qt Quick | Current UI with improved damage handling; evaluate a custom scene-graph terminal viewport | Establish how much improvement is possible with the existing application shell |
| C++ / Qt Widgets | One shared UI with a custom terminal viewport; compare raster and custom GPU rendering | Demonstrate a worthwhile benefit that justifies porting QML screens |
| Native Windows + Qt Widgets Linux | Win32/DirectWrite-based Windows frontend and Qt Linux frontend sharing the core | Demonstrate platform benefits large enough to justify maintaining two UIs |
| WinUI 3 with C++/WinRT | Alternative native Windows application shell | Measure its startup/resource costs; it does not require adopting C# |
| Rust with Slint or Iced; Go with Gio | Additional shared-UI challengers if the first comparisons justify a broader rewrite | Full input/text/platform compatibility and measured application-level gains |

The first comparison is optimized Qt Quick, Qt Widgets, and the native Windows
prototype. A Rust rewrite is not a prerequisite for that comparison. If those
options meet the goals, avoid expanding the experiment without a concrete reason.

Renderer choice must be explicit: a software path may have different throughput,
memory, text, or graphics-feature behavior from a GPU path. Slint's researched
software renderer documents script limitations; verify the selected version and
backend before relying on it. Qt's `QRhiWidget` can host custom GPU rendering,
but the underlying QRhi classes require version-aware integration. Neither a
framework's advertised lightness nor an empty-window benchmark proves suitability.

**Acceptance workloads and feature parity**

| Area | Required comparison |
| --- | --- |
| Startup | Cold and warm process launch to usable interface; main-to-first-frame separately; first use of optional panels |
| Interaction | Median/p95/p99 typing-to-display latency under load; scrolling, resize, tab switching, search, and large paste |
| Throughput | Plain text, ANSI-heavy output, Unicode, operational logs, and representative `vim`/`tmux`/SQL console use |
| Scale | 0/1/5/20/50 sessions, 2/4/6 visible panes, large catalogs, equivalent populated history, and repeated open/close cycles |
| Resources | Startup peaks and steady application private commit/working set; child processes and GPU allocations separately; active/inactive/minimized idle |
| Platforms | Windows hardware and remote-desktop/software conditions; Linux Wayland/X11; fixed fonts, resolution, DPI, and dependency versions |
| Correctness | Identical replay outcomes, complete log output, session cleanup, and the existing regression corpus |

Extend [measure-performance.py](../scripts/measure-performance.py) rather than
assuming its current switches already implement this entire matrix. Record
sample counts, variability, enabled features, renderer/device identity, toolchain,
and executable hashes. Compare changes one at a time where practical. Investigate
regressions in latency even when aggregate output throughput improves.

Feature parity includes tabs and splits; configured scrollback; Unicode, wide and
combining characters; IME, clipboard, mouse and alternate-screen behavior;
selection, search, highlighting, themes and zoom; YAML compatibility/imports;
session/folder routing; macros and reviewed multi-SSH targeting; key selection and
PPK import; SFTP; monitoring; complete enabled logging; and keyboard/accessibility
behavior. Keep existing authentication limitations explicit rather than implying
the rewrite adds unfinished features. Future product features remain on
[ROADMAP.md](ROADMAP.md).

Preserve literal program-plus-argument launching, strict host-key checks, disabled
agent forwarding, gateway approval, key/ACL protections, and paste/broadcast
confirmation. Do not shorten history, drop terminal output, disable a requested
feature, reduce text quality, or weaken these controls to obtain a better score.
Use synthetic/local benchmark data. Real SSH readiness, DNS, authentication,
CyberArk, and MFA timings need separate authorized environment measurements.

Reuse the existing regression suites and add targeted coverage for changed
ownership, damage, parser, and lifecycle behavior. Run sanitizer/fuzz/replay work
where it resolves those risks. The prior Windows run had three environment-blocked
test targets, and Linux validation was outstanding; a new report must distinguish
passed, failed, blocked, and unrun checks. Existing release/security gates remain
in force.

**Next work and completion evidence**

- [x] Record the plan against the current source and link it from the README.
- [ ] Capture a fresh Release baseline with equivalent feature/history workloads.
- [ ] Extract session ownership behind the existing UI API.
- [ ] Implement and measure precise redraws and the highest-cost allocation/I/O work.
- [ ] Compare the three initial frontend/renderer candidates with the same core.
- [ ] Record the stack decision and any justified Rust boundary in an ADR.
- [ ] Complete feature-parity validation and publish reproducible before/after data.

Each implementation change should identify the bottleneck, explain the change,
show comparable measurements, and state validation limits. The result should be
the existing application doing less unnecessary work, with a measured reason for
every substantial architecture change. No replacement benchmark results or new
optimization claims are implied by this planning document.

**Research references**

- [Qt painted-item rendering and damage updates](https://doc.qt.io/qt-6/qquickpainteditem.html)
- [Qt custom GPU widgets and compatibility boundaries](https://doc.qt.io/qt-6/qrhiwidget.html)
- [Ghostty's shared-core/native-frontend architecture](https://ghostty.org/docs/about)
- [Ghostty bulk processing and Unicode lookup optimizations](https://mitchellh.com/writing/ghostty-devlog-006)
- [Ghostty page storage, reuse, and allocation accounting](https://mitchellh.com/writing/ghostty-memory-leak-fix)
- [Rust ownership](https://doc.rust-lang.org/book/ch04-00-understanding-ownership.html)
- [Go garbage collector tradeoffs](https://go.dev/doc/gc-guide)
- [Slint renderer capabilities and limitations](https://docs.slint.dev/latest/docs/slint/guide/backends-and-renderers/backends_and_renderers/)

These sources informed the research discussion. Recheck API/backend constraints
against the dependency versions selected for an implementation; external project
benchmarks are not 8bitShell performance predictions.
