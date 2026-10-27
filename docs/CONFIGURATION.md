# Readable configuration and interchange

8bitShell uses UTF-8 YAML as the source of truth for sessions, settings, and macro groups.
The files live in `$XDG_CONFIG_HOME/8bitshell/`, normally `~/.config/8bitshell/`.

The app uses only its canonical 8bitshell profile, or an explicit BIT8SHELL_PROFILE override. Private files remain local. No unrelated profile directory is adopted or merged automatically.

| File | `kind` | Contents |
| --- | --- | --- |
| `sessions.yaml` | `8bitshell/sessions` | SSH profiles and nested/empty folder paths |
| `settings.yaml` | `8bitshell/settings` | Appearance, terminal, shortcuts, logging, navigation defaults |
| `macros.yaml` | `8bitshell/macros` | Editable groups, ordered buttons, folder assignments |
| `keys.yaml` | `8bitshell/keys` | Local key names, fingerprints, and default assignments; never private-key contents |
| `local-profiles.yaml` | `8bitshell/local-profiles` | Default local launcher and customized shell/command profiles |

Sessions now export `version: 2` (version 1 still loads); settings and macros use
`version: 1`. Local `personal.yaml` stores the ordinary display identity, the CyberArk
master switch and employee ID, picture preference, and gateway approvals separately.
`keys.yaml` and the private `keys/` directory are also
device-local and never included in catalog/settings exports. See
[CyberArk routing and personal profile](CYBERARK.md) for route/account fields.
Files are readable without the app; no proprietary encoding or
encrypted blobs are required. The importable examples are in [`examples/config`](../examples/config/).
The older `examples/catalog` and `examples/profiles` files describe future policy/profile
designs and are **not** the runtime import format.

## In the UI

- **Connections → … → Session YAML import / export…** exports the complete catalog or merges
  another catalog. Existing session IDs are skipped, never overwritten. Folder paths merge.
  mRemoteNG and OpenSSH importers remain available.
- **Settings → Data & About** imports/exports settings and sessions. Imported settings
  apply immediately; keys omitted from the import keep their current values. The confirmation
  dialog explains the replacement behavior before applying settings.
- **Macro groups** imports groups, exports all groups plus assignments, or exports the selected
  group without machine-specific folder assignments. Export saves the currently edited group
  first. Change the name, text, Enter behavior, or button order in the editor and save.
- **Reload YAML** reads external edits without restarting. A settings reload fills missing keys
  with defaults; unlike a partial import, it treats the file as the complete configuration.
  Unsaved macro editor contents are replaced on reload.
- The terminal's macro group selector switches between **Auto** (folder inheritance) and a
  manually chosen group. A folder assignment changes that folder's descendants; the nearest
  nonempty assigned group wins. Empty groups inherit from the next parent.

Macro import keeps identical groups with the same ID. When the ID exists but the contents
differ, it adds a copy with a new ID and an `(imported)` name suffix. Existing groups are never
silently overwritten. The optional **Also import folder assignments** checkbox is off by
default; even when enabled, existing folder assignments are retained. Imported groups never
run on import. Review command text before pressing their buttons.

## Local shell and command profiles

