# Qt runtime notice

The Windows and Linux bundled previews dynamically link Qt 6.11.2 libraries and
load Qt plugins. Native distro packages use their declared system Qt dependency.
Qt is copyright The Qt Company Ltd. and other contributors. The LGPL version 3
license text and its incorporated GPL version 3 terms are included in
`qt-licenses`. Qt libraries remain separate, replaceable shared libraries. This project adds
no restriction on modification or reverse engineering for debugging changes to
the LGPL-covered libraries.

Upstream sources, component attribution and license information:

- https://code.qt.io/cgit/qt/qtbase.git/tree/?h=v6.11.2
- https://code.qt.io/cgit/qt/qtdeclarative.git/tree/?h=v6.11.2
- https://code.qt.io/cgit/qt/qtsvg.git/tree/?h=v6.11.2
- https://code.qt.io/cgit/qt/qtshadertools.git/tree/?h=v6.11.2
- https://code.qt.io/cgit/qt/qttools.git/tree/?h=v6.11.2
- https://code.qt.io/cgit/qt/qttranslations.git/tree/?h=v6.11.2
- https://code.qt.io/cgit/qt/qtwayland.git/tree/?h=v6.11.2
- https://doc.qt.io/qt-6/licensing.html
- https://doc.qt.io/qt-6/third-party-licenses.html

When available in the selected SDK, its original CycloneDX documents are retained
under `vendor-sbom`. These describe the SDK components, not an independently
validated, exact application SBOM. They can include tools/modules not deployed
in a portable archive. Qt's embedded third-party code has additional license terms.
The locked Linux SDK also contains ICU 73.2; the deployed ICU and its license/source
obligations must be included in the redistribution review.

This is a private evaluation build. Before external distribution, review the
actual deployed component set, retain the matching Qt source/configuration and
all required third-party notices, and complete the project's licensing/release
gates. This notice alone is not a completed redistribution-compliance assessment.

Windows OpenSSH is an operating-system prerequisite, not redistributed here.
The Microsoft C++ runtime must be installed from the official signed Microsoft
redistributable; compiler-directory runtime DLLs are not bundled.
