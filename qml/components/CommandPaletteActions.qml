import QtQuick
import BitShell

QtObject {
    id: root
    objectName: "commandPaletteActions"
    readonly property var sections: ["Profile", "Appearance", "Terminal", "Shortcuts", "Authorization",
        "Rules", "Logging", "Data & About", "Local profiles", "Updates"]

    function search(text, tabs) {
        const input = String(text || "").trim()
        const commandsOnly = input.startsWith(">")
        const needle = (commandsOnly ? input.slice(1).trim() : input).toLowerCase()
        const terms = needle.split(/\s+/).filter(term => term.length)
        const parsed = commandsOnly ? ({}) : SessionStore.parseSshTarget(input)
        const targetKey = parsed.host ? SessionStore.sshHostKey(parsed.host) : ""
        function matches(value) {
            const haystack = value.toLowerCase()
            return terms.every(term => haystack.indexOf(term) >= 0)
        }
        function command(id, title, subtitle, keywords, type, target) {
            return {id: id, category: "Commands", title: title, subtitle: subtitle || "",
                    keywords: keywords || "", type: type || "command", target: target || "", enabled: true}
        }
        const open = [], saved = [], connections = [], actions = [], locals = [], themes = []
        if (!commandsOnly) {
            for (const tab of tabs || []) {
                const exact = targetKey && SessionStore.sshHostKey(tab.host || "") === targetKey
                              && (!parsed.user || parsed.user === tab.user)
                              && (!parsed.port || Number(parsed.port) === Number(tab.port))
                if (!exact && !matches([tab.name, tab.host, tab.user, tab.gateway, tab.localName,
                                       tab.host ? "ssh" : "local terminal"].join(" "))) continue
                const target = tab.host ? (tab.user ? tab.user + "@" : "") + tab.host
                              + (tab.port && tab.port !== 22 ? ":" + tab.port : "") : tab.localName || "Local terminal"
                open.push({id: "tab:" + tab.id, type: "tab", target: tab.id, category: "Open tabs",
                    exactMatch: Boolean(exact), tabOrder: open.length,
                    title: tab.name || target, subtitle: target + (tab.gateway ? " · via " + tab.gateway : "")
                           + " · " + (tab.running ? "Open" : "Ended") + (tab.active ? " · current tab" : ""), enabled: true})
            }
            open.sort((left, right) => Number(right.exactMatch) - Number(left.exactMatch) || left.tabOrder - right.tabOrder)
            // Exact destinations also cover normalized IPv6 and explicit user/port.
            const exact = parsed.host ? SessionStore.sshCandidates(parsed.host, parsed.user || "", parsed.port || 0,
                             PersonalProfile.cyberArkEnabled, PersonalProfile.routingIdentity, 80).results : []
            // An unavailable saved route must not turn Enter into an implicit direct connection.
            const explicitNewConnection = exact.some(entry => entry.valid === false)
            const seen = new Set()
            const entries = exact.concat(input ? SessionStore.search(input, 80).results : [])
            for (const entry of entries) {
                if (seen.has(entry.id)) continue
                seen.add(entry.id)
                saved.push({id: "saved:" + entry.id, type: "saved", target: entry.id, category: "Saved connections",
                    title: entry.name, subtitle: (entry.account || entry.user ? (entry.account || entry.user) + "@" : "")
                           + entry.host + (entry.port !== 22 ? ":" + entry.port : "")
                           + (entry.group ? " · " + entry.group : "")
                           + (entry.gateway ? " · via " + entry.gateway : ""),
                    enabled: entry.valid !== false, reason: entry.reason || ""})
            }
            if (parsed.host) {
                connections.push({id: "connect:new", type: "connect", target: input, category: "New connection",
                    autoSelect: !explicitNewConnection,
                    title: "Open a new SSH tab to " + parsed.host,
                    subtitle: "Connect now" + (parsed.user ? " · " + parsed.user : "") + " · keeps existing tabs open", enabled: true})
                connections.push({id: "save:new", type: "save", target: input, category: "New connection",
                    autoSelect: !explicitNewConnection,
                    title: "Create a saved connection for " + parsed.host,
                    subtitle: "Review account, key and route before saving", enabled: true})
                const routes = SessionStore.sshRouteCandidates(parsed.host, parsed.user || "", parsed.port || 0,
                    PersonalProfile.cyberArkEnabled, PersonalProfile.routingIdentity, 40).results
                for (const route of routes)
                    connections.push({id: "route:" + route.routeId, type: "routeConnect", target: route.routeId,
                        autoSelect: !explicitNewConnection,
                        category: "CyberArk routes", title: "New SSH via " + (route.routeName || route.name),
                        subtitle: (route.account || "Account required") + " · " + parsed.host + " via " + route.gateway,
                        enabled: route.valid, reason: route.reason || ""})
            }
        }

        const commands = [
            command("ssh:new", "New SSH connection…", "Enter a host or IP address", "connect remote ssh host ip", "query", "ssh "),
            command("session:new", "Create a saved SSH connection…", "Open the connection editor", "save profile ssh host", "save"),
            command("local:default", "Open default local terminal", LocalProfileStore.defaultProfile.name || "Local shell", "shell terminal new", "local", LocalProfileStore.defaultProfileId),
            command("theme:choose", "Change theme…", "Search available themes", "appearance color palette", "query", "theme "),
            command("appearance:toggle", "Switch to " + (AppSettings.darkAppearance ? "day" : "night") + " appearance", "Use the configured day/night themes", "theme dark light toggle appearance"),
            command("sidebar:toggle", (AppSettings.sidebarVisible ? "Hide" : "Show") + " connections sidebar", "", "toggle sidebar connections panel"),
            command("files:toggle", (AppSettings.fileBrowserVisible ? "Hide" : "Show") + " file browser", "", "toggle files explorer panel"),
            command("macros:toggle", (AppSettings.macroBarVisible ? "Hide" : "Show") + " macro bar", "", "toggle macro panel"),
            command("separate:toggle", AppSettings.separateTerminalTheme ? "Link terminal and UI themes" : "Separate terminal and UI themes", "Preserve independent terminal choices", "toggle separate theme appearance"),
            command("cyberark:toggle", (PersonalProfile.cyberArkEnabled ? "Disable" : "Enable") + " CyberArk mode", "Existing connections remain open", "cyberark gateway routing toggle"),
            command("routes:manage", "Manage CyberArk routes", "", "cyberark gateway routes", "routes"),
            command("keys:manage", "Manage SSH keys", "", "authentication credentials key identity", "keys"),
            command("credentials:manage", "Manage credentials", "Usernames with passwords or keys, kept in the keychain", "authentication credentials password login account secret", "credentials"),
            command("logging:toggle", (AppSettings.logByDefault ? "Disable" : "Enable") + " session logging", "Use the logging preference for terminal sessions", "logging record toggle"),
            command("highlight:toggle", (AppSettings.highlightByDefault ? "Disable" : "Enable") + " terminal highlighting", "", "highlight syntax colors toggle")
        ]
        for (const section of sections)
            commands.push(command("settings:" + section, "Settings: " + section, "Open this settings section", "settings preferences options " + section, "settings", section))
        // Credential rows read CredentialStore only while searching, so it
        // stays unloaded at startup.
        const current = (tabs || []).find(tab => tab.active)
        if (current && current.running && needle) {
            for (const credential of CredentialStore.credentials)
                commands.push(command("credential:" + credential.id, "Use credential " + credential.name + " in this tab",
                    credential.username + " · answers the password prompt of ssh you type here", "credential login password key use bind " + credential.username,
                    "bindCredential", credential.id))
            if (current.credentialBound)
                commands.push(command("credential:unbind", "Stop using credential " + current.credential + " in this tab", "",
                    "credential unbind stop forget", "unbindCredential"))
        }
        for (const entry of commands)
            if (matches(entry.title + " " + entry.subtitle + " " + entry.keywords)) actions.push(entry)
        for (const profile of LocalProfileStore.profiles) {
            if (!matches("open new local shell terminal " + profile.name + " " + profile.program + " " + profile.distribution)) continue
            locals.push({id: "profile:" + profile.id, type: "local", target: profile.id, category: "Local shells",
                title: "Open " + profile.name, subtitle: profile.distribution ? "WSL · " + profile.distribution : profile.program,
                enabled: true})
        }
        for (const theme of Theme.selectableUiThemes("")) {
            if (!needle || !matches("theme appearance ui color " + theme.name + " " + theme.id)) continue
            themes.push({id: "ui-theme:" + theme.id, type: "uiTheme", target: theme.id, category: "Themes",
                title: theme.name, subtitle: (AppSettings.separateTerminalTheme ? "UI" : "UI and terminal")
                    + " · current " + (AppSettings.darkAppearance ? "night" : "day") + " theme", enabled: true})
        }
        if (AppSettings.separateTerminalTheme) for (const theme of Theme.selectableTerminalThemes("")) {
            if (!needle || !matches("theme terminal color " + theme.name + " " + theme.id)) continue
            themes.push({id: "terminal-theme:" + theme.id, type: "terminalTheme", target: theme.id, category: "Terminal themes",
                title: theme.name, subtitle: "Terminal only · current " + (AppSettings.darkAppearance ? "night" : "day") + " theme", enabled: true})
        }
        // Prefer the tab that already exists; connecting is always an explicit row.
        return (needle ? open.concat(saved, actions, locals, themes, connections)
                       : actions.concat(open, locals)).slice(0, 240)
    }
}