The left side of the terminal **+** button (and the new-terminal shortcut) opens
the default local profile. The arrow lists detected and customized profiles and
links to **Settings → Local profiles**. The initial terminal uses this same default.
Windows discovers Windows PowerShell, installed `pwsh.exe` versions, Command Prompt,
and WSL distributions. WSL discovery uses the asynchronous, bounded
[`wsl --list --quiet`](https://learn.microsoft.com/en-us/windows/wsl/basic-commands)
command; it does not boot each distribution. Use **Refresh detected shells** after
installing or removing a shell. Linux also lists available bash/zsh/fish shells.

Profiles have a name, an executable, literal arguments, and a starting directory.
In the editor, put each argument on its own line; do not add quotes just because
a path has spaces. Executables may be absolute paths or commands found on `PATH`.
Use an interpreter explicitly for scripts (for example, `powershell.exe` with
`-File` and the script path as separate arguments). Saved arguments are plaintext:
for SQL*Plus use `/nolog` and enter credentials interactively.

A blank starting directory inherits the application's launch directory. `~` means
the current user's home directory; other native directories must be absolute and
already exist. For WSL, the field is a Linux path or `~`, passed as a literal
`--cd` value to the selected distribution. Each tab gets its own working directory;
opening or restarting it never changes another tab's directory. Duplicate/restart
uses the original tab's profile snapshot; edits apply to newly opened tabs.

Editing a detected profile saves an override. **Reset** restores its detected
values; **Duplicate** creates a separate profile. Removing a customized default
returns the default to the system shell. No shell is started just by saving or
refreshing a profile. A missing executable or directory produces a visible error.
Administrator shells in embedded tabs are not supported.

`local-profiles.yaml` uses version 1 and is machine-local, separate from SSH catalog
exports. It follows the same atomic-save, external-edit and malformed-file
protection as the other YAML files. **Reload profiles from disk** reloads manual
edits; saving never silently overwrites changes made by another instance.

```yaml
version: 1
kind: bitshell/local-profiles
defaultProfile: custom:sqlplus
profiles:
  - id: custom:sqlplus
    name: SQL*Plus
    kind: local
    distribution: ''
    program: 'C:\oracle\instantclient\sqlplus.exe'
    arguments: [/nolog]
    workingDirectory: 'C:\work\sql'
```

## Session format

```yaml
version: 1
kind: bitshell/sessions
folders: [Databases/Oracle/Production, Empty folder]
sessions:
  - id: oracle-production-01
    name: Oracle production
    host: oracle.example.test
    user: operator
    port: 22
    folder: Databases/Oracle/Production
    tags: [oracle, production]
    backspaceMode: bs
```

`id` and `host` are required nonempty strings; IDs must be unique in a file. Keep IDs stable in
a session generator. `name`, `user`, `folder`, `tags`, `identityKey`, `identityFile`, `proxyJump`, `forwardX11`,
`agentForwarding`, `compression`, `port`, and `backspaceMode` are optional. Default port is 22;
the booleans default to false; `backspaceMode` defaults to `default` (global setting), or accepts
`bs` / `del`. Unspecified text and tags are empty. Folder ancestors are created automatically.
Folders use `/`, regardless of OS. `identityKey` is empty for the application default,
`none` for ordinary OpenSSH/agent discovery, or a local 32-character managed-key id.
`identityFile` remains an advanced reference to an existing local file. A managed key id is
intentionally not portable: importing a shared catalog never imports private key material,
and a missing explicit id blocks authentication instead of silently falling back.

A catalog supports up to 10,000 sessions and 10,000 expanded folders. A folder path is
limited to 1024 characters and 64 levels; session text fields to 4096 characters. These
limits apply to reloads, imports, and saved changes; invalid data is rejected, not truncated.

Connection validation rejects SSH option/shell syntax in hosts, users, jump hosts, and key
paths. Key paths must be absolute or start with `~/`, without SSH expansion tokens (`%`, `$`).
`proxyJump` accepts up to eight comma-separated `[user@]host[:port]` hops; bracket IPv6.
Legacy `agentForwarding: true` is retained as data but blocks connection until unchecked.
`forwardX11: true` requires an explicit per-connection warning; only untrusted, time-limited
`-X` is allowed. These files cannot weaken host-key or cryptographic policy. See the
[security assessment](SECURITY-ASSESSMENT.md) before distributing a generated catalog.

For edits to existing IDs, edit `sessions.yaml` and reload, or use the session editor. Reimport
is deliberately an additive operation, not an update/replace operation. Replacing a catalog
does not terminate running terminals; their connections keep the settings used at launch.

## Macro format

```yaml
version: 1
kind: bitshell/macros
groups:
  - id: oracle-tools
    name: Oracle tools
    buttons:
      - label: Database status
        text: |-
          select name, open_mode
          from v$database;
        enter: false
folders:
  Databases/Oracle: oracle-tools
```

Groups require `id`, `name`, and a `buttons` list (which may be empty). Buttons require a
nonempty `label` and `text`. `enter` is optional and defaults to false. `folders` maps folder
paths to group IDs in this document; an empty-string path assigns the global fallback.

There are at most 256 groups and 256 buttons per group. Names, IDs and labels allow 256
characters; button text allows 256 Ki UTF-16 code units. The overall YAML budget also applies.

Use `|-` for multiline text without a final newline, `|` to retain one final newline, or a
quoted string with explicit escapes when necessary. The exporter preserves exact command
text, including Unicode, quotes, tabs, and trailing newlines. Embedded newlines still belong
to the pasted text even if `enter: false`; that flag only controls the extra Enter afterward.
Macros are plain text, **not a password vault**. Do not include secrets in them or their exports.
Multiline text, invisible formatting, and `enter: true` require a full-content confirmation
before transmission. Terminal control characters such as ESC are refused. Paste is limited
to 256 Ki UTF-16 code units and less than 1 MiB of UTF-8; a rejected paste sends nothing.

## Settings format

Settings use named sections with the same readable keys as the old INI file:

```yaml
version: 1
kind: bitshell/settings
appearance:
  uiTheme: catppuccin-mocha
  terminalTheme: xterm
  uiLightTheme: catppuccin-latte
  terminalLightTheme: xterm
  mode: dark
  dayStart: "07:00"
  nightStart: "19:00"
terminal:
  fontSize: 14
  textAntialiasing: system
  fontHinting: default
  backspaceMode: del
  copyOnSelect: false
monitoring:
  enabledByDefault: false
navigation:
  sidebarVisible: true
  fileBrowserVisible: false
shortcuts:
  copy: Ctrl+Shift+C
  paste: Ctrl+Shift+V
```

Fresh profiles default to Catppuccin Mocha/Latte. `blueberry` and `blueberry-light` remain
stable compatibility IDs displayed as Blueberi and Raspberi.
Appearance mode is `dark`, `light`, `system`, or `schedule`. UI and terminal slots remain
independent. Other sections include `logging`, `highlighting`, `navigation`, and `macros`.
Terminal `textAntialiasing` accepts `system`, `grayscale`, or `aliased`;
`fontHinting` accepts `default`, `none`, `vertical`, or `full`. Omitted fields retain
defaults on loading older files, or their current values on partial import. These are
local display preferences, not transport or security options.
Export from the app to see every setting and its value. Settings imports reject invalid
types, out-of-range numbers, bad times, unknown keys, and conflicting shortcut assignments
before making any change. Shortcut spelling is normalized by Qt (e.g. `Insert` may become `Ins`).

## SSH key metadata and private storage

**Settings → Authorization → SSH keys** imports up to 64 OpenSSH/PEM or PuTTY PPK private keys. The source
file is never modified. OpenSSH keys are validated with the system `ssh-keygen`; RSA keys
below 3072 bits and DSA/unknown key types are refused. PPK is converted once to the modern
OpenSSH private-key format with a fixed-path PuTTYgen executable. The application continues
to use system OpenSSH for every connection; it never switches the transport to Plink.

`keys.yaml` is readable metadata resembling:

```yaml
version: 1
kind: bitshell/keys
defaultKey: "0bff7b778f6a49dda223066f36ae841e"
cyberArkKey: "c9ec58d744a34d729336124895816006"
keys:
  - id: "0bff7b778f6a49dda223066f36ae841e"
    name: "Personal ED25519"
    file: "key-0bff7b778f6a49dda223066f36ae841e-51de67887dbe4515ba586ef0c615a10a.key"
    format: "openssh"
    fingerprint: "SHA256:example"
    type: "ED25519"
    bits: 256
```

The `file` value is a constrained relative name inside the private `keys/` directory.
Private keys themselves never appear in YAML, exports, logs, session search, or About.
On replacement, a new validated generation is committed first and the stable `id` remains;
session/default assignments therefore survive daily key rotation. The dedicated CyberArk
key takes precedence only after a session resolves to a CyberArk route. Otherwise an explicit
session key, legacy external path, application default, or ordinary OpenSSH/agent behavior
applies in that order. Whenever a concrete key is supplied, 8bitShell also sends
`IdentitiesOnly=yes` so unrelated agent keys are not offered.

An encrypted PPK passphrase is passed to PuTTYgen through a private transient runtime file,
cleared from the UI after the attempt, and never saved in configuration. It may still exist
briefly in process memory, like any passphrase entered into a desktop client. Official
packages bundle a PuTTYgen command-line converter built from SHA-256-locked official source
beside 8bitShell; it is used only for one-time conversion and never for a connection. If a
developer build omits it, OpenSSH imports continue to work and PPK import fails visibly
without a PATH-based fallback.

## Safety, migration, and concurrent editing

- On first launch, the application creates YAML from existing `sessions.json`, `macros.json`,
  and `settings.ini`. Originals stay untouched. Once YAML exists, it takes precedence;
  editing the old files has no effect. A malformed legacy file blocks migration rather than
  being replaced by an empty configuration.
- Writes use atomic replacement, owner-only file permissions, and a short lock against other
  app instances. A content check refuses a stale save if the file changed externally. Reload
  before continuing. Do not keep old and new app versions editing the same configuration.
- Syntax/validation errors and unsupported versions leave the previous in-memory model and
  disk file intact. Saving is blocked until the corrected file is reloaded. Errors appear in
  the YAML controls; sessions also show errors through the catalog UI.
- Exports must target a separate file, not an active configuration file. Existing export-file
  replacement is confirmed by the save dialog. Exports may contain hostnames, usernames,
  local paths, folder organization, and macro commands: review before sharing.
- Supported input is one mapping document, at most 8 MiB, 32 nested container levels,
  200,000 parser events, and 1 MiB per scalar. Limits are checked before building a YAML
  object graph. Duplicate keys, unknown fields, custom object tags, and **all anchors and
  aliases** are rejected. Legacy INI/JSON reads are also bounded; INI migration parses a
  private snapshot. mRemoteNG XML allows 128 MiB, 64 levels, and 200,000 elements, but no DTD.
  Use lowercase `true` / `false`; quote numeric-looking names and text such as `"true"`.
- UI saves standardize formatting and do not retain custom YAML comments or anchor syntax.
  Data, button order, and command text are preserved. Keep commented templates under version
  control and import them if preserving editorial comments matters.

Highlight rules remain in `highlights.json`; terminal logs remain ordinary text `.log` files.
Neither changes as part of this configuration migration.

New config/log directories use `0700`, written files `0600`; unsafe symlink/special-file paths
are refused. Existing parent directories, legacy backups, and historical exports are not
recursively changed. Review their permissions separately. Symlink-based dotfile layouts
must use a real directory/file for the app's canonical storage.

YAML parsing and emission use [yaml-cpp](https://github.com/jbeder/yaml-cpp/wiki/Tutorial)
0.9 or later (bundled fallback pinned to 0.9.0 and SHA-256), with preflight budgets and
application-level validation layered over the parser.
