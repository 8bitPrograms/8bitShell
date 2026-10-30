![8bitShell logo](assets/bitshell.svg)

# 8bitShell

**Kendi yolunu aç.**

**Current version: 1.0.0.** Pixel Core branding: teal terminal.

Local build, test, portable launch and troubleshooting instructions: [docs/BUILDING.md](docs/BUILDING.md).

Optional version checks and updates are in **Settings > Updates**. Automatic installation starts disabled. Windows installation adds `8bitshell` to user PATH; see [commands and updates](docs/UPDATES.md).

Click the version seven times in Settings to reveal Blueberi, Raspberi and the Cyberpunk-inspired Silverhand, V and K palettes for both the interface and terminal. Unlocking is saved per profile and keeps the current theme; see [theme unlocking](docs/UPDATES.md#version-and-bonus-themes).

Cross-platform release builds, bundled runtimes, curl installation, native packages and repository maintenance are documented in [docs/RELEASING.md](docs/RELEASING.md).

**Version 1.0.0 in local release preparation.** Executable: `8bitShell`; Linux package: `8bitshell`.
See [releases, installers, versioning and distro repositories](docs/RELEASING.md).
Copyright (C) 2026 8bitShell contributors. 8bitShell is free software under the
GNU General Public License, version 3 or any later version; see [LICENSE](LICENSE).
Qt remains a separate, dynamically linked LGPL-3.0/GPL-3.0 library. yaml-cpp,
PuTTY, and libvterm remain MIT. Installer publication stays closed until the
third-party redistribution and Windows packaging reviews are recorded.

8bitShell is a programmable remote-access terminal with a usable Linux build and a
native Windows 11 preview. The shared Qt Quick interface combines terminals, SFTP,
logging and organization-specific connection policy. Remote desktops and shared
team synchronization remain roadmap items.

The following describes the Linux application; see [Windows support](docs/WINDOWS.md)
for the native port, build instructions and its separate validation boundaries:

- a custom Qt Quick desktop shell;
- a real Linux PTY-backed local terminal rendered inside Qt Quick;
- direct OpenSSH sessions from Quick Connect and saved profiles;
- password authentication with optional device-local protected storage, separate from
  YAML exports, plus the existing key/agent authentication;
- a private multi-key library for validated OpenSSH/PEM and imported PPK keys, with separate
  application and CyberArk defaults plus assignment-preserving key replacement;
- structured CyberArk PSMP routes, local employee identity, inherited folder rules,
  explicit direct exceptions, and local gateway approval (no live PSMP validation);
- opt-in local profile pictures with an initial displayed by default;
- 24-bit/256-color ANSI rendering, alternate-screen applications, mouse input, scrollback,
  selection, clipboard, and per-pane zoom;
- multiple live tabs with drag reordering, horizontal overflow navigation, and terminal
  history search;
- 2/4/6-pane layouts and reviewed multi-SSH commands with explicit include/exclude
  targeting, all-open or visible-page scope, and no automatic keystroke broadcasting;
- ranked session-search suggestions with names, hosts and folder paths, keyboard selection,
  and live catalog updates;
- a Ctrl+Shift+P command palette for switching matching open tabs, starting SSH/local
  sessions, changing themes and toggles, and opening settings sections;
- keyboard-selectable saved SSH accounts and CyberArk routes at supported local
  PowerShell/Bash prompts, executing with native SSH in the same terminal;
- a persistent, human-readable SSH session catalog with nested folders,
  virtualized browsing, path-aware search, remembered expansion state, and persistent
  drag-and-drop moves between folders or back to the catalog root;
- bulk importers for complete mRemoteNG trees and concrete OpenSSH config aliases;
- versioned YAML session/settings/macro files with safe legacy migration, import/export,
  live reload, and external-edit conflict protection;
- paired, per-tab local/SFTP file browsers with explicit remote connection, directory history, recursive and multi-file
  transfers, drag/drop upload, native file-URL exports, and selectable download destinations;
- persistent macro button groups with nearest-non-empty-folder inheritance and live updates;
- optional per-tab host monitoring with native local Windows metrics and Linux
  CPU/RAM/network/top-filesystem metrics locally or via a separate read-only SSH command channel;
- configurable shortcuts (Linux and Insert presets), Ctrl+wheel zoom, terminal context menus,
  and opt-in copy-on-select;
- a structured connection resolver with policy-driven routing;
- a prompt-aware conversation state machine;
- persistent custom regex highlighting, applied live without changing terminal bytes;
- plain-text session logging under `~/Documents/8bitShell/Logs`;
- linked day/night UI and terminal palettes, with an optional **Separate terminal & UI themes** switch; classic terminal palettes such as XTerm, VGA, rxvt, and Tango, OS/scheduled
  automatic switching, and a one-click day/night toggle;
- global and per-session Backspace byte selection (`DEL/^?` or `BS/^H`) for modern and
  legacy/Oracle terminal environments;
- application settings for themes, fonts, scrollback, logging, and highlighting;
- versioned, human-readable session and policy schemas;
- desktop integration and password-free per-user installation;
- tests for catalogs, settings, highlighting, SFTP orchestration, PTY input/output,
  connection policy behavior, compact context menus, editing actions, catalog drag/drop,
  large-list edge scrolling, theme isolation, tab overflow/reordering, and real PTY
  Backspace bytes.

General-purpose conversation automation, RDP/VNC, shared team synchronization, and the final
credential broker remain later milestones. The SFTP panel deliberately uses OpenSSH
`BatchMode` today: SSH keys and agents work, while password-only graphical SFTP waits for
the in-app credential prompt. Password authentication continues to work normally inside a
terminal session.

See [CyberArk and personal profile](docs/CYBERARK.md) for the implemented PSMP grammar,
readable shared catalog, limits, avatar controls, and Linux terminal registration.
See [Search, split panes and multi SSH](docs/MULTI-SSH.md) for layout controls,
target-selection rules and broadcast safety boundaries.
See [Windows port](docs/WINDOWS.md) for the native ConPTY/MSVC build, file-security
boundaries and the Windows-specific acceptance checklist.

## Build

Requirements:

- CMake 3.24 or newer
- Ninja
- a C++23 compiler
- Qt 6.10 or newer with Core, Gui, QML, Quick, Quick Controls, Quick Dialogs, Concurrent, Network, and Test
- libvterm 0.3.3 is fetched with a pinned SHA-256 and local parser bounds fixes;
  the same bundled source is compiled on Linux and Windows
- yaml-cpp 0.9 development files (optional: CMake fetches a SHA-256-pinned 0.9.0 source
  release when absent; no system installation or administrator password required)
- PuTTYgen for optional PPK import. Official packages and the supplied build scripts use
  a command-line converter built from the SHA-256-locked official source; manual Linux
  source-tree builds may use `/usr/bin/puttygen` or set `BIT8SHELL_PUTTYGEN_EXECUTABLE`.
  OpenSSH/PEM keys do not require it.

For Windows validation without installing a local compiler or SDK, use the
[manual GitHub Actions build](docs/WINDOWS.md#cloud-build-without-a-local-windows-toolchain).

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
./build/dev/8bitShell
```

For normal development use, the helper builds on first launch:

```bash
./scripts/run-dev.sh
```

To build, test, and install into `~/.local` without root access:

```bash
./scripts/install-user.sh
```

After installation, open **8bitShell** from the desktop application menu or run
`~/.local/bin/bitshell`.

Use security-patched dependency builds; the minimum compilation version is not a security
support guarantee.

Type commands directly in a local terminal, or enter `host`, `user@host`, or an SSH target
with `-p`, `-l`, `-i`, `-J`, `-X`, `-C` in Quick Connect. Enter opens the highlighted saved
result; use the explicit **Quick connect to:** button for a raw target. Arbitrary SSH options and remote
commands are blocked there. App-managed SSH, SFTP, and monitoring share a strict policy:
unknown/changed host keys are refused, agent forwarding is disabled, and legacy crypto is
excluded. Provision verified host keys or a host CA before connecting. Password/MFA prompts
remain available in an authenticated-to-the-server SSH terminal; SFTP uses keys/agents.

The managed policy deliberately ignores user/system `ssh_config` (including hooks and distro
crypto-policy includes); standard `known_hosts` files remain in use. It is not a FIPS profile.
SSH typed directly inside a local shell uses that shell's ordinary configuration instead.
See [connection security and first use](docs/USER_GUIDE.md#connection-security-and-first-use).

Saved sessions, macros, terminal preferences, and private key metadata live in
`sessions.yaml`, `macros.yaml`, `settings.yaml`, and `keys.yaml` under
`~/.config/8bitshell/`. Key material is held separately in the owner-only `keys/` directory
and is never exported. The app uses only this canonical directory or an explicit BIT8SHELL_PROFILE override; unrelated profiles are never silently adopted or merged. Existing JSON/INI files migrate automatically and
are not deleted or rewritten. Highlight rules remain in `highlights.json`. The application
does not save prompt answers; do not enter credentials in macro text.

Use **Settings → Configuration · YAML**, the **Connections → …** menu, or **Macro groups**
to import/export and reload files. See the [configuration format](docs/CONFIGURATION.md) and
[importable examples](examples/config/). Session imports merge without overwriting existing
IDs; macro conflicts become separate copies. Settings imports validate before applying.

## Design constraints

- One visible application; OpenSSH transport processes are separate, while the terminal
  parser and UI still share a process. Further parser isolation remains a release goal.
- No GTK or libadwaita UI dependency.
- No encoded or client-specific session format as the source of truth.
- Never execute a generated connection through a shell string; use a program plus argument
  array.
- Never store credentials in session or policy files.
- Highlighting changes rendering only. Copied terminal text and logs preserve the original
  output.
- Keystrokes are not directly logged. Remote output/echo, scrollback, screenshots, and macros
  can contain secrets; ordinary session logs are not a redacting or tamper-proof audit system.

## Performance verification

Catalog indexing, incremental history search, bounded viewport caching and hidden-panel
work reduction preserve the security profile and complete session logging.
The UI regression tests cover batched theme updates, terminal smoothing/hinting,
connection indicators and interaction/layout behavior.

```bash
cmake --build --preset release --target performance_benchmark
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software build/release/tests/performance_benchmark
```

The benchmark is opt-in, uses isolated synthetic data and makes no network connections.
Correctness and security regressions remain in the normal CTest suite; there are no
machine-specific timing thresholds used to declare a security test successful.

## Security validation

[SECURITY.md](SECURITY.md) describes the current support/disclosure limitations. This build
is hardened and locally tested, not independently certified or declared vulnerability-free.

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
bash scripts/security-evidence.sh
# Requires your toolchain's ASan and UBSan runtime development packages:
cmake --preset security
cmake --build --preset security
ctest --preset security
```

The trust integration test starts a temporary unprivileged loopback SSH server if `sshd`
and `ssh-keygen` are available. It uses disposable keys, not your real credentials or trust
store. A skipped trust test is not evidence of successful SSH security validation.

See [single/bulk/folder catalog deletion](docs/CATALOG-DELETION.md) and
the [user guide](docs/USER_GUIDE.md).
