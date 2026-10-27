# Version 1.0.0

First release under the 8bitShell / 8bitPad names. Matching Pixel Core icons: teal terminal and silver plain-text editor.

Packages bundle Qt and application runtime libraries. Windows packages also carry the redistributable MSVC runtime; Shell carries its SSH command-line tools. Linux requires a supported glibc desktop, its display service, and graphics drivers. macOS requires 14.4 or newer.

32-bit packages, when enabled and successfully built, are experimental and require explicit opt-in in the curl installers. Published artifacts must pass the platform build, application tests, package smoke tests, and checksum verification.

Candidates are unsigned/ad-hoc unless signing credentials have been configured. Review the target build reports, signing/notarization status, and project licensing before publishing the draft.
