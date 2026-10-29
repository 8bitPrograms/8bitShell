# 8bitShell 1.0.0 validation

## Running-app update preflight — 2026-10-27

The subsequent native MSI update correctly entered reinstall mode, then failed
with 1603 because 8bitPad.exe was still running (PID 28884). The log explicitly
reported the executable in use, followed by Error 1310 while writing that file.

The local helper now waits up to three minutes for the selected installed
application to exit normally before invoking MSI. It identifies the executable
path, ignores a separate portable copy, and never force-closes the process.
A timeout reports the running PID before any MSI transaction starts.

All five installer-helper regression tests passed, including wait/release,
portable-copy filtering, timeout behavior and the earlier 1638 update paths.
The actual desktop installation still needs the app to be closed and the helper
run in the user's normal PowerShell.


## Same-version local MSI update correction — 2026-10-27

The user's external installation log reported product state 5 (already installed)
and PackagecodeChanging=1, but no REINSTALL properties. The local helper's single
HKCU uninstall-key check had missed the existing per-user product, causing 1638.

The helper now queries MsiQueryProductStateW, sets REINSTALL=ALL and
REINSTALLMODE=vamus for an existing product, and handles one service-side 1638
with the same verified package in in-place reinstall mode. It retains both logs
and never adds an uninstall step. Other errors, including 1601, are not retried.

The reusable planner/runner is scripts/ci/windows-msi.ps1 in each repository.
Three isolated regression tests passed: first-install/advertised states, an
installed product with no uninstall registry key, and bounded 1638 recovery
including repeated conflicts and access-denied service errors. No real MSI
installation, profile or association changes were performed by these fixtures.
The local helper's PowerShell syntax and read-only plan mode also passed.

Microsoft's update guidance:
https://learn.microsoft.com/en-us/windows/win32/msi/applying-small-updates-by-reinstalling-the-product

The user's actual MSI update still needs to run from normal PowerShell; this
managed session does not share the same product-registration visibility and
previously could not connect to the Windows Installer service.


## Local installable build — 2026-10-27

Both applications were rebuilt and tested from the current working trees:
8bitShell 21/21 CTest suites, 8bitPad 15/15. Fresh runtime payloads passed
SDK-free startup checks. The local x64 version 1.0.0 MSI packages were compiled
using cached WiX 6.0.2 after converting generated authoring; the CI authoring
remains WiX 3 compatible. Their compiled database tables were checked for the
correct product/version, per-user scope, 8bitPrograms Start Menu group,
application/uninstall shortcuts and empty-folder-only uninstall cleanup.

The local NSIS compiler could not start (Windows DLL relocation error), so the
local install artifacts are MSIs. The prepared EXE workflow still requires
validation on a suitable build host.

Installation from the managed session stopped before installing files:
Windows Installer could not connect to its service, returning 1601 with
access denied 0x80070005. Actual desktop installation/uninstallation is therefore
still pending. A checksum-verified helper for both local MSIs is under the
ignored 8bitShell build/maintenance directory, for execution from the user's
normal PowerShell.

Build trees, packages, SDK caches and this generated helper are ignored by Git.


## Local build guides and startup-folder fix — 2026-10-27

The documented Windows x64 entry point was exercised with the relocated Qt
6.11.2 SDK and MSVC 2022. All **21/21** CTest suites passed. The new startup
regression first reproduced a saved profile directory overriding the requested
folder, then passed after the fix. Its final focused rerun also checks that a
later normal tab uses the saved profile directory.

A fresh portable folder was deployed with app-local Qt, CRT and locked SSH
clients. Startup passed with SDK environment paths cleared. A separate check
launched the actual portable EXE and verified the terminal child's cwd both with
the saved default and with `--working-directory`, including spaces, Turkish
characters and shell punctuation. Saved profile bytes remained unchanged.

See [BUILDING.md](BUILDING.md) for commands and reports. These Windows checks do
not establish hosted Linux/ARM/macOS or installer lifecycle validation.


Verified locally on 2026-10-27 after migration to this new checkout.

