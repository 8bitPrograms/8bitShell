# Version 1.0.0

The first release under the 8bitShell name. 8bitShell is a programmable remote-access terminal workspace. It brings local terminals, OpenSSH sessions, SFTP, logging and organization-specific connection policy into one Qt Quick app. It is licensed GPL-3.0-or-later.

## Highlights

- **Terminals:** real PTY-backed local terminals on Linux, and ConPTY on Windows. They support 24-bit and 256-color ANSI, alternate-screen apps, mouse input, scrollback, history search and zoom per pane.
- **Tabs and layouts:** tabs you can reorder by dragging, and 2-, 4- or 6-pane layouts. Multi-SSH commands are reviewed before they run and use explicit include/exclude targeting. Keystrokes are never broadcast automatically.
- **SSH:** direct OpenSSH sessions from Quick Connect and saved profiles. Saved passwords are kept in device-local protected storage and are now used when you open a saved session. Key and agent login also work.
- **Key library:** a private library for OpenSSH/PEM keys and imported PPK keys. PPK conversion uses the bundled PuTTYgen.
- **Session catalog:** a persistent, human-readable catalog with nested folders, path-aware search, drag-and-drop moves, and importers for mRemoteNG trees and OpenSSH config aliases.
- **CyberArk PSMP routes:** structured routes with inherited folder rules and local gateway approval.
- **SFTP:** paired local and SFTP file browsers in each tab, with recursive and multi-file transfers and drag-and-drop upload.
- **Command palette** (Ctrl+Shift+P), macro button groups, custom regex highlighting that never changes terminal bytes, and plain-text session logs under `~/Documents/8bitShell/Logs`.
- **Optional host monitoring:** CPU, RAM, network and filesystem metrics, read over a separate read-only SSH channel.
- **Config files:** settings, sessions and macros are versioned YAML files with import, export, live reload and conflict protection.
- **Secure defaults:** unknown or changed host keys are refused, agent forwarding is off and legacy crypto is excluded.
- **Windows:** the MSI adds `8bitshell` to your user PATH. Console input supports ConPTY's Win32 keyboard mode. Ctrl+C interrupts the foreground program when nothing is selected.
- **Updates:** optional stable-release checks and SHA-256-verified downloads are in Settings > Updates. Automatic installation is off by default. Per-user Windows installs can opt in to installing on exit. Portable and Linux copies offer manual downloads.
- New Pixel Core icon: a teal terminal tile that matches 8bitPad.

## Install

Packages bundle Qt, the app's runtime libraries and the SSH command-line tools, so you don't need a Qt SDK. Windows packages also include the app-local MSVC runtime DLLs and the OpenSSH client tools, so neither the Visual C++ Redistributable nor the Windows OpenSSH Client feature has to be installed.

**Windows (x64, ARM64)**
- `8bitShell-1.0.0-windows-<arch>.msi`: per-user install with a Start menu entry and `8bitshell` on your PATH.
- `8bitShell-1.0.0-windows-<arch>-setup.exe`: a bootstrap that runs the same MSI.
- `8bitShell-1.0.0-windows-<arch>.zip`: portable. Extract it and run `Start-Portable.cmd`.

**Linux (x64, ARM64; glibc 2.35 or newer, e.g. Ubuntu 22.04)**
- Debian/Ubuntu: `sudo apt install ./8bitShell-1.0.0-linux-<arch>.deb`
- Fedora/RHEL: `sudo dnf install ./8bitShell-1.0.0-linux-<arch>.rpm`
- Arch: `sudo pacman -U 8bitShell-1.0.0-linux-<arch>.pkg.tar.zst`
- Any distribution: extract `8bitShell-1.0.0-linux-<arch>.tar.gz` and run `./8bitShell`.
- Per-user curl install: `curl -fsSL https://github.com/8bitPrograms/8bitShell/releases/latest/download/install.sh | sh`

Use `x64` or `arm64` as `<arch>`.

## Known issues

- **No macOS build in 1.0.0.** A known session-log bug, caught intermittently by `PerformanceRegressionTest::concurrentHiddenPtysKeepCompleteLogs` on macOS, holds macOS back. macOS builds will follow in 1.0.1 after the fix.
- **Windows:** packages aren't Authenticode-signed yet, so SmartScreen may warn.
- The SFTP panel uses OpenSSH `BatchMode`, so it needs keys or an agent. Password-only SFTP isn't available yet. Passwords still work in terminal sessions.
- Host keys must be verified before connecting. App-managed SSH ignores user and system `ssh_config` but still uses standard `known_hosts`.
- RDP/VNC, shared team sync and general-purpose conversation automation aren't included yet.
- Session logs and scrollback can contain secrets that a remote host echoes back. Logs aren't redacted or tamper-proof.

## Checksums

SHA-256 sums are attached as `SHA256SUMS.txt`.
