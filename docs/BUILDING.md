# Build and test 8bitShell locally

Run commands from the root of the **8bitShell** repository. These are source
build instructions for contributors. A packaged application bundles its runtime;
a source build needs a compiler and a matching Qt development SDK.

## Windows x64: quickest path

Prerequisites:

- Visual Studio 2022 C++ Build Tools, the Windows SDK, and its CMake/Ninja tools.
- A Qt MSVC x64 SDK. CMake requires Qt >= 6.10; the release configuration uses
  **6.11.2**. Modules: Core, Gui, Qml, Quick, QuickControls2, QuickDialogs2,
  Concurrent, Network, Svg and Test, with their SDK dependencies.
- Python 3.12+ for the locked PuTTYgen/OpenSSH helpers and release checks.
- Network access for the first dependency download, or previously verified caches.

Use a normal PowerShell window. The helper finds Visual Studio and initializes
MSVC itself; CMake and Ninja need not be on your global PATH.

```powershell
.\build-windows.cmd -Diagnose
.\build-windows.cmd
# Build, run all tests, deploy Qt + CRT + SSH, and check portable startup:
.\build-windows.cmd -Portable
```

SDK lookup: `-QtRoot`, `BIT8SHELL_QT_ROOT`, `build/tooling/qt-sdk`,
`tooling/qt-sdk-x64`, then `C:/Qt/6.11.2/msvc2022_64`. Set another SDK explicitly:

```powershell
.\build-windows.cmd -QtRoot C:/Qt/6.11.2/msvc2022_64 -Jobs 4 -Portable
```

An ignored local SDK can live in `build/tooling/qt-sdk`. If `python` points at
the Windows Store alias, pass a real interpreter with `-PythonExecutable`.

```powershell
.\build-windows.cmd -Diagnose -PythonExecutable <path-to-python.exe>
.\build-windows.cmd -Portable -PythonExecutable <path-to-python.exe>
```

The helper verifies PuTTYgen provenance, reuses optional
`tooling/source-cache/libvterm` and `tooling/source-cache/yaml-cpp`, and prepares
the SHA256-locked OpenSSH client. It uses a workspace-local temporary directory
for tests. `-PortableDirectory dist/my-new-folder` selects a fresh destination;
existing packages and profiles are preserved.

| Output | Location |
| --- | --- |
| CMake/Ninja build | `build/windows-release` |
| Application binary | `build/windows-release/8bitShell.exe` |
| Test details | `build/windows-release/tests/*_test.txt` |
| CTest failure log | `build/windows-release/Testing/Temporary/LastTest.log` |
| New portable application | `dist/8bitShell-windows-x64-<timestamp>` |
| Portable startup diagnostics | `build/package-smoke-windows-x64` |

Launch **Start-Portable.cmd** from the complete newly generated folder. Its
settings stay in that folder's `profile` directory. The raw build executable
depends on the SDK environment and is not the normal launch path.

The local `-Portable` flow needs no WiX/NSIS and does not install the app.
Native MSI/EXE and CI ZIP/tar.gz production use [RELEASING.md](RELEASING.md).
The older `-Package` switch invokes the separate CPack installer path.

## Linux: native development build

Use CMake >= 3.24, Ninja, Git, a C++23 compiler, and a complete matching Qt SDK
with the modules listed above. Native OpenSSH tools are needed for SSH/SFTP tests
and use. Optional PPK import requires PuTTYgen; the release builder builds the
locked converter automatically. A distro's default Qt may be too old: check the
actual SDK rather than assuming Fedora, Debian/Ubuntu or Arch has a given version.

With Qt installed in a custom prefix:

```bash
export CMAKE_PREFIX_PATH=/absolute/path/to/qt-sdk
cmake -S . -B build/local -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBIT8SHELL_BUILD_TESTS=ON
cmake --build build/local --parallel 4
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  ctest --test-dir build/local --output-on-failure --no-tests=error
./build/local/8bitShell
```

CMake downloads hash-pinned libvterm and, when not installed, yaml-cpp. To use
existing source caches offline, add these to the configure command:

```bash
-DFETCHCONTENT_SOURCE_DIR_BITSHELL_VTERM_SOURCE=/absolute/path/to/libvterm \
-DFETCHCONTENT_SOURCE_DIR_YAML-CPP=/absolute/path/to/yaml-cpp
```

Run as your normal desktop user. To install a development build in your own
prefix after testing:

```bash
cmake --install build/local --prefix "$HOME/.local"
```

This is a development install. For the fully collected runtime archive, package
checks, glibc baseline and distro formats, follow [RELEASING.md](RELEASING.md).

## macOS: native development build

Use Xcode's C/C++ developer tools, CMake, Ninja, and a Qt SDK for your machine
(Intel x64 or Apple Silicon ARM64). The release configuration targets macOS
**14.4+** and Qt **6.11.2**. Do not mix architectures in one build directory.

```bash
export CMAKE_PREFIX_PATH=/absolute/path/to/native/qt-sdk
cmake -S . -B build/local-macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=14.4 \
  -DBIT8SHELL_BUILD_TESTS=ON
cmake --build build/local-macos --parallel 4
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  ctest --test-dir build/local-macos --output-on-failure --no-tests=error
open build/local-macos/8bitShell.app
```

The macOS flow is prepared for native execution but has not been run on this
Windows workstation. APP deployment, DMG creation, signing and notarization are
separate steps in the release guide.

## Repeat tests and inspect failures

On Windows, rerun the build helper to restore the SDK/MSVC environment and run
all tests. For a focused run, open a developer PowerShell, put Qt's `bin`
directory on PATH, and use:

```powershell
$env:PATH = "$PWD/build/tooling/qt-sdk/bin;" + $env:PATH
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QUICK_BACKEND = 'software'
$env:TEMP = "$PWD/build/windows-release/test-temp"
$env:TMP = $env:TEMP
# cmake/ctest must be available in this developer terminal.
ctest --test-dir build/windows-release -R 'startup_directory|windows_pty|local_profile' --output-on-failure
& $python -m unittest discover -s scripts/ci/tests -v
```

On Linux/macOS, use the equivalent `ctest --test-dir <build>` command. Test
counts differ by platform; inspect the current CTest summary. A skipped remote
SFTP fixture means that scenario was not exercised. Offscreen tests do not
replace interactive graphics/desktop testing.

The `startup_directory_test` regression uses a controlled PTY executable to
verify that an explicit startup folder wins over a saved profile directory,
that spaces/Unicode/punctuation stay literal, and that the saved profile remains
unchanged. See the matching integration steps in
[8bitPad's local guide](https://github.com/8bitPrograms/8bitPad/blob/main/doc/Building.md#check-open-shell-with-8bitshell)
when both repositories are cloned side by side.

## Check the startup folder manually

Launch the fresh portable executable with an existing folder:

```powershell
& .\dist\<new-portable-folder>\bin\8bitShell.exe --working-directory 'C:/path with spaces/project'
```

Run `Get-Location` inside its PowerShell tab. It should report the requested
folder even when the default local profile has another starting directory.
Later tabs opened from a saved profile retain that profile's configured directory.
With `-e PROGRAM ARGS...`, place `--working-directory` before `-e`; arguments
after `-e` belong literally to the child program.

## Troubleshooting

For the optional updater, installed command aliases and desktop verification,
see [UPDATES.md](UPDATES.md). Automatic installation is off by default.

- Start with `-Diagnose`; absence from PATH alone does not mean a tool is missing.
- Use an MSVC SDK with MSVC, not a MinGW Qt SDK. Match Qt and CPU architecture.
- After moving a checkout or switching toolchains, use a fresh CMake build tree;
  cached absolute paths from another checkout cannot be reused.
- The helper uses Release compiler probes to avoid mixed debug/release PDB tools.
- A missing DLL/platform plugin when running a loose EXE usually means the full
  portable folder or development environment is missing. Rebuild with `-Portable`.
- If a sandbox blocks a path, distinguish an access denial from a missing SDK.
- Do not change machine-wide execution policy or install the application to test
  a source change. The launcher runs PowerShell without a profile.

Windows x64 is locally exercised. Hosted Linux/ARM/macOS and experimental 32-bit
release validation remain separate and are tracked outside this repository.