- Fresh Windows x64 Release build succeeds with MSVC and Qt 6.11.2.
- All 20/20 CTest suites pass.
- The release contract suite has 10 tests: 9 pass, with the POSIX-only fixture skipped on Windows. Its Windows fixture checks x64, ARM64 and x86 MSI authoring without installing test packages.
- Fresh Windows ZIP and tar.gz packages are in dist/release. Their SHA256 digests match the target manifest.
- The packaged app starts with SDK/plugin variables removed and System32-only PATH.
- Source scanning finds no deprecated application names or nested VCS metadata.
- The existing new origin is git@github.com:8bitPrograms/8bitShell.git. Previous repository history was not imported.
- Local SDKs and caches are ignored; build paths point to the new checkouts. Upstream source and license terms are retained.
- Runtime settings use only the current app namespace or an explicit profile override. Unrelated personal profile directories are neither adopted nor deleted.
- Both installers use the shared 8bitPrograms Start menu group, product-specific uninstall shortcuts and normal Windows Installed apps registration.

The native Linux/ARM/macOS matrix, experimental 32-bit builds, compiled MSI/EXE lifecycle tests, Windows signing and Apple signing/notarization still need their corresponding hosted/distribution checks. Preparing these flows does not claim those checks passed locally.

The new local Git history starts from the migrated source. No public release is created by this migration. See RELEASING.md for release setup and publication checks.

Two environment-dependent cases skip inside the passing suites: remote file drag/drop needs an SFTP server, and the managed host cannot create one hard-link fixture. The existing project-license/redistribution/tooling publication gate remains incomplete.

## 2026-10-28: keyboard input, commands and optional updates

- Windows x64 Release: 23/23 CTest suites pass. The native PTY fixture exits raw VT, processed Ctrl+C and Win32-input foreground programs, then executes another PowerShell command. Ctrl+C bound to copy also interrupts when no text is selected.
- The Win32 keyboard codec handles split mode requests, ignores OSC/DCS payloads, bounds sequence buffering, and sends key-up records with released modifiers cleared.
- Offline updater fixtures cover stable numeric versions, architecture selection, repository/tag restrictions, checksums, damaged downloads, optional defaults and skipped versions. No installer or live release is invoked by those tests.
- The Python distribution/MSI helper suites run 15 tests: 14 pass and the POSIX fixture skips on Windows.
- A fresh portable app starts with SDK paths removed. The compiled local MSI contains the updater helper, user PATH install/removal actions, the shared Start menu and the product-specific uninstall shortcut. Its payload matches the tested executable; build artifacts remain ignored.

Local MSI execution is performed from normal Windows PowerShell: the managed test host cannot access the user's Windows Installer service. Use the refreshed maintenance installer with `-Verify`, or [the standalone verifier](UPDATES.md#desktop-verification). Actual Grok interaction, a published-release update/rollback, Linux/macOS/ARM execution, signing and the existing publication gates remain separate checks. No release was published by this work.

### Version taps and Cyberpunk palettes

All 23 Windows x64 suites pass after adding the Settings version button and the
five bonus choices: Blueberi, Raspberi, Silverhand, V and K. The core tests check
seven consecutive taps, timeout reset and persisted unlocking; the UI test clicks
the actual version button, confirms the choices appear without changing the
current theme, then selects all three Cyberpunk palettes and checks the interface
and terminal colors, including their 16-color ANSI arrays. Day/night selection,
keyboard navigation and the existing terminal tests continue to pass. Fresh
portable and MSI payloads include the changes. The installed offscreen verifier
uses the Windows font directory, matching the clean portable startup check.

The follow-up theme checks cover all four pickers with previously saved bonus
selections while locked, public fallback palettes for unknown IDs, and unlocking
through the version button. Saved rendering is preserved, but bonus entries stay
hidden until unlocked. The three Cyberpunk themes now have filled active tabs,
primary header bands and secondary cyan/ice-blue pane/status accents. The UI
suite checks actual component colors and rendered header pixels, and captures all
three using a labeled ANSI fixture without executing its sample commands.

### 2026-10-29: full Cyberpunk headers and linked theme selection

Silverhand now fills the upper command/title bar with yellow; V uses red and K
cyan. Header text, panel controls, hover/focus states and window buttons have
their own contrasting foregrounds. The connection field and brand backing stay
dark, while existing tab, terminal and status colors are retained. Actual
captures of all three palettes were reviewed, as was the public light theme.

Appearance defaults to linked UI/terminal themes. The optional **Separate
terminal & UI themes** switch reveals the independent terminal day/night row.
Stored terminal choices remain available when re-enabled. Both the preview and
every terminal session use the same resolved palette, including unknown-ID
fallbacks. Failed settings writes leave the visible switch in its actual state.

