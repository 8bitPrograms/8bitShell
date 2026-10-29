# Windows port

For current local build/test commands and the self-contained portable folder, see
[BUILDING.md](BUILDING.md).

## Local build-tool installation

The supplied MSVC Qt SDK does not include a C++ compiler or Windows SDK. For a
local build, open PowerShell as Administrator once, change to the repository
root, and run:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy RemoteSigned -Force
& .\scripts\install-windows-build-tools.ps1
```

The execution-policy setting applies only to that PowerShell window and expires
when it closes. It does not override an organization-managed Group Policy.

The script downloads the official signed Visual Studio 2022 Build Tools
bootstrapper and installs the v143 x64/x86 compiler, Windows 11 SDK, CMake/Ninja,
and AddressSanitizer. It verifies the Microsoft signature before execution,
checks the installed tools, and never requests a Windows restart. It can reuse
an existing complete VS 2022 toolchain. Qt and Rust are not installed by it.

Return to a normal, non-administrator terminal for building and testing
8bitShell. The application refuses elevated execution. If a copied build cache
contains another machine's source path, configure into a fresh build directory;
do not reuse that cache. The installation helper needs ordinary Internet access
and administrator rights; a restricted Codex session may not have either.

Installer/component references:
[Microsoft command-line setup](https://learn.microsoft.com/en-us/visualstudio/install/use-command-line-parameters-to-install-visual-studio?view=vs-2022),
[Build Tools components](https://learn.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-build-tools?view=vs-2022).

## Cloud build without a local Windows toolchain

The recommended validation path does not require Visual Studio, Qt, CMake, WiX
or NSIS on the developer PC. After this repository exists on GitHub's default
branch, open **Actions → Build, test and package → Run workflow**, leave **Build target**
as `all` to build every supported package, or select `windows` for only the
Windows artifacts. Leave **Build downloadable packages** enabled. The job uses a
[temporary GitHub-hosted `windows-2022` machine](https://docs.github.com/en/actions/reference/runners/github-hosted-runners),
installs the build-only dependencies there, compiles the application and runs
the Windows component tests.

The manual run uploads ZIP/MSI/EXE evaluation artifacts without publishing a
GitHub Release. They are unsigned and expire with the Actions retention period.
The separate tag-driven release workflow remains blocked by the publication
policy until licensing and redistribution review are complete. Ordinary pushes
and pull requests run the complete Linux, Fedora and Windows test matrix without
uploading packages.

Because GitHub only exposes **Run workflow** after the workflow file is present
on the [default branch](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/manually-run-a-workflow),
an empty repository needs one initial push. After that source-only push, start
the packaging run manually from Actions. This
changes no machine-wide settings on the developer PC and does not install a
local toolchain.

## Architecture and build

The Windows port uses the same C++23/Qt Quick UI, terminal parser, connection
policies, YAML catalogs, themes and macros as Linux. Native differences are
confined to `Platform`, `PrivateFile` and `PtyProcess`. Linux still uses `forkpty`
and its original POSIX file protections; Windows uses ConPTY and Win32 handles.
Both platforms now compile a SHA-256-pinned libvterm 0.3.3 source with the local
`cmake/LibvtermPatch.cmake` patch. It initializes the parser's fragment pointer,
avoids subtracting null pointers, bounds the fixed CSI argument array and checks
CSI/OSC arithmetic before multiplication. Oversized numeric controls are rejected.
The screen patch also bounds reflow at the first visible row when a wrapped line
begins in scrollback, preventing a read before the screen buffer during resize.
When restoring narrower history lines, the terminal supplies one-column blank
cells rather than zero-width padding so expanding split panes cannot stall reflow.
This supersedes the earlier system-libvterm dependency; installed Linux binaries
must be rebuilt/reinstalled to acquire the patch. This is not a full parser audit
or process isolation. `VtermSafetyTest` exercises hostile fragmented controls and
the wrapped-line resize regression.

Target: **Windows 11 x64, MSVC 2022, Qt 6.11.2**. This is a port under validation,
not yet a signed, enterprise-certified Windows release.

Install the C++ x64 compiler, Windows 11 SDK and CMake tools from Visual Studio
Build Tools. Use the MSVC x64 Qt SDK, including Qt Declarative, SVG and Shader
Tools. From a normal (non-administrator) developer PowerShell:

```powershell
.\build-windows.cmd -QtRoot C:/Qt/6.11.2/msvc2022_64 -Portable
```

The script builds, runs tests, and only packages if the tests pass. See
[release tooling and licensing gates](RELEASING.md) for the MSI/EXE prerequisites.
Local portable folders are written under `dist/8bitShell-windows-x64-<timestamp>`.
Run `Start-Portable.cmd` from the entire folder; Qt DLLs, plugins, QML modules,
app-local MSVC runtime and the locked SSH client are part of the application. The script does not disable security tools,
change machine-wide execution policy or request elevation.

Windows release builds also carry a console-only PuTTYgen converter solely for importing
PuTTY PPK private keys into 8bitShell's protected OpenSSH-key library. The build downloads
the official source version in `packaging/puttygen.lock.json`, verifies its SHA-256 digest,
and compiles the upstream command-line frontend with MSVC and Windows CNG randomness. A
provenance sidecar binds the source lock to the resulting binary hash, and packaging checks
it again before staging. PuTTYgen is not an SSH transport and is never selected through
`PATH`. The local build script creates the converter automatically; `BIT8SHELL_PUTTYGEN` or
`-PuttygenPath` may only point to an executable produced by the same verified builder.

The current portable flow bundles the redistributable MSVC runtime and locked
OpenSSH client alongside Qt. The target computer does not need a developer SDK,
compiler, or a separate SSH installation for the bundled client.

Qt 6.11 uses architecture-specific repository directories. aqtinstall 3.3.0's
legacy `install-qt` lookup does not understand that layout. Use Qt's official
installer or verified archives from the official architecture repository; do not
disable TLS or checksum verification to work around the lookup failure.

## Native behavior and security boundaries

- Local tabs start the selected default profile (initially Windows PowerShell).
  The split **+ / arrow** button offers detected PowerShell versions, Command Prompt,
  WSL distributions, and custom executables such as SQL*Plus. Configure names,
  arguments and per-tab starting directories in **Settings → Local profiles**;
  see [local profile configuration](CONFIGURATION.md#local-shell-and-command-profiles).
  WSL discovery is asynchronous and does not start a distribution. Local profiles
  use the normal user's token; embedded administrator shells are not supported.
- Local monitoring reads CPU, memory, network, fixed-disk and uptime counters
  from Windows APIs; Linux SSH targets keep using the separate read-only `/proc`
  sampling channel.
- Windows uses Segoe UI for chrome and Consolas as the default terminal font;
  saved user-selected terminal fonts are preserved. Linux font defaults are unchanged.
- The executable declares Per-Monitor V2 DPI awareness and embeds a multi-resolution
  Windows icon. Windows chrome uses a slightly larger Segoe UI baseline while the OS
  scale factor still follows each monitor; terminal font preferences remain independent.
- Initial window size fits the available desktop where the 1080x680 minimum
  client area can fit. Startup panels restore without an entrance animation.
  Diagnostic screenshots wait for completed panel transitions, QML creation and
  rendered frames; an unrenderable window fails after a bounded timeout instead
  of returning a misleading early capture.
- SSH/SFTP use the Windows system OpenSSH executables, addressed by absolute path
  under the Windows system directory. No PATH-based substitute is silently used.
- PPK input is converted once by the bundled, fixed-path PuTTYgen executable. Any
  PPK passphrase is staged in protected temporary storage, never placed in process
  arguments or YAML, and is deleted after the bounded conversion attempt.
- Managed sessions retain strict host-key checking and the existing algorithm
  exclusions. The port does not lower minimum key sizes or enable old algorithms.
- ConPTY receives an explicit executable and escaped literal arguments via
  `CreateProcessW`, never through `cmd /c`. `%`, `@`, quotes, backslashes and Unicode
  in arguments are not interpolated by a shell.
- Input/output have independent pipe workers and bounded queues. Process exit
  and ConPTY shutdown are not waited for on the GUI thread. Job objects terminate
  the session's descendants when its root exits or the tab closes.
- Application handles are not inherited by terminal children.
  Standard handles are explicitly reset so a redirecting launcher cannot divert
  an embedded session's input or output into its own pipes.
- Private files use protected ACLs for the current user and SYSTEM, explicit
  ownership checks on writes, handle-based atomic renames and bounded reads.
  Ancestor directory handles pin paths during operations. Reparse points,
  hard-linked private write targets, UNC/device paths and alternate-data-stream
  syntax are rejected; there is no insecure fallback.
- Local storage paths that rely on junctions or redirected network folders may
  therefore fail explicitly. Choose a local, ordinary directory in those cases.
- The application refuses an elevated administrator/SYSTEM launch. Runtime DLL
  lookup is restricted to the application and Windows system directories; injected
  Qt/QML import-path environment variables are removed at startup. The executable
  and its bundled dependencies still need a trusted installation directory.
- ASLR, DEP, stack protection and Control Flow Guard are enabled in MSVC builds.
  These are hardening measures, not a claim that every vulnerability is prevented.
- Windows administrators, debuggers, malware running as the user, and OS crash-dump
  policies are outside this application's protection boundary. There is no claim
  of Windows memory-dump prevention, FIPS certification or a completed penetration
  test. Defender and other endpoint protections remain enabled.

## Data and compatibility

Configuration remains versioned, readable YAML under the Windows generic config
directory plus `bitshell` (normally `%LOCALAPPDATA%\bitshell`). Existing
profiles are selected in order: `bitshell`, `8bitshell`, `8bitshell`, `8bitshell`.
Older profiles and keys remain in place; saves preserve their original YAML kind
prefix, while exports use `8bitshell/*`. Profiles are not copied or merged. An absolute
`XDG_CONFIG_HOME` explicitly selects another config root, including isolated tests.
Exports contain portable catalog/macro structure; Linux absolute key/log paths
must be changed to Windows paths after importing. Passwords are not exported.

Windows has filenames which Linux permits but Win32 cannot safely represent,
such as `NUL`, trailing spaces/dots and `name:stream`. Such download names are
rejected rather than silently rewritten.

## Acceptance scope

The native Windows test suites cover command-line quoting/Unicode, ConPTY
input/output/resize/shutdown/backpressure, private ACLs, junction/hardlink
rejection, non-overwriting publication and OpenSSH's effective local policy.
Linux-only shell/sshd fixtures are explicitly not counted as Windows evidence.
Actual test results and UI verification must be recorded for each artifact.
Run `ui_interaction_test.exe` with `QT_QPA_PLATFORM=windows` and without a
`QT_QUICK_BACKEND` override on an unlocked test desktop for native UI coverage.
The default CTest run deliberately uses offscreen/software rendering; that alone
does not validate the Windows event loop or graphics path. The synthetic folder
drag cancellation test posts only to its own Windows test window to wake Qt's
native drag-start loop; it is not an Explorer interoperability test.

Still separate acceptance work:

- Real corporate SSH/CyberArk authentication, MFA and gateway policy. No corporate
  infrastructure is contacted by the port tests.
- Real Windows-to-server SFTP, agent access, Explorer drag/drop and remote monitoring
  across the required server fleet. SFTP remains key/agent-only, as on Linux.
- X11 integration with an approved Windows X server; no X server is bundled.
- Installer/upgrade/rollback, Authenticode signing, dependency redistribution and
  license review, accessibility/DPI and broad real-hardware performance testing.
- RDP/VNC and shared synchronization remain existing roadmap items, not newly
  implemented capabilities of this port.

## Primary references

- [Qt supported platforms](https://doc.qt.io/qt-6/supported-platforms.html)
- [Qt Windows deployment](https://doc.qt.io/qt-6/windows-deployment.html)
- [Microsoft ConPTY lifecycle](https://learn.microsoft.com/en-us/windows/console/creating-a-pseudoconsole-session)
- [Microsoft ConPTY redirected-handle behavior](https://github.com/microsoft/terminal/discussions/15814)
- [CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw)
- [File security and access rights](https://learn.microsoft.com/en-us/windows/win32/fileio/file-security-and-access-rights)
- [Handle-based file renaming](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info)
- [DLL security](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-security)
- [Windows OpenSSH key management](https://learn.microsoft.com/en-us/windows-server/administration/openssh/openssh_keymanagement)
