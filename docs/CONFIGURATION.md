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

## Search and command palette

Press **Ctrl+Shift+P**, or click **Search** in the title bar. Enter a host/IP,
connection name, setting, theme or local shell name. Use Up/Down to select,
Enter to run, Escape to return, and Tab/Shift+Tab to move between the search field,
selected result and close button. Customize or disable the shortcut under
**Settings → Shortcuts → Command palette**. Existing custom shortcuts are retained;
if an older profile already uses Ctrl+Shift+P, the new binding starts unassigned.

For an IP address, exact matching open tabs come first, followed by other matching
tabs, saved SSH connections, and explicit actions to open another SSH tab or create
a saved connection. Each tab shows its account and port so concurrent connections
can be distinguished. Selection uses stable tab/session IDs; moving, renaming or
closing a tab while searching cannot redirect activation to another tab. Native
SSH destinations are tracked for the local SSH helper and simple commands entered
at a supported integrated prompt; unknown or complex nested commands are not inferred.

Examples include `192.0.2.25`, `settings appearance`, `theme solarized`, `powershell`,
`wsl`, `sidebar`, and `cyberark`. Prefix a search with `>` to restrict it to commands,
local launchers and themes. Available WSL distributions and custom local profiles
appear by name; merely searching never launches a process or opens a connection.
CyberArk routes retain their normal gateway approval before a new connection opens.
If an exact saved connection has an unavailable route, new-connection actions
require an explicit arrow-key or mouse selection; Enter does not silently fall
back to direct SSH.

Theme actions change the active day/night slot and respect **Separate terminal &
UI themes**. Bonus themes remain hidden until unlocked with the version button.
The palette includes settings sections, panel toggles, day/night appearance,
CyberArk mode, key/route management, logging and highlighting. It stays closed
while a security review or editor requires attention.

Review dialogs keep **Cancel** initially selected. Left/Right or Tab/Shift+Tab
move between buttons with a visible focus border; Enter or Space activates the
focused button. Error dialogs focus their single **OK** button.

## Saved SSH accounts in local terminals

In a normal PowerShell or Bash prompt (including a WSL distribution using Bash),
type `ssh host`, `ssh user@host`, or `ssh -p 2222 user@host`. Matching saved SSH
browser entries appear beside the command. DNS names match without case and IPv6
addresses are normalized; an explicitly typed account or port constrains the match.
The first saved entry is selected. Use Up/Down or Tab to choose another, Enter to
connect, or Escape to dismiss the helper and retain the ordinary command.

The selected connection runs **inside that same local shell**, using its native
OpenSSH client and known-hosts database. Saved account, port, jump host, key and
compression settings are reused. Passwords/passphrases are entered into SSH itself;
the suggestions contain account names and key labels, never stored passwords.
The helper replaces the pending line only after selection and after the shell
has confirmed a fresh prompt. It does not create a managed SSH tab or enable that
tab's SFTP/monitoring controls.

When CyberArk mode is enabled, configured routes also appear for the target.
Choose a route explicitly to use its configured account rule and gateway. A route
suggestion alone does not change what ordinary Enter does. Saved routed entries
retain their required route and inherited folder account rule. Unavailable routes
and missing keys block assisted activation; they never cause a direct fallback.
Gateway approval and X11 consent still apply.

PowerShell resolves `ssh.exe` from application paths, falling back to the packaged
Windows client if unavailable. WSL uses the distribution's own `ssh`, which must
be installed inside that distribution.
The generated connection ignores native SSH config overrides so the selected saved
profile defines the destination. Native host-key prompts remain enabled. For WSL,
a selected Windows key is mapped with `wslpath` and copied into a private temporary
directory with mode 0600 for the command, then removed on normal exit. Abrupt process
or OS termination can leave that owner-only temporary directory.

Prompt integration is session-only; startup files are not edited. Assistance stays
off for running programs, password entry, nested/managed SSH sessions, alternate
screens, scrolled history, unsupported shells and explicit command launches.
Only a simple, single-display-line SSH command is recognized; remote commands,
pipelines, redirects, extra SSH options, wrapped or uncertain input continue as
ordinary terminal input. Bash needs PS0 support (4.4 or newer). Customized launchers
that bypass the standard interactive shell bootstrap retain ordinary terminal use.

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
`-X` is allowed. These files cannot weaken host-key or cryptographic policy. Read [SECURITY.md](../SECURITY.md)
before distributing a generated catalog.

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
  separateTerminalTheme: false
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
Appearance mode is `dark`, `light`, `system`, or `schedule`. By default, the terminal
follows the active UI theme. Enable **Separate terminal & UI themes** (YAML:
`appearance.separateTerminalTheme: true`) to choose independent day/night terminal
themes. Turning it off immediately applies the UI theme to both; the separate terminal
choices remain saved for when the option is enabled again. Older profiles without this
field also start with linked themes. Other sections include `logging`, `highlighting`,
`navigation`, and `macros`.
Terminal `textAntialiasing` accepts `system`, `grayscale`, or `aliased`;
`fontHinting` accepts `default`, `none`, `vertical`, or `full`. Omitted fields retain
defaults on loading older files, or their current values on partial import. These are
local display preferences, not transport or security options.
Export from the app to see every setting and its value. Settings imports reject invalid
types, out-of-range numbers, bad times, unknown keys, and conflicting shortcut assignments
before making any change. Shortcut spelling is normalized by Qt (e.g. `Insert` may become `Ins`).

## Password authentication

In a saved SSH session, choose **Authentication > Password**. This explicitly
uses SSH password authentication instead of application-default keys or an agent.
Leave **Remember on this device** off to enter the password in the terminal on
each connection. Remembering requires an explicit username and an effective
direct connection, including a folder that inherits direct SSH.

When remembering is enabled, enter the password in the masked field and save.
Editing a remembered session never fills the secret back into the form: leave
the field blank to keep it, enter a new value to replace it, or choose **Forget
saved password** and Save. Cancel discards those form changes. Changing the
host, port or username cannot send the previous password to the new account.
Duplicates have their own IDs and do not inherit a saved password.

Windows protects the local record with user-scoped DPAPI. macOS uses the login
Keychain; Linux uses the desktop Secret Service through optional system libsecret.
If the desktop keyring is unavailable or locked, saving reports an error and
manual password authentication remains available. Portable profiles do not make
stored credentials portable between computers, users or profile locations.

Only `authenticationMode: password` is written into the session YAML. Passwords
are never included in catalog/settings exports, search results, command arguments
or terminal input. A packaged askpass helper returns a saved password once through
a private local channel to OpenSSH, after its normal host-key verification.
The helper accepts the destination's password prompt, not host-key confirmation,
key passphrases or MFA challenges. Existing server trust requirements still apply.

Saved passwords work for managed SSH, the SFTP browser, monitoring, and native
PowerShell/Bash SSH assistance. SFTP and monitoring require a remembered password
because their background processes cannot display an interactive prompt. WSL
native SSH remains in the same terminal and asks for its password manually.
Target passwords are not forwarded to CyberArk gateways or ProxyJump hosts;
those prompts retain their separate interactive flow.

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