The Windows x64 run passed the 22 non-UI suites; the UI suite passed after the
final fallback and toggle corrections were rebuilt. UI checks click the switch,
verify live ANSI/background changes, preserve the terminal instance and tabs,
exercise day/night and reload behavior, and check rendered header pixels and
text contrast. Core checks cover old-profile defaults, saved independent choices,
YAML import/export, resets and rejected writes. The existing SFTP integration
case remains skipped when no local sftp-server fixture is available.

### 2026-10-29: saved SSH accounts inside local terminals

Supported PowerShell and Bash prompts now offer exact-host saved SSH accounts and
explicit CyberArk routes. Selection resolves the catalog again and runs native SSH
in the existing local process. Nonce-qualified, session-only prompt markers keep
assistance out of running applications and password prompts. Unsupported commands
retain ordinary shell behavior. Gateway and X11 review still precede activation.

The Windows x64 validation covers all 27 suites: 26 passed in the final full run;
the remaining local-terminal suite passed after its asynchronous argv-output
assertions were corrected to wait for separate PTY reads. That suite passes 31
cases, including a real PowerShell session with an isolated, network-free native
SSH executable fixture. It verifies account/port arguments, repeated characters,
an unchanged process ID, cancellation, stale-input rejection, and maximizing an
empty prompt before typing. Test history is isolated and disabled before input.
The existing Windows Ctrl+C, rendering, memory and performance tests also pass.

The new suggestion UI suite passes 8 cases, including initialization/cleanup,
covering keyboard selection, changed/deleted records, route and X11 cancellation,
and narrow-viewport geometry. Its rendered account list and gateway review were
inspected. The main UI suite passes 32 cases with the pre-existing SFTP fixture
skip. Native command tests pass 9 cases, including connection-free `ssh -G`
checks for the environment client and the Windows packaged-client fallback.

Portable build `8bitShell-windows-x64-20261029-100222` passed clean-environment
startup. WSL discovery is denied by this execution host (`Wsl/E_ACCESSDENIED`),
and its Git Bash runtime is also unavailable. Bash/WSL bootstrap construction and
Windows-key conversion/copy/cleanup are covered structurally, but live WSL SSH
and corporate CyberArk authentication remain separate acceptance checks. No real
SSH or CyberArk server was contacted during this work.

### 2026-10-29: command palette and dialog keyboard access

The lazy command palette opens with Ctrl+Shift+P or the title-bar Search button.
It finds exact host matches before partial tab matches, distinguishes accounts
and ports, resolves stable tab/catalog IDs again at activation, and offers saved
or new SSH connections. Settings sections, local launchers, panel toggles and
theme actions use the same application operations as their existing controls.
Bonus-theme unlocking and linked/separate terminal themes remain enforced.
Unavailable saved routes cannot implicitly select a direct connection instead.

The palette suite passes 11 checks, including real keyboard events, duplicate IP
tabs, normalized IPv6, renamed/reordered/closed tabs, catalog edits, prefilled
connection creation, blocked routes, query-preset focus, themes, settings and
local launchers. Isolated fixtures launch no SSH server connection or WSL distro.
Rendered IP results and theme lists were reviewed at the minimum window size.

The native SSH suite passes 34 checks, including metadata creation and clearing
at the authenticated parent prompt, process exit and explicit stop. Explicit
stop now also cancels any pending local SSH dispatch. The suggestion/dialog
suite passes 10 checks, including Tab/Shift+Tab and Left/Right button navigation,
visible focus, Enter/Space activation, Escape and error-dialog OK focus. The
13 settings checks include configurable shortcut persistence and migration of
older profiles whose explicit shortcut already occupies Ctrl+Shift+P.

All 28 Windows x64 CTest suites pass on the final sources. The existing SFTP UI
fixture remains skipped when its server executable is unavailable. Portable
build `8bitShell-windows-x64-20261029-103423` passed clean-environment startup.
The matching MSI in `dist/local-install-20261029-103423` was inspected for version,
PATH, updater, Start menu and uninstall entries; its staged executable matches
the tested build. The maintenance installer now references this MSI. Build,
capture and installer outputs remain ignored by Git.
Live WSL launching and corporate CyberArk authentication remain acceptance checks
on the user's normal desktop; this execution host denies WSL discovery.
