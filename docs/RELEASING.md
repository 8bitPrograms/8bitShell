# 8bitShell 1.0 release preparation

The source version is recorded in `PROJECT_VERSION`. The current release path
is **local Windows x64 builds** and GitHub CLI uploads. Automatic push, tag and
pull-request Actions triggers are parked in `.github/workflows/build.yml`.
The existing manual workflow is retained for future use; do not dispatch it
while hosted builds are frozen.

## Local Windows release

Run from the repository root in your normal PowerShell session. Use an unused
version, update `PROJECT_VERSION`, and commit the reviewed changes before
building. Commit and tag messages must end with `[skip ci]` while Actions are
frozen. These commands do not enable or run Actions.

```powershell
$python = 'python' # Or the absolute path to a working Python 3.12+ interpreter.
$version = (Get-Content PROJECT_VERSION -Raw).Trim()
$stage = "dist/8bitShell-$version-windows-x64-stage"
$out = "dist/release-$version-windows-x64"
.\build-windows.cmd -Portable -PortableDirectory $stage -PythonExecutable $python
if ($LASTEXITCODE) { throw 'Build/test/startup checks failed; stop the release.' }
```

The build helper runs the complete test suite before deployment and tests
startup with the SDK environment removed. Use fresh output paths. A restricted
automation sandbox may deny the private named pipes used by `password_broker_test`
and `password_auxiliary_test`; run the full suite in the normal desktop session
and require it to pass before releasing. A developer preview packaged after
scoped checks is not a fully validated release.

For all formats together, run `build-windows.cmd -Package`. It builds/tests the
application, deploys the runtime and produces the setup EXE, MSI and portable
ZIP (plus tar.gz) with checksums in one output directory. `-PackageDirectory`
selects a fresh output folder. The EXE embeds the same MSI and invokes the
absolute system Windows Installer executable; it adds no second installed product.

The native setup launcher is compiled with the existing MSVC tools and requires
no NSIS installation. MSI packaging uses a SHA256-verified WiX 3.14.1 download/cache.
To package an already tested portable stage separately:

```powershell
$env:BIT8_PYTHON = (Get-Command $python -CommandType Application -ErrorAction Stop).Source
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/ci/package-windows.ps1 -Stage $stage -OutputDirectory $out -Architecture x64
if ($LASTEXITCODE) { throw 'Installer packaging failed.' }
& $python scripts/ci/distribution.py archive --stage $stage --out $out --os windows --arch x64
if ($LASTEXITCODE) { throw 'Archive packaging failed.' }
& $python scripts/ci/distribution.py stamp --out $out --os windows --arch x64
if ($LASTEXITCODE) { throw 'Package manifest failed.' }
& $python scripts/ci/check-windows-installer.py --setup "$out/8bitShell-$version-windows-x64-setup.exe" --msi "$out/8bitShell-$version-windows-x64.msi"
if ($LASTEXITCODE) { throw 'Installer payload verification failed.' }
```

`check-windows-installer.py` reads the MSI database, checks its version,
per-user scope and bundled console/runtime files, and verifies that setup
extracts the exact same MSI without installing anything. It also verifies
refusal to overwrite an existing extraction target. Real install/upgrade/remove
testing remains a separate desktop check.

In a restricted sandbox, `-PreviewWithoutMsiValidation` on the packager can
produce preview artifacts when the Windows Installer service is unavailable.
It explicitly omits ICE validation; the normal `-Package` flow retains that
validation and does not use this preview option.

Keep the stage free of personal `profile` data. Include the corresponding
application and Qt source bundles before finalizing:

```powershell
& $python scripts/ci/local-windows-sources.py --out $out --fetch
if ($LASTEXITCODE) { throw 'Corresponding source collection failed.' }
```

The helper requires a clean committed checkout, archives `HEAD`, verifies the
SDK version and checks every Qt source archive against its cached SHA256
sidecar before writing the source manifest. `--qt-root` and `--qt-sources`
select another matching SDK/cache. `--fetch` populates `tooling/qt-sources`
from the official Qt archive and its checksum sidecars; omit it when a complete
verified cache already exists. An unrelated or empty cache cannot substitute
for the SDK's corresponding sources.

Then run `python scripts/ci/release.py --release-check` and
`python scripts/ci/distribution.py finalize --out <output-folder> --repository 8bitPrograms/8bitShell`.
Finalization verifies package digests and writes `install-index.tsv`, the
download scripts, `release-manifest.json` and `SHA256SUMS.txt`. For this local
release, the index contains Windows x64 only; other platform recipes below
remain available for later validation.

Test the ZIP and native installer on Windows before uploading. Create a new
tag on the exact tested commit, push it, and prepare a draft with the CLI:

```powershell
gh auth status
git tag -a "v$version" -m "8bitShell $version [skip ci]"
git push origin "v$version"
gh release create "v$version" "$out/*" --repo 8bitPrograms/8bitShell --verify-tag --draft --title "8bitShell $version" --notes-file release-notes.txt
```

