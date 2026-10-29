# Optional updates and command-line checks

Both applications start at **1.0.0**. Updates use stable GitHub Releases from
[8bitPrograms/8bitPad](https://github.com/8bitPrograms/8bitPad/releases) and
[8bitPrograms/8bitShell](https://github.com/8bitPrograms/8bitShell/releases).

Open **Help > Updates** in Pad, or **Settings > Updates** in Shell.
Version checks are enabled by default, run after startup at most once per day,
and can be turned off. Automatic installation is **off by default**.
You can check manually, download, skip a version, or postpone installation.

Installed per-user Windows copies can install a verified MSI after all their
windows close. “Install and restart” follows the normal close/save prompts.
If you enable automatic installation, the package downloads in the background
and is applied when you next close the application. Other open copies postpone
installation; the helper never kills your application or discards documents.
Failures are recorded in `install-update.log` beside the downloaded package.
An update requiring a reboot is not automatically restarted.

Portable Windows, Linux and macOS copies support version checks and verified
downloads; install/extract the downloaded package manually, or use your normal
system package manager. Automatic replacement is currently limited to the
standard per-user Windows installation. No published release is a normal state,
not a startup failure.

The updater accepts only stable numeric three-part versions, an exact asset
for the application's native architecture, and its entry in `SHA256SUMS.txt`.
HTTPS downloads are restricted to GitHub and its release asset hosts. The
download is streamed with size limits and checked again before Windows
Installer runs. Checksums detect damaged/mismatched downloads; release publishing
and signing credentials remain part of the repository's release process.

## Version and bonus themes

The version is always visible in Settings. Click it seven times in quick
succession (less than 1.5 seconds between clicks) to reveal Blueberi, Raspberi,
Silverhand, V and K in the application and terminal theme pickers. The last three
are Cyberpunk-inspired palettes: yellow, red and cyan respectively. Unlocking is saved
per profile and does not change the active theme. Existing saved theme choices
continue to render, but remain absent from all four pickers until unlocked; a
locked saved bonus selection leaves its picker blank. A new profile starts with
the bonus themes hidden. Silverhand uses yellow filled tabs and cyan details,
V uses red and cyan, and K uses cyan with ice-blue details on graphite. These
accents cover the header, active tabs and status/pane edges; terminal ANSI
colors remain distinct for readable command output.

## Windows command line

The MSIs append each application's `bin` directory to **user PATH**. Reopen your
terminal after installation. Uninstall removes only that application's entry.
Pad includes the native `8p.exe` launcher:

```powershell
8p .
8p some.txt
8bitpad 'C:\path with spaces\notes.txt'
8bitshell --working-directory 'C:\path with spaces'
```

Relative paths use the invoking terminal's current directory. Pad forwards them
to its existing window, including spaces, Unicode and shell punctuation.
Portable Windows users can invoke `.\bin\8p.exe` directly or add that folder to
their own PATH. Linux packages install command wrappers in `/usr/bin`; curl
installs place wrappers in the configured user `bin` directory.

## Desktop verification

From a normal Windows PowerShell window in the 8bitShell checkout, after installing both apps:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Verify-WindowsInstall.ps1
```

This checks the installed runtimes with SDK paths removed, user PATH entries,
and Pad's live `8p . some.txt` handoff using a temporary profile and workspace.
Reports stay under `8bitShell/build/install-verification`. Existing documents,
settings and file associations are untouched.

Managed test hosts that deny user-local named pipes explicitly skip Pad's live
handoff check. That skip is not desktop verification; the script above requires
the actual handoff to pass.

For Shell, open a new PowerShell tab, run `grok`, then exit with **Ctrl+C**.
The prompt must return, and `Get-Location` must execute normally. Repeat in a
second tab. The automated fixture tests cover raw VT input, processed Ctrl+C,
Win32 console input, and a Ctrl+C copy binding with no selection. They do not
replace an interactive Grok test or require a model request.
