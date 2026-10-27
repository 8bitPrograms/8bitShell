# Search, split panes and multi SSH

## Find a session

Use the top search field or `Ctrl+K`. Results show the session name, host/user and
folder/tags, including sessions inside collapsed folders. Queries can combine terms,
such as `oracle prod`, `operator@db.example.test`, or `ssh db`. Exact names and hosts
rank above prefixes and other matches. The first 80 results are shown; refine a broad
query to find the rest. Matching is case-insensitive, but stored names are not rewritten.

Use Up/Down and Enter or click a result. Selection resolves the saved session's stable ID
again before opening it, preserving its connection policy. Catalog edits and moves update
the results. Empty input and Enter on a no-match query do not initiate SSH. Use the explicit
**Quick connect to:** button for a validated raw SSH target instead.

The Connections sidebar keeps its compact, path-aware tree filter as well.

## Arrange live terminals

The layout selector in the upper tab strip offers **Tabs**, **2 panes**, **4 panes** and
**6 panes**. Six panes form a 3×2 grid when the terminal area is wide enough and a 2×3
grid on narrower windows. These are fixed equal-sized layouts, not arbitrary nested splits.
Hide Connections and/or Files to give small panes more room.

Click a terminal or its pane header to make it active. Zoom, Find, macros, monitoring and
the file browser follow that active terminal. The highlighted border identifies it.
All visible panes remain independent PTYs; switching layouts does not recreate them.

Drag tabs to arrange their order. For more sessions than the selected layout holds,
use the page arrows; selecting a tab also opens its page. Hidden sessions continue running
and collecting output without rendering invisible terminal frames.

The outline lock is beside the open SSH tab/pane name, not in the saved catalog. It means
app-managed SSH with required host-key verification, **not** a claim that authentication
has completed. Local shells do not receive a lock.

## Send one command to selected servers

**Multi SSH is off initially.** Click its lower action-bar button beside Macros and Monitor
to show the command strip.

1. Finish authentication in each intended SSH terminal and check its shell prompt.
2. Open **Targets**. In **Include checked**, tick the desired sessions, or use **Target all**.
   In **Exclude checked**, use **Target all**, then tick the sessions to leave out.
   Switching include/exclude mode intentionally clears the effective selection.
3. Choose **All open SSH** or **Visible panes only**. The latter intersects the selection
   with the current page. A pane's own **Target** checkbox always means positive inclusion,
   regardless of the checklist mode.
4. Enter the command, then press Enter or **Review**. Check the complete command and exact
   recipients. Cancel is focused by default; only **Send to N terminals** queues the input.

The active terminal does not have to be one of the targets. Normal typing, clipboard paste
and macro buttons are **not** broadcast. Local shells, even ones where you manually typed
`ssh`, cannot be selected by this feature. Only app-managed SSH PTYs are eligible.

**Target all / Target none** affect eligible sessions in the chosen scope. Closing the
Multi SSH strip clears all targets, including those on other pages. New and restarted
sessions always begin unselected. Selections, review state and command history are not
persisted in settings; the command field remains in memory until cleared or the app closes.

## Safety boundaries

- Selection is an explicit assertion that login is complete. A raw SSH PTY does not provide
  a reliable authenticated-shell signal. The app cannot distinguish a shell from a password,
  MFA, `sudo`, editor, database or other interactive prompt. **Never target such a prompt.**
- Every send requires a new review, bound to the selected PTY instances and their launch/
  selection generations. Reviews expire after two minutes. A closed, restarted, deselected
  or input-blocked target invalidates the entire review before any input is queued.
- The review lists the connection's frozen destination/user/port, including the gateway for
  routed sessions. Later saved-catalog edits do not silently retarget an already open tab.
- Commands are bounded to 16,384 UTF-16 code units / 65,536 UTF-8 bytes and 64 targets.
  Terminal controls and invisible formatting are rejected. CRLF/CR normalize to newline;
  review shows the normalized text. Existing bracketed-paste framing is retained when
  requested by the remote application, followed by Enter. It is not a command sandbox.
- Preflight validates every target and queue capacity before writing. Remote execution is
  **not atomic**: a server can disconnect after another has received the command. A queued
  count is not proof of remote execution or success. Check every terminal; there is no
  automatic retry, rollback or exit-code aggregation.
- Do not broadcast passwords, tokens or other secrets. This feature does not add a command
  history file, but ordinary terminal echo/output may appear in each enabled session log.
- Existing host-key, algorithm, X11, routing and paste-review policies remain unchanged.
  This is a Linux implementation; no real corporate/CyberArk connection was used to test it.

## Implementation notes

Session instances remain owned by their existing QML Repeater; the layout only changes
geometry and visibility. The reviewed target list uses guarded QObject pointers so a
destroyed terminal cannot become a dangling target. See the official Qt documentation for
[Repeater ownership](https://doc.qt.io/qt-6/qml-qtquick-repeater.html) and
[QPointer lifetime tracking](https://doc.qt.io/qt-6/qpointer.html).

The production broadcast path has no fixture switch. Automated tests use a C++ test-only
friend to attach local PTY fixtures that acknowledge literal input without executing it;
separate localhost SSH tests cover transport trust enforcement.
