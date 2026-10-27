# 8bitShell 1.0.0 validation

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
