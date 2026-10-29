# Version 1.0.0

Optional stable-release checks and SHA-256 verified downloads are available in Settings > Updates. Automatic installation is off by default; installed per-user Windows copies can opt in to installation on exit. Portable, Linux and macOS copies offer manual downloads.

Windows MSI installation adds the app's command directory to user PATH and removes its own entry on uninstall. Console input supports ConPTY's Win32 keyboard mode and key releases. Ctrl+C interrupts a foreground program when the copy binding has no selection.

First release under the 8bitShell / 8bitPad names. Matching Pixel Core icons: teal terminal and cobalt blue editor with a transparent icon background.

Packages bundle Qt and application runtime libraries. Windows packages also carry the redistributable MSVC runtime; Shell carries its SSH command-line tools. Linux requires a supported glibc desktop, its display service, and graphics drivers. macOS requires 14.4 or newer.

32-bit packages, when enabled and successfully built, are experimental and require explicit opt-in in the curl installers. Published artifacts must pass the platform build, application tests, package smoke tests, and checksum verification.

Candidates are unsigned/ad-hoc unless signing credentials have been configured. Review the target build reports, signing/notarization status, and project licensing before publishing the draft.
