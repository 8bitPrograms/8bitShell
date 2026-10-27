# 8bitShell identity

The selected identity for version 1.0.0 uses the original teal terminal prompt inside a stepped square. 8bitPad uses the matching silver plain-text mark with no folded corner. There is no visible umbrella product name.

| Surface | Value |
| --- | --- |
| Display name and executable | 8bitShell |
| Linux package slug | 8bitshell |
| C++ namespace / internal target prefix | bitshell |
| QML module | BitShell |
| Build / environment prefix | BIT8SHELL |
| New profile directory | 8bitshell |
| Desktop / application ID | io.github.8bitshell.App |

The terminal icon uses teal #00BCAC and graphite #20252A. Pad uses silver #D5DCE5 and graphite #20252A. SVG, PNG, multi-size Windows ICO and macOS ICNS assets are provided. The ICO contains 16, 20, 24, 32, 40, 48, 64, 96, 128 and 256 px entries.

Shell's main assets are assets/bitshell.svg, assets/bitshell-mark.svg, assets/bitshell.png, assets/bitshell.ico and assets/bitshell.icns. QML renders the vector through BrandMark.qml.

An explicit BIT8SHELL_PROFILE selects an isolated absolute profile path. Otherwise the app uses the 8bitshell directory beneath the platform configuration root. YAML documents use 8bitshell kinds; unrelated application directories are not adopted or merged.

The source checkout uses origin git@github.com:8bitPrograms/8bitShell.git and independent new Git metadata. For build/test results and release setup, see RELEASE-VALIDATION.md and RELEASING.md.
