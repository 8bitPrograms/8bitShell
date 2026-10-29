pragma Singleton

import QtQuick

QtObject {
    id: root

    property string uiThemeId: "catppuccin-mocha"
    property string terminalThemeId: "xterm"
    property bool bonusThemesUnlocked: false
    function isBonusTheme(id) { return id === "blueberry" || id === "blueberry-light" || id.indexOf("cyberpunk-") === 0 }
    function selectableUiThemes(selected) {
        return uiThemes.filter(function(theme) { return root.bonusThemesUnlocked || !root.isBonusTheme(theme.id) })
    }
    function selectableTerminalThemes(selected) {
        return terminalThemes.filter(function(theme) { return root.bonusThemesUnlocked || !root.isBonusTheme(theme.id) })
    }

    // Build each palette off-tree, then publish it as one value. Changing nine
    // inherited roles separately makes Qt re-resolve every control nine times.
    readonly property Component paletteFactory: Component { Palette {} }
    property var paletteCache: ({})
    function paletteFor(scheme) {
        if (!paletteCache[scheme.id]) {
            paletteCache[scheme.id] = paletteFactory.createObject(root, {
                window: scheme.canvas, windowText: scheme.text,
                base: scheme.surface, text: scheme.text,
                button: scheme.surfaceRaised, buttonText: scheme.text,
                highlight: scheme.accentDim, highlightedText: scheme.text,
                placeholderText: scheme.textFaint
            })
        }
        return paletteCache[scheme.id]
    }
    readonly property Palette controlPalette: paletteFor(activeUiTheme)

    function ui(id, name, canvas, surface, raised, hover, selected, border,
                borderSoft, text, muted, faint, accent, accentBright, accentDim,
                blue, amber, red, violet, green) {
        return {
            id: id, name: name, canvas: canvas, surface: surface,
            surfaceRaised: raised, surfaceHover: hover,
            surfaceSelected: selected, border: border, borderSoft: borderSoft,
            text: text, textMuted: muted, textFaint: faint, accent: accent,
            accentBright: accentBright, accentDim: accentDim, blue: blue,
            amber: amber, red: red, violet: violet, green: green
        }
    }

    function term(id, name, background, foreground, cursor, selection, ansi) {
        return {
            id: id, name: name, background: background, foreground: foreground,
            cursor: cursor, selection: selection, ansi: ansi
        }
    }

    function cyberUi(palette) {
        // Keep the shared layout; these themes have two deliberate chrome accents.
        palette.secondaryAccent = palette.id === "cyberpunk-k" ? "#91bcff" : "#00f0ff"
        palette.chromeSurface = Qt.tint(palette.surface, "#18" + palette.accent.slice(1))
        return palette
    }

    function lightUi(id, name, canvas, surface, raised, text, muted, accent,
                     blue, amber, red, violet, green) {
        return ui(id, name, canvas, surface, raised, Qt.darker(surface, 1.04),
                  Qt.tint(surface, "#1f" + accent.slice(1)),
                  Qt.darker(surface, 1.18), Qt.darker(surface, 1.08), text, muted,
                  muted, accent, Qt.darker(accent, 1.12),
                  Qt.tint(surface, "#21" + accent.slice(1)),
                  blue, amber, red, violet, green)
    }

    readonly property var uiThemes: [
        ui("blueberry", "Blueberi", "#090d14", "#101620", "#171d2e",
           "#222c43", "#263751", "#2d3852", "#202a40", "#e8eef7",
           "#9ba7c1", "#72809a", "#91a9f5", "#b7c6ff", "#293553",
           "#70a5eb", "#e5b567", "#f26d78", "#ad8ee6", "#7bd88f"),
        ui("dracula", "Dracula", "#282a36", "#21222c", "#343746",
           "#44475a", "#3c4053", "#6272a4", "#343746", "#f8f8f2",
           "#b8b8c4", "#6272a4", "#8be9fd", "#b6f3ff", "#244650",
           "#8be9fd", "#f1fa8c", "#ff5555", "#bd93f9", "#50fa7b"),
        ui("catppuccin-mocha", "Catppuccin Mocha", "#11111b", "#181825", "#1e1e2e",
           "#313244", "#313244", "#45475a", "#313244", "#cdd6f4",
           "#a6adc8", "#6c7086", "#89b4fa", "#b4d0ff", "#26344f",
           "#89b4fa", "#f9e2af", "#f38ba8", "#cba6f7", "#a6e3a1"),
        ui("nord", "Nord", "#242933", "#2e3440", "#3b4252",
           "#434c5e", "#3b5265", "#4c566a", "#3b4252", "#eceff4",
           "#d8dee9", "#7b88a1", "#88c0d0", "#a7d8e5", "#304b57",
           "#81a1c1", "#ebcb8b", "#bf616a", "#b48ead", "#a3be8c"),
        ui("tokyo-night", "Tokyo Night", "#16161e", "#1a1b26", "#24283b",
           "#292e42", "#283457", "#3b4261", "#292e42", "#c0caf5",
           "#a9b1d6", "#565f89", "#7aa2f7", "#9ab8ff", "#243457",
           "#7aa2f7", "#e0af68", "#f7768e", "#bb9af7", "#9ece6a"),
        ui("gruvbox-dark", "Gruvbox Dark", "#1d2021", "#282828", "#32302f",
           "#3c3836", "#504945", "#665c54", "#3c3836", "#ebdbb2",
           "#d5c4a1", "#928374", "#83a598", "#a9c3b9", "#344645",
           "#83a598", "#fabd2f", "#fb4934", "#d3869b", "#b8bb26"),
        ui("solarized-dark", "Solarized Dark", "#001b24", "#002b36", "#073642",
           "#0b4652", "#164e59", "#586e75", "#16404a", "#eee8d5",
           "#93a1a1", "#657b83", "#2aa198", "#55c2b8", "#0b4748",
           "#268bd2", "#b58900", "#dc322f", "#6c71c4", "#859900"),
        ui("one-dark", "One Dark", "#1b1d23", "#21252b", "#282c34",
           "#2f343d", "#33404f", "#3e4451", "#2c313a", "#abb2bf",
           "#8b93a1", "#5c6370", "#56b6c2", "#7ed0da", "#244149",
           "#61afef", "#e5c07b", "#e06c75", "#c678dd", "#98c379"),
        ui("monokai", "Monokai", "#1e1f1c", "#272822", "#30312b",
           "#3e3d32", "#49483e", "#75715e", "#3b3c35", "#f8f8f2",
           "#cfcfc2", "#75715e", "#66d9ef", "#94e7f5", "#29464a",
           "#66d9ef", "#e6db74", "#f92672", "#ae81ff", "#a6e22e"),
        ui("github-dark", "GitHub Dark", "#010409", "#0d1117", "#161b22",
           "#21262d", "#1f3348", "#30363d", "#21262d", "#f0f6fc",
           "#b1bac4", "#6e7681", "#58a6ff", "#79c0ff", "#17375e",
           "#58a6ff", "#d29922", "#f85149", "#bc8cff", "#3fb950"),
        ui("ayu-mirage", "Ayu Mirage", "#171b24", "#1f2430", "#242936",
           "#2d3442", "#324052", "#3d4757", "#2d3442", "#cccac2",
           "#b8b8b1", "#707a8c", "#5ccfe6", "#87dfef", "#25434d",
           "#73d0ff", "#ffd173", "#f28779", "#d4bfff", "#bae67e"),
        ui("everforest", "Everforest", "#232a2e", "#2d353b", "#343f44",
           "#3d484d", "#425047", "#56635f", "#3d484d", "#d3c6aa",
           "#a7c080", "#7a8478", "#83c092", "#a8d5b1", "#304b40",
           "#7fbbb3", "#dbbc7f", "#e67e80", "#d699b6", "#a7c080"),
        ui("rose-pine", "Rosé Pine", "#191724", "#1f1d2e", "#26233a",
           "#312e47", "#393552", "#524f67", "#312e47", "#e0def4",
           "#908caa", "#6e6a86", "#9ccfd8", "#c4e5ea", "#2e4650",
           "#31748f", "#f6c177", "#eb6f92", "#c4a7e7", "#9ccfd8"),
        ui("kanagawa", "Kanagawa", "#16161d", "#1f1f28", "#2a2a37",
           "#363646", "#2d4f67", "#54546d", "#363646", "#dcd7ba",
           "#c8c093", "#727169", "#7e9cd8", "#9eb5e3", "#293b58",
           "#7e9cd8", "#e6c384", "#e46876", "#957fb8", "#98bb6c"),
        ui("material-ocean", "Material Ocean", "#0b0f14", "#0f111a", "#171a24",
           "#1f2230", "#263451", "#30364a", "#202533", "#eeffff",
           "#b2ccd6", "#546e7a", "#89ddff", "#b6ebff", "#243d4a",
           "#82aaff", "#ffcb6b", "#ff5370", "#c792ea", "#c3e88d"),
        lightUi("blueberry-light", "Raspberi", "#f7edf3", "#fff8fb", "#f3e4ed",
                "#4b2940", "#7a586d", "#ae3f73", "#485ea6", "#946000", "#ba394e", "#7950a8", "#287642"),
        lightUi("catppuccin-latte", "Catppuccin Latte", "#e6e9ef", "#eff1f5", "#dce0e8",
                "#4c4f69", "#6c6f85", "#1e66f5", "#1e66f5", "#8b6500", "#d20f39", "#8839ef", "#357b0f"),
        lightUi("solarized-light", "Solarized Light", "#eee8d5", "#fdf6e3", "#eee8d5",
                "#586e75", "#657b83", "#167d75", "#268bd2", "#896700", "#dc322f", "#6c71c4", "#657800"),
        lightUi("github-light", "GitHub Light", "#f6f8fa", "#ffffff", "#f0f3f6",
                "#1f2328", "#59636e", "#0969da", "#0969da", "#9a6700", "#cf222e", "#8250df", "#1a7f37"),
        lightUi("one-light", "One Light", "#f0f0f0", "#fafafa", "#eaeaeb",
                "#383a42", "#696c77", "#4078f2", "#4078f2", "#986801", "#e45649", "#a626a4", "#507b24"),
        lightUi("rose-pine-dawn", "Rosé Pine Dawn", "#f2e9e1", "#faf4ed", "#fffaf3",
                "#575279", "#797593", "#286983", "#286983", "#966400", "#b4637a", "#907aa9", "#497c7c"),
        cyberUi(ui("cyberpunk-silverhand", "Silverhand", "#0b0d16", "#101620", "#18212c",
           "#232e3c", "#27313b", "#3b5361", "#243641", "#e8f0f2",
           "#a3b8c5", "#829aa9", "#fcee0a", "#fff77d", "#39380e",
           "#51b7e8", "#fcee0a", "#ff586b", "#d692ff", "#81e995")),
        cyberUi(ui("cyberpunk-v", "V", "#140d13", "#1c121c", "#291925",
           "#3a2331", "#442131", "#633548", "#3e2433", "#f4e4e8",
           "#c5a8b5", "#b596a3", "#ff4f5e", "#ff99a5", "#491e2a",
           "#74baff", "#fcee0a", "#ff6878", "#c0b2ff", "#8dedbd")),
        cyberUi(ui("cyberpunk-k", "K", "#0b141b", "#101e26", "#172b35",
           "#203c49", "#183947", "#31576a", "#203d4b", "#e2f2f5",
           "#adc6d0", "#89a7b2", "#00f0ff", "#91ffff", "#143c46",
           "#91bcff", "#f4e482", "#ff6176", "#b9a6ff", "#9aefcf"))
    ]

    readonly property var terminalThemes: [
        term("blueberry", "Blueberi", "#070b11", "#e8eef7", "#b7c6ff", "#2e3c61", ["#171d2e", "#f26d78", "#7bd88f", "#e5b567", "#91a9f5", "#ad8ee6", "#6acac4", "#c8d3e1", "#72809a", "#ff8791", "#9be7a8", "#f2ca86", "#b7c6ff", "#c6a9f4", "#8be0d8", "#f4f7fb"]),
        term("dracula", "Dracula", "#282a36", "#f8f8f2", "#f8f8f0", "#44475a", ["#21222c", "#ff5555", "#50fa7b", "#f1fa8c", "#bd93f9", "#ff79c6", "#8be9fd", "#f8f8f2", "#6272a4", "#ff6e6e", "#69ff94", "#ffffa5", "#d6acff", "#ff92df", "#a4ffff", "#ffffff"]),
        term("catppuccin-mocha", "Catppuccin Mocha", "#1e1e2e", "#cdd6f4", "#f5e0dc", "#45475a", ["#45475a", "#f38ba8", "#a6e3a1", "#f9e2af", "#89b4fa", "#f5c2e7", "#94e2d5", "#bac2de", "#585b70", "#f38ba8", "#a6e3a1", "#f9e2af", "#89b4fa", "#f5c2e7", "#94e2d5", "#a6adc8"]),
        term("nord", "Nord", "#2e3440", "#d8dee9", "#88c0d0", "#434c5e", ["#3b4252", "#bf616a", "#a3be8c", "#ebcb8b", "#81a1c1", "#b48ead", "#88c0d0", "#e5e9f0", "#4c566a", "#bf616a", "#a3be8c", "#ebcb8b", "#81a1c1", "#b48ead", "#8fbcbb", "#eceff4"]),
        term("tokyo-night", "Tokyo Night", "#1a1b26", "#c0caf5", "#c0caf5", "#33467c", ["#15161e", "#f7768e", "#9ece6a", "#e0af68", "#7aa2f7", "#bb9af7", "#7dcfff", "#a9b1d6", "#414868", "#f7768e", "#9ece6a", "#e0af68", "#7aa2f7", "#bb9af7", "#7dcfff", "#c0caf5"]),
        term("gruvbox-dark", "Gruvbox Dark", "#282828", "#ebdbb2", "#ebdbb2", "#504945", ["#282828", "#cc241d", "#98971a", "#d79921", "#458588", "#b16286", "#689d6a", "#a89984", "#928374", "#fb4934", "#b8bb26", "#fabd2f", "#83a598", "#d3869b", "#8ec07c", "#ebdbb2"]),
        term("solarized-dark", "Solarized Dark", "#002b36", "#839496", "#93a1a1", "#073642", ["#073642", "#dc322f", "#859900", "#b58900", "#268bd2", "#d33682", "#2aa198", "#eee8d5", "#002b36", "#cb4b16", "#586e75", "#657b83", "#839496", "#6c71c4", "#93a1a1", "#fdf6e3"]),
        term("one-dark", "One Dark", "#282c34", "#abb2bf", "#528bff", "#3e4451", ["#1e2127", "#e06c75", "#98c379", "#d19a66", "#61afef", "#c678dd", "#56b6c2", "#abb2bf", "#5c6370", "#e06c75", "#98c379", "#e5c07b", "#61afef", "#c678dd", "#56b6c2", "#ffffff"]),
        term("monokai", "Monokai", "#272822", "#f8f8f2", "#f8f8f0", "#49483e", ["#272822", "#f92672", "#a6e22e", "#f4bf75", "#66d9ef", "#ae81ff", "#a1efe4", "#f8f8f2", "#75715e", "#f92672", "#a6e22e", "#f4bf75", "#66d9ef", "#ae81ff", "#a1efe4", "#f9f8f5"]),
        term("github-dark", "GitHub Dark", "#0d1117", "#c9d1d9", "#58a6ff", "#264f78", ["#484f58", "#ff7b72", "#3fb950", "#d29922", "#58a6ff", "#bc8cff", "#39c5cf", "#b1bac4", "#6e7681", "#ffa198", "#56d364", "#e3b341", "#79c0ff", "#d2a8ff", "#56d4dd", "#f0f6fc"]),
        term("ayu-mirage", "Ayu Mirage", "#1f2430", "#cbccc6", "#ffcc66", "#34455a", ["#191e2a", "#f28779", "#bae67e", "#ffd580", "#73d0ff", "#d4bfff", "#95e6cb", "#c7c7c7", "#686868", "#f07178", "#c2d94c", "#ffb454", "#59c2ff", "#ffee99", "#95e6cb", "#ffffff"]),
        term("everforest", "Everforest", "#2d353b", "#d3c6aa", "#d3c6aa", "#475258", ["#475258", "#e67e80", "#a7c080", "#dbbc7f", "#7fbbb3", "#d699b6", "#83c092", "#d3c6aa", "#859289", "#e67e80", "#a7c080", "#dbbc7f", "#7fbbb3", "#d699b6", "#83c092", "#fff9e8"]),
        term("rose-pine", "Rosé Pine", "#191724", "#e0def4", "#524f67", "#403d52", ["#26233a", "#eb6f92", "#9ccfd8", "#f6c177", "#31748f", "#c4a7e7", "#ebbcba", "#e0def4", "#6e6a86", "#eb6f92", "#9ccfd8", "#f6c177", "#31748f", "#c4a7e7", "#ebbcba", "#e0def4"]),
        term("kanagawa", "Kanagawa", "#1f1f28", "#dcd7ba", "#c8c093", "#2d4f67", ["#16161d", "#c34043", "#76946a", "#c0a36e", "#7e9cd8", "#957fb8", "#6a9589", "#c8c093", "#727169", "#e82424", "#98bb6c", "#e6c384", "#7fb4ca", "#938aa9", "#7aa89f", "#dcd7ba"]),
        term("material-ocean", "Material Ocean", "#0f111a", "#8f93a2", "#ffcc00", "#273747", ["#000000", "#ff5370", "#c3e88d", "#ffcb6b", "#82aaff", "#c792ea", "#89ddff", "#ffffff", "#546e7a", "#ff5370", "#c3e88d", "#ffcb6b", "#82aaff", "#c792ea", "#89ddff", "#ffffff"]),
        term("blueberry-light", "Raspberi", "#fff8fb", "#4b2940", "#ae3f73", "#efd2e0", ["#4b2940", "#ba394e", "#287642", "#946000", "#485ea6", "#7950a8", "#147a72", "#e6d2dd", "#7a586d", "#cf4057", "#33864e", "#a76d00", "#586fc5", "#8a5bb9", "#188880", "#ffffff"]),
        term("catppuccin-latte", "Catppuccin Latte", "#eff1f5", "#4c4f69", "#dc8a78", "#ccd0da", ["#5c5f77", "#d20f39", "#40a02b", "#df8e1d", "#1e66f5", "#ea76cb", "#179299", "#acb0be", "#6c6f85", "#d20f39", "#40a02b", "#df8e1d", "#1e66f5", "#ea76cb", "#179299", "#bcc0cc"]),
        term("solarized-light", "Solarized Light", "#fdf6e3", "#657b83", "#586e75", "#eee8d5", ["#073642", "#dc322f", "#859900", "#b58900", "#268bd2", "#d33682", "#2aa198", "#eee8d5", "#002b36", "#cb4b16", "#586e75", "#657b83", "#839496", "#6c71c4", "#93a1a1", "#fdf6e3"]),
        term("github-light", "GitHub Light", "#ffffff", "#1f2328", "#0969da", "#b6d7ff", ["#24292f", "#cf222e", "#116329", "#4d2d00", "#0969da", "#8250df", "#1b7c83", "#6e7781", "#57606a", "#a40e26", "#1a7f37", "#633c01", "#218bff", "#a475f9", "#3192aa", "#8c959f"]),
        term("one-light", "One Light", "#fafafa", "#383a42", "#526fff", "#d0d0d0", ["#383a42", "#e45649", "#50a14f", "#c18401", "#4078f2", "#a626a4", "#0184bc", "#a0a1a7", "#696c77", "#e45649", "#50a14f", "#c18401", "#4078f2", "#a626a4", "#0184bc", "#ffffff"]),
        term("rose-pine-dawn", "Rosé Pine Dawn", "#faf4ed", "#575279", "#575279", "#dfdad9", ["#f2e9e1", "#b4637a", "#56949f", "#ea9d34", "#286983", "#907aa9", "#d7827e", "#575279", "#9893a5", "#b4637a", "#56949f", "#ea9d34", "#286983", "#907aa9", "#d7827e", "#575279"]),
        term("xterm", "XTerm", "#000000", "#ffffff", "#ffffff", "#4d4d4d", ["#000000", "#cd0000", "#00cd00", "#cdcd00", "#0000ee", "#cd00cd", "#00cdcd", "#e5e5e5", "#7f7f7f", "#ff0000", "#00ff00", "#ffff00", "#5c5cff", "#ff00ff", "#00ffff", "#ffffff"]),
        term("xterm-white", "XTerm Light", "#ffffff", "#000000", "#000000", "#b5d5ff", ["#000000", "#cd0000", "#00cd00", "#cdcd00", "#0000ee", "#cd00cd", "#00cdcd", "#e5e5e5", "#7f7f7f", "#ff0000", "#00ff00", "#ffff00", "#5c5cff", "#ff00ff", "#00ffff", "#ffffff"]),
        term("vga", "VGA", "#000000", "#aaaaaa", "#ffffff", "#555555", ["#000000", "#aa0000", "#00aa00", "#aa5500", "#0000aa", "#aa00aa", "#00aaaa", "#aaaaaa", "#555555", "#ff5555", "#55ff55", "#ffff55", "#5555ff", "#ff55ff", "#55ffff", "#ffffff"]),
        term("rxvt", "rxvt", "#000000", "#faebd7", "#faebd7", "#4d4d4d", ["#000000", "#cd0000", "#00cd00", "#cdcd00", "#0000cd", "#cd00cd", "#00cdcd", "#faebd7", "#404040", "#ff0000", "#00ff00", "#ffff00", "#0000ff", "#ff00ff", "#00ffff", "#ffffff"]),
        term("tango", "Tango", "#2e3436", "#d3d7cf", "#d3d7cf", "#555753", ["#2e3436", "#cc0000", "#4e9a06", "#c4a000", "#3465a4", "#75507b", "#06989a", "#d3d7cf", "#555753", "#ef2929", "#8ae234", "#fce94f", "#729fcf", "#ad7fa8", "#34e2e2", "#eeeeec"]),
        term("tango-light", "Tango Light", "#ffffff", "#2e3436", "#2e3436", "#d3d7cf", ["#2e3436", "#cc0000", "#4e9a06", "#c4a000", "#3465a4", "#75507b", "#06989a", "#d3d7cf", "#555753", "#ef2929", "#8ae234", "#fce94f", "#729fcf", "#ad7fa8", "#34e2e2", "#eeeeec"]),
        term("green-phosphor", "Green", "#000000", "#33ff33", "#33ff33", "#003300", ["#000000", "#cd0000", "#00cd00", "#cdcd00", "#0000ee", "#cd00cd", "#00cdcd", "#33ff33", "#007700", "#ff0000", "#00ff00", "#ffff00", "#5c5cff", "#ff00ff", "#00ffff", "#ccffcc"]),
        term("amber", "Amber", "#000000", "#ffb000", "#ffb000", "#4a3200", ["#000000", "#cd0000", "#00cd00", "#cdcd00", "#0000ee", "#cd00cd", "#00cdcd", "#ffb000", "#7f5800", "#ff0000", "#00ff00", "#ffff00", "#5c5cff", "#ff00ff", "#00ffff", "#ffe0a0"])
    ].concat([
        term("cyberpunk-silverhand", "Silverhand", "#0b0d16", "#e8f0f2", "#fcee0a", "#263f50", ["#101a25", "#ff586b", "#81e995", "#fcee0a", "#51b7e8", "#d692ff", "#00f0ff", "#dce7eb", "#879aa8", "#ff8190", "#a4f3ab", "#fff77d", "#91dbff", "#ebbcff", "#8affff", "#f5fcff"]),
        term("cyberpunk-v", "V", "#140d13", "#f4e4e8", "#ff4f5e", "#442131", ["#21121e", "#ff6878", "#8dedbd", "#fcee0a", "#74baff", "#c0b2ff", "#00f0ff", "#eee0e7", "#b294a3", "#ff99a5", "#bcffda", "#fff88a", "#a1d3ff", "#e0cfff", "#94faff", "#fff4f7"]),
        term("cyberpunk-k", "K", "#0b141b", "#e2f2f5", "#00f0ff", "#183947", ["#14252d", "#ff6176", "#9aefcf", "#f4e482", "#91bcff", "#b9a6ff", "#00f0ff", "#d6e9ee", "#89a7b2", "#ff9aaa", "#bcffe1", "#fff7b6", "#bfdaff", "#e0cfff", "#91ffff", "#f0fcff"])
    ])

    function findTheme(id, themes, fallbackId) {
        const normalized = String(id || "").toLowerCase()
        for (let index = 0; index < themes.length; ++index) {
            if (themes[index].id === normalized)
                return themes[index]
        }
        for (let index = 0; index < themes.length; ++index) {
            if (themes[index].id === fallbackId)
                return themes[index]
        }
        return themes.find(function(theme) { return !root.isBonusTheme(theme.id) })
    }

    function themeIndex(id, themes, missingIndex) {
        const normalized = String(id || "").toLowerCase()
        for (let index = 0; index < themes.length; ++index) {
            if (themes[index].id === normalized)
                return index
        }
        return missingIndex === undefined ? 0 : missingIndex
    }

    // Qt's attached ToolTip uses AutoText. Escape every untrusted value before
    // handing it to that control; only this fixed paragraph markup is emitted.
    function tooltip(value) {
        return "<p>" + String(value).slice(0, 4096).replace(/&/g, "&amp;")
            .replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/\n/g, "<br>") + "</p>"
    }
    function terminalScheme(id) { return findTheme(id, terminalThemes, "xterm") }
    function uiThemeIndex(id) { return themeIndex(id, uiThemes) }
    function terminalThemeIndex(id) { return themeIndex(id, terminalThemes) }

    readonly property var activeUiTheme: findTheme(uiThemeId, uiThemes, "catppuccin-mocha")
    readonly property var activeTerminalTheme: findTheme(terminalThemeId, terminalThemes, "xterm")

    readonly property bool cyberpunkChrome: activeUiTheme.id.indexOf("cyberpunk-") === 0
    readonly property color secondaryAccent: activeUiTheme.secondaryAccent || activeUiTheme.blue
    readonly property color chromeSurface: activeUiTheme.chromeSurface || activeUiTheme.surface
    readonly property color chromeBorder: cyberpunkChrome ? secondaryAccent : borderSoft
    // The title/command bar is the primary color field. Its foreground and
    // controls have separate roles so terminal and sidebar text stay light.
    readonly property color headerSurface: cyberpunkChrome ? accent : chromeSurface
    readonly property color headerText: cyberpunkChrome ? canvas : text
    readonly property color headerTextMuted: cyberpunkChrome ? Qt.tint(accent, "#c0000000") : textMuted
    readonly property color headerHover: cyberpunkChrome ? Qt.tint(accent, "#18000000") : surfaceHover
    readonly property color headerPressed: cyberpunkChrome ? Qt.tint(accent, "#30000000") : surfaceSelected
    readonly property color headerDivider: cyberpunkChrome ? Qt.tint(accent, "#50000000") : borderSoft
    readonly property color headerControlBorder: cyberpunkChrome ? Qt.tint(accent, "#70000000") : border
    readonly property color headerActiveText: cyberpunkChrome ? accent : accentBright
    readonly property color headerFocus: cyberpunkChrome ? canvas : accent
    readonly property color activeTabBackground: cyberpunkChrome ? accent : canvas
    readonly property color activeTabText: cyberpunkChrome ? canvas : text

    readonly property color canvas: activeUiTheme.canvas
    readonly property color surface: activeUiTheme.surface
    readonly property color surfaceRaised: activeUiTheme.surfaceRaised
    readonly property color surfaceHover: activeUiTheme.surfaceHover
    readonly property color surfaceSelected: activeUiTheme.surfaceSelected
    readonly property color terminalBackground: activeTerminalTheme.background
    readonly property color terminal: terminalBackground
    readonly property color border: activeUiTheme.border
    readonly property color borderSoft: activeUiTheme.borderSoft
    readonly property color text: activeUiTheme.text
    readonly property color textMuted: activeUiTheme.textMuted
    readonly property color textFaint: activeUiTheme.textFaint
    readonly property color accent: activeUiTheme.accent
    readonly property color accentBright: activeUiTheme.accentBright
    readonly property color accentDim: activeUiTheme.accentDim
    readonly property color blue: activeUiTheme.blue
    readonly property color amber: activeUiTheme.amber
    readonly property color red: activeUiTheme.red
    readonly property color violet: activeUiTheme.violet
    readonly property color green: activeUiTheme.green

    readonly property string uiFont: Qt.platform.os === "windows" ? "Segoe UI" : "Inter"
    readonly property string monoFont: Qt.platform.os === "windows" ? "Consolas" : "Noto Sans Mono"
    // The original 8-11 px chrome was designed around Linux font metrics and
    // is too small with Segoe UI at Windows' common 100% scale. Keep Linux
    // geometry unchanged while giving Windows text a readable baseline; native
    // per-monitor DPI scaling is handled separately by the application manifest.
    readonly property real uiFontScale: Qt.platform.os === "windows" ? 1.2 : 1.0
    function uiFontSize(pixelSize) { return Math.round(pixelSize * uiFontScale) }
    readonly property int radiusSmall: 5
    readonly property int radius: 8
    readonly property int radiusLarge: 12
    readonly property int unit: 4
}