`release-notes.txt` must describe the actual checks and remaining limitations.
Inspect the uploaded draft and publish it from GitHub when ready. Never reuse
an existing release tag or overwrite an older release's assets.

For the prepared Windows preview folder (including `build-info.json`,
`RELEASE-NOTES.md` and `SHA256SUMS.txt`), one command verifies all three required
assets, pushes the recorded source branch and uploads them to the same draft:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/publish-windows-preview.ps1
```

`-Publish` creates a public prerelease instead of a draft. It always remains a
prerelease; it does not change the existing stable/latest release. The helper
refuses missing or changed assets and requires the exact recorded source commit.

## Release matrix

| Target | Runner / build environment | Packages | Status |
| --- | --- | --- | --- |
| Windows x64 | Windows 2022, native MSVC | MSI, EXE bootstrap, ZIP, tar.gz | Standard matrix; hosted run pending |
| Windows ARM64 | Windows 11 ARM, native MSVC | MSI, EXE bootstrap, ZIP, tar.gz | Standard matrix; hosted run pending |
| Linux x64 | Ubuntu 22.04, GCC 12 | tar.gz, ZIP, DEB, RPM, pkg.tar.zst | glibc 2.35 baseline; hosted run pending |
| Linux ARM64 | Ubuntu 22.04 ARM, GCC 12 | tar.gz, ZIP, DEB, RPM, pkg.tar.zst | glibc 2.35 baseline; hosted run pending |
| macOS Intel | macOS 15 Intel, Apple Clang | APP in DMG, ZIP, tar.gz | macOS 14.4 minimum; hosted run pending |
| macOS Apple Silicon | macOS 15 ARM, Apple Clang | APP in DMG, ZIP, tar.gz | macOS 14.4 minimum; hosted run pending |
| Windows x86 | Windows 2022, x86 MSVC + source-built Qt | MSI, EXE, ZIP, tar.gz | Opt-in experimental; Qt does not officially support this desktop target |
| Linux x86 | Debian 12 i386 container + source-built Qt | tar.gz, ZIP, DEB, RPM, pkg.tar.zst | Opt-in experimental; glibc 2.36 baseline |
| Linux ARMv7 | Debian 12 arm32v7, user-mode emulation + source-built Qt | tar.gz, ZIP, DEB, RPM, pkg.tar.zst | Opt-in experimental; glibc 2.36 baseline |

For Debian/Ubuntu, use DEB or the runtime archive. For Fedora, use RPM or the archive. For Arch, use pkg.tar.zst or the archive. ARM Arch-style packages target the community Arch Linux ARM ecosystem; the official Arch Linux container smoke test covers x64 only.

The retained hosted workflow requires the six standard targets when that workflow resumes. The current local release covers Windows x64 only. Experimental targets never silently substitute an x64 binary and require --experimental / -Experimental in the installer. There is no modern 32-bit macOS target.

## Runtime policy

Qt 6.11.2 is built from versioned official source archives with their official SHA256 sidecars. Each native job caches a matching SDK; there is no dependence on a developer's local Qt path. A private workstation SDK stays outside the hosted build.

Windows packages carry their Qt DLLs, plugins and app-local redistributable MSVC runtime. Shell includes the verified PuTTYgen converter and app-local OpenSSH client tools; it does not require the optional Windows OpenSSH feature or install a server/service.

Linux packages carry Qt, QML where applicable, plugins, fonts, OpenSSL and other linked non-system libraries. Runtime dependency discovery rejects unresolved ELF libraries, sets relative paths, and excludes glibc/its loader. X11/Wayland services, kernel interfaces, hardware graphics drivers and a supported glibc remain properties of the operating system. Bundling these would not make an arbitrary old/minimal/non-graphical OS a compatible desktop.

macOS packages carry a native APP and its Qt frameworks/plugins. Homebrew, Qt, Python and a compiler are build-machine tools; users do not need them to run a package.

The package smoke tests clear SDK/library environment paths. Linux also starts the archive in Ubuntu, Debian and Fedora containers with no Qt SDK installed; x64 adds Arch. These are offscreen startup/application checks. They do not replace interactive desktop or installer lifecycle testing.

## Curl installation

After the first published release exists, substitute the real organization and repository in these examples. The scripts accept --repo / -Repository today; the release workflow injects github.repository into the downloadable installer assets, so source files do not need a hardcoded organization.

Linux and macOS:

    curl -fsSL https://github.com/8bitPrograms/8bitShell/releases/latest/download/install.sh | sh

Pinned version:

    curl -fsSL https://raw.githubusercontent.com/8bitPrograms/8bitShell/main/install.sh | sh -s -- --repo 8bitPrograms/8bitShell --version 1.0.0

Portable directory:

    curl -fsSL https://raw.githubusercontent.com/8bitPrograms/8bitShell/main/install.sh | sh -s -- --repo 8bitPrograms/8bitShell --portable "$HOME/8bitShell-portable"

Windows, in PowerShell:

    curl.exe -fsSL https://github.com/8bitPrograms/8bitShell/releases/latest/download/install.ps1 -o "$env:TEMP\8bitShell-install.ps1"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:TEMP\8bitShell-install.ps1"

Windows portable ZIP:

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:TEMP\8bitShell-install.ps1" -Portable -InstallDirectory "$HOME\8bitShell-portable"

Source checkout before publishing:

    .\install.ps1 -Repository 8bitPrograms/8bitShell -Version 1.0.0 -DownloadOnly
    sh install.sh --repo 8bitPrograms/8bitShell --version 1.0.0 --download-only

The installer uses a small TSV release index instead of requiring Python/jq to parse a GitHub API response. It resolves latest to one exact release, checks OS/architecture and SHA256 before extracting, and never executes an installer in fixture tests. Linux/macOS installations are per-user and retain previous versions; Windows uses the per-user MSI. Existing application profiles are preserved. Portable launchers keep a separate profile next to the application.

## Build and package flow

The current entry point is `build-windows.cmd -Portable`, followed by the local
packaging steps above. Packaging runs after application tests succeed. The
parked `.github/workflows/build.yml` retains the broader hosted recipe for
future use. Its final job checks full matrix coverage and creates a draft;
the local Windows release deliberately has a smaller platform scope.

The release toolchain is copied into each repository; the apps do not depend on a sibling checkout or a future monorepo. Shared changes must be applied to both copies.

On a prepared native build host:

    python scripts/ci/build-qt.py --prefix tooling/qt-sdk-NATIVE_ARCH --jobs 2
    python scripts/ci/build.py --qt-root tooling/qt-sdk-NATIVE_ARCH --jobs 2

Windows initializes the matching MSVC environment through:

    .\scripts\ci\run-windows.ps1 -Architecture x64
    .\scripts\ci\run-windows.ps1 -Architecture arm64
    .\scripts\ci\run-windows.ps1 -Architecture x86

Build prerequisites belong only on the CI/developer host. Source-built Qt takes longer on the first run; its SDK and verified source archives are cached for later runs.

## macOS signing

Local/branch candidates use ad-hoc signing when no Apple identity is configured. For public macOS distribution, configure these GitHub secrets:

- MACOS_CERTIFICATE_P12: base64 Developer ID Application certificate.
- MACOS_CERTIFICATE_PASSWORD: certificate password.
- MACOS_SIGN_IDENTITY: exact Developer ID signing identity.
- MACOS_NOTARY_APPLE_ID, MACOS_NOTARY_TEAM_ID, MACOS_NOTARY_PASSWORD: notarytool credentials.

The signing helper imports the identity into a temporary runner keychain. The packaging step signs the APP, submits its DMG to Apple when notarization credentials exist, and staples the accepted DMG. Never commit credentials. Signing and notarization are not claimed as validated until a real credential-backed build has run.

Windows packages are unsigned until an Authenticode signing process/certificate is configured. They still include runtime libraries; signing status is a separate release property.

## Publication checklist

1. Choose an unused version and commit the reviewed source with `[skip ci]`.
2. Build Windows x64 locally and require all application/startup checks to pass.
3. Test the portable archive and native installer; collect matching source bundles.
4. Complete the publication-policy checks and finalize manifests/checksums.
5. Push the exact tested tag, upload with `gh release create --draft`, inspect and publish.

The two repositories have independent source and Git history. Build products, local SDKs, private profiles and dependency caches are ignored. The local source no longer depends on a previous project checkout.

## Sources used for platform decisions

- Qt supported desktop targets: https://doc.qt.io/qt-6/supported-platforms.html
- GitHub native hosted runner labels: https://docs.github.com/en/actions/reference/runners/github-hosted-runners
- Qt deployment tools: https://doc.qt.io/qt-6/qt-generate-deploy-app-script.html
- Qt QML deployment: https://doc.qt.io/qt-6/qt-generate-deploy-qml-app-script.html

Local checks and known limitations are recorded in the release validation log, kept outside this repository. Preparing files locally is not evidence that hosted Windows ARM, Linux, macOS, emulated 32-bit, signing, or distribution install tests have passed.

## Windows Start menu and removal

MSI and EXE installations appear under the 8bitPrograms publisher in Windows Installed apps. The shared Start menu folder is 8bitPrograms, containing 8bitShell / 8bitPad and Uninstall 8bitShell / Uninstall 8bitPad shortcuts. Each uninstall shortcut invokes Windows Installer for its own installed ProductCode. Removing one app retains the other app's shortcuts. Only empty installer-owned directories are removed; normal settings, editor recovery and saved connections/keys are retained.

Curl's normal Windows installation uses the same MSI and removal entries. ZIP/tar.gz packages remain portable: close the app and remove its extracted directory; copy its profile directory first if its portable settings are needed. Portable extraction creates no installed-app registration or Start menu shortcuts.
